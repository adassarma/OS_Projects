#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "proxyserver.h"
#include "safepqueue.h"


/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;


int *listener_fds;
pqueue_t *workqueue;
thr_pool_t *threadpool;
int verbose = 0;

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// -------- SOLUTION FUNCTIONS START -------
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

// ****************************************
// ---- Threadpool and worker functions ---
// ****************************************

/*
 * worker thread executes this function after delaying
 */
void serve_request(work_t *work) {
    int client_fd = work->fd;

    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }
    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);
    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));
    // forward the client request to the fileserver
    int ret = http_send_data(fileserver_fd, work->httpreq, strlen(work->httpreq));
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }
    // forward the fileserver response to the client
    while (1) {
        int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
        // printf("Response from fileserver (%d bytes)\n", bytes_read);
        if (bytes_read <= 0) // fd has been closed, break
            break;
        ret = http_send_data(client_fd, buffer, bytes_read);
        if (ret < 0) { // write failed, client_fd has been closed
            printf("Failed to send response back to client\n");
            break;
        }
    }

    // Free resources and exit
    free(buffer);
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    close(fileserver_fd);
}

/*
 * worker thread function
 */
void *work_forever(void *arg) {
    int tid = ((worker_t *)arg)->tid;
    while (1) {
        if (verbose) printf("T%d waiting for work\n", tid);
        work_t *work = get_work(workqueue);
        if (work == NULL) {
            printf("T%d Error getting work\n", tid);
            continue;
        }
        if (verbose) printf("T%d dequeued a job and started work %d\n", tid, work->wid);
        update_thrpool(threadpool, tid, 1);

        // working ...
        if (work->delay > 0) sleep(work->delay);
        if (verbose) printf("T%d done sleeping for work %d\n", tid, work->wid);
        serve_request(work);

        printf("T%d completed work %d\n", tid, work->wid);
        update_thrpool(threadpool, tid, 0);

        // clean up this work request
        free(work->httpreq);
        free(work->path);
        free(work);
    }
    return NULL;
}

/*
 * print #active and #inactive threads in the threadpool
 */
void print_thrpool_status(thr_pool_t *thrpool) {
    pthread_mutex_lock(&thrpool->lock);
    printf("\n\tThreadpool status:\n\t%d active %d idle\n\t----\n", thrpool->n_active, thrpool->n_idle);
    pthread_mutex_unlock(&thrpool->lock);
}

/*
 * updates threadpool active thread count
 */
void update_thrpool(thr_pool_t *thrpool, int tid, int active) {
    pthread_mutex_lock(&thrpool->lock);
    if (active) {
        thrpool->n_active++;
        thrpool->n_idle--;
    } else {
        thrpool->n_active--;
        thrpool->n_idle++;
    }
    pthread_mutex_unlock(&thrpool->lock);
}

/*
 * Create a threadpool
 * Assuming this will run in a single-threaded env
 */
thr_pool_t *create_thrpool(int n_thr) {
    thr_pool_t *thrpool = (thr_pool_t *)malloc(sizeof(thr_pool_t));

    thrpool->threads = (pthread_t *)malloc(n_thr * sizeof(pthread_t));
    thrpool->n_threads = n_thr;
    thrpool->n_active = 0;
    thrpool->n_idle = thrpool->n_threads;
    for (int i = 0; i < thrpool->n_threads; i++) {
        worker_t *arg = (worker_t *)malloc(sizeof(worker_t));
        arg->tid = i;
        pthread_create(&thrpool->threads[i], NULL, work_forever, (void *)arg);
        if (verbose) printf("Creating worker %i\n", i);
    }
    pthread_mutex_init(&thrpool->lock, NULL);

    printf("Created a thread pool with %d threads\n", thrpool->n_threads);
    return thrpool;
}

void delete_thrpool(thr_pool_t *thrpool) {
    // not freed properly
    free(thrpool->threads);
    free(thrpool);
}

// ****************************************
// --------- Listener functions -----------
// ****************************************

