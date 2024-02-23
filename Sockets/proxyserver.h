#ifndef PROXYSERVER_H
#define PROXYSERVER_H

#include "safepqueue.h"

typedef enum scode {
    OK = 200,           // ok
    BAD_REQUEST = 400,  // bad request
    BAD_GATEWAY = 502,  // bad gateway
    SERVER_ERROR = 500, // internal server error
    QUEUE_FULL = 599,   // priority queue is full
    QUEUE_EMPTY = 598   // priority queue is empty
} status_code_t;

// *****************************************
// -----------SOLUTION FUNCTIONS -----------
// *****************************************

#define GETJOBCMD "/GetJob"

typedef struct {
    pthread_mutex_t lock;
    pthread_t *threads;
    int n_threads;
    int n_active;
    int n_idle;
} thr_pool_t;

typedef struct {
    int tid;
} worker_t;

void print_thrpool_status(thr_pool_t *thrpool);
thr_pool_t *create_thrpool(int n_thr);
void update_thrpool(thr_pool_t *thrpool, int tid, int active);

typedef struct {
    int tid;
    int port;
    int *fd;
} listener_t;

work_t *parse_client_request(int fd);

void send_error_response(int client_fd, status_code_t err_code, char *err_msg);

void print_settings();

// *****************************************
// -----------SOLUTION FUNCTIONS END -------
// *****************************************

/*
 * A simple HTTP library.
 *
 * Usage example:
 *
 *     // Returns NULL if an error was encountered.
 *     struct http_request *request = http_request_parse(fd);
 *
 *     ...
 *
 *     http_start_response(fd, 200);
 *     http_send_header(fd, "Content-type", http_get_mime_type("index.html"));
 *     http_send_header(fd, "Server", "httpserver/1.0");
 *     http_end_headers(fd);
 *     http_send_string(fd, "<html><body><a href='/'>Home</a></body></html>");
 *
 *     close(fd);
 */

#define LIBHTTP_REQUEST_MAX_SIZE 8192

/*
 * Functions for parsing an HTTP request.
 */
struct http_request {
    char *method;
    char *path;
    char *delay;
};

/*
 * Functions for sending an HTTP response.
 */
void http_start_response(int fd, int status_code);
void http_send_header(int fd, char *key, char *value);
void http_end_headers(int fd);
void http_send_string(int fd, char *data);
int http_send_data(int fd, char *data, size_t size);
char *http_get_response_message(int status_code);

void http_start_response(int fd, int status_code) {
    dprintf(fd, "HTTP/1.0 %d %s\r\n", status_code,
            http_get_response_message(status_code));
}

void http_send_header(int fd, char *key, char *value) {
    dprintf(fd, "%s: %s\r\n", key, value);
}

void http_end_headers(int fd) {
    dprintf(fd, "\r\n");
}

void http_send_string(int fd, char *data) {
    http_send_data(fd, data, strlen(data));
}

int http_send_data(int fd, char *data, size_t size) {
    ssize_t bytes_sent;
    while (size > 0) {
        bytes_sent = write(fd, data, size);
        if (bytes_sent < 0)
            return -1; // Indicates a failure
        size -= bytes_sent;
        data += bytes_sent;
    }
    return 0; // Indicate success
}

void http_fatal_error(char *message) {
    fprintf(stderr, "%s\n", message);
    exit(ENOBUFS);
}

struct http_request *http_request_parse(int fd) {
    struct http_request *request = malloc(sizeof(struct http_request));
    if (!request) http_fatal_error("Malloc failed");

    char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
    if (!read_buffer) http_fatal_error("Malloc failed");

    int bytes_read = read(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE);
    read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

    char *read_start, *read_end;
    size_t read_size;

    do {
        /* Read in the HTTP method: "[A-Z]*" */
        read_start = read_end = read_buffer;
        while (*read_end >= 'A' && *read_end <= 'Z') {
            printf("%c", *read_end);
            read_end++;
        }
        read_size = read_end - read_start;
        if (read_size == 0) break;
        request->method = malloc(read_size + 1);
        memcpy(request->method, read_start, read_size);
        request->method[read_size] = '\0';
        printf("parsed method %s\n", request->method);

        /* Read in a space character. */
        read_start = read_end;
        if (*read_end != ' ') break;
        read_end++;

        /* Read in the path: "[^ \n]*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0) break;
        request->path = malloc(read_size + 1);
        memcpy(request->path, read_start, read_size);
        request->path[read_size] = '\0';
        printf("parsed path %s\n", request->path);

        /* Read in HTTP version and rest of request line: ".*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != '\n')
            read_end++;
        if (*read_end != '\n') break;
        read_end++;

        free(read_buffer);
        return request;
    } while (0);

    /* An error occurred. */
    free(request);
    free(read_buffer);
    return NULL;
}

char *http_get_response_message(int status_code) {
    switch (status_code) {
    case 100:
        return "Continue";
    case 200:
        return "OK";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    default:
        return "Internal Server Error";
    }
}

#endif