void *listen_forever(void *args) {
    listener_t *listener = (listener_t *)args;
    int tid = listener->tid;
    int *listen_fd = listener->fd;

    // create a socket to listen
    *listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (*listen_fd == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(*listen_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(listener->port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(*listen_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(*listen_fd, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    if (verbose) printf("L%d listening on port %d (socket %d)...\n", tid, listener->port, *listen_fd);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    while (1) {
        if (verbose) {
            print_queue(workqueue);
            print_thrpool_status(threadpool);
        }
        if (verbose) printf("L%d waiting for client\n", tid);
        client_fd = accept(*listen_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        printf("L%d accepted connection from %s on port %d\n",
               tid, inet_ntoa(client_address.sin_addr),
               client_address.sin_port);

        work_t *work = parse_client_request(client_fd);
        /* if parsing error, return with errcode bad request */
        if (work == NULL) {
            perror("Error parsing client request");
            send_error_response(client_fd, BAD_REQUEST, "Error parsing client request");
            close(client_fd);
            continue;
        }

        printf("\tRequest path '%s'\n", work->path);
        /* if special request /GetJob, then dequeue and return the job */
        if (strcmp(GETJOBCMD, work->path) == 0) {
            int size;
            work_t *work = get_work_nonblocking(workqueue, &size);

            if (work == NULL) { // queue empty
                char buf[100];
                sprintf(buf, "Queue Empty (size %d)", size);
                send_error_response(client_fd, QUEUE_EMPTY, buf);
                close(client_fd);
                if (verbose) printf("\tSent error response %s\n", buf);

            } else { // removed the request
                char buf[256];
                sprintf(buf, "%s", work->path);
                send_error_response(client_fd, OK, buf);
                close(client_fd);
                printf("\tRemoved the highest priority job\n");
            }
            continue;
        }

        /* add the request to the queue, and move on */
        int size;
        int ret = add_work(workqueue, work, &size);
        if (ret == QFULL) {
            char buf[100];
            sprintf(buf, "Queue Full (size %d)", size);
            send_error_response(client_fd, QUEUE_FULL, buf);
            close(client_fd);
            if (verbose) printf("\tSent error response %s\n", buf);

        } else {
            printf("\tEnqueued the job\n");
        }
    }

    shutdown(*listen_fd, SHUT_RDWR);
    close(*listen_fd);
}

int start_listening_threads(int num_listener, int *listener_ports,
                            int *listener_fds) {

    pthread_t *listeners = malloc((num_listener - 1) * sizeof(pthread_t));

    int i = 0;
    for (; i < num_listener - 1; i++) {
        listener_t *listener = (listener_t *)malloc(sizeof(listener_t));
        listener->tid = i;
        listener->port = listener_ports[i];
        listener->fd = &listener_fds[i];
        pthread_create(&listeners[i], NULL, listen_forever, (void *)listener);
        if (verbose) printf("Creating listener %d\n", i);
    }

    listener_t *listener = (listener_t *)malloc(sizeof(listener_t));
    listener->tid = i;
    listener->port = listener_ports[i];
    listener->fd = &listener_fds[i];
    if (verbose) printf("Creating listener %d\n", i);
    listen_forever((void *)listener);
    // this should not return
    return -1;
}

// ****************************************
// -------- HTTP Request Parsing ----------
// ****************************************

work_t *parse_client_request(int fd) {
    work_t *work = malloc(sizeof(work_t));
    if (!work) http_fatal_error("Malloc failed");
    work->fd = fd;
    work->delay = -1;
    work->priority = -1;

    char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
    if (!read_buffer) http_fatal_error("Malloc failed");

    int bytes_read = read(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE);
    read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

    work->httpreq = strdup(read_buffer);

    int is_first = 1;
    size_t size;

    char *token = strtok(read_buffer, "\r\n");
    while (token != NULL) {
        size = strlen(token);
        if (is_first) {
            is_first = 0;
            // get path
            char *s1 = strstr(token, " ");
            char *s2 = strstr(s1 + 1, " ");
            size = s2 - s1 - 1;
            char *path = strndup(s1 + 1, size);

            work->path = path; // strdup(path);
            if (strcmp(GETJOBCMD, path) == 0) {
                free(read_buffer);
                return work;
            } else {
                // get priority
                s1 = strstr(path, "/");
                s2 = strstr(s1 + 1, "/");
                size = s2 - s1 - 1;
                char *p = strndup(s1 + 1, size);
                work->priority = atoi(p);
                free(p);
            }
        } else {
            char *value = strstr(token, ":");
            if (value) {
                size = value - token - 1; // -1 for space
                if (strncmp("Delay", token, size) == 0)
                    work->delay = atoi(value + 2); // skip `: `
            }
        }
        token = strtok(NULL, "\r\n");
    }

    if (verbose) {
        printf("\tParsed HTTP request: ");
        if (strcmp(work->path, GETJOBCMD) == 0) printf("\t%s", GETJOBCMD);
        printf("\tPriority: %d", work->priority);
        printf("\tDelay: %d\n", work->delay);
    }

    free(read_buffer);
    return work;
}

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    free(buf);
    return;
}

//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
// -----------SOLUTION FUNCTIONS END -------
//%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever() {
    workqueue = create_queue(max_queue_size);
    threadpool = create_thrpool(num_workers);

    listener_fds = (int *)malloc(num_listener * sizeof(int));
    start_listening_threads(num_listener, listener_ports, listener_fds);
}

/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 2;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;
    listener_ports[1] = 8001;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t  ---- Settings ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_listener);
    printf("\tFileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tMax queue size %d\n", max_queue_size);
    printf("\t  ----\t----\t----\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));

    for (int i = 0; i < num_listener; i++) {
        printf("Closing socket %d\n", listener_fds[i]);
        if (close(listener_fds[i]) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    free(listener_fds);
    delete_queue(workqueue);
    delete_thrpool(threadpool);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    free(listener_ports);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();

    serve_forever();

    return EXIT_SUCCESS;
}
