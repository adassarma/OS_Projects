#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "wsh.h"

/**
 * Function for checking validity of parameters
 */
void check_params(char **argv, int argc)
{
    if (argc != 1) {
        printf("Usage: %s\n", argv[0]);
        printf("No arguments allowed.\n");
        exit(EXIT_FAILURE);
    }
}

/**
 * Main
 */
int main(int argc, char **argv) 
{
    check_params(argv, argc);

    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    set_signal_handler(SIGCHLD, sigchld_handler);
    set_signal_handler(SIGQUIT, sigquit_handler); 

    initjobs(jobs);
    getcwd(cwd, MAXLINE);
    while (1) {
        printf("[%d] (%s) %s", last_exit, cwd, prompt);
        fflush(stdout);

        char cmdline[MAXLINE];
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) {
            quit();
            clearerr(stdin);
            continue;
        }

        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }
}

/**
 * Parsing arguments into the form viable for piped execution.
 * margs[N] contains array of arguments for N-th process in the pipe.
 *
 * @param margs Allocated memory for string pointers
 * @param argv Commandline in argv style
 * @param argc Number of arguments
 *
 * @return Number of processes in the pipe command
 */
int parse_margs(char *margs[][MAXARGS], char **argv, int argc)
{
    int i,j,k;
    for (i = 0, j = 0, k = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            j = 0;
            k++;
            continue;
        }
        margs[k][j++] = argv[i];
        margs[k][j] = NULL;
    }
    return k+1;
}

/**
 * Close all descriptors withou error checking.
 *
 * @param pipefds Array of tupples of descriptors for closing
 * @param proc_count Number of processes in the pipe
 */
void close_all_fds(int pipefds[][2], int proc_count)
{
    for (int i = 0; i < proc_count; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }
}

/**
 * Command line evaluation
 *
 * @param cmdline Parsed commandline.
 */
void eval(char *cmdline) 
{
    /* first part of parsing, especially tokenizing */
    char **argv;
    alloc_argv(argv);
    int argc;
    int bg = parseline(cmdline, argv, &argc);
    if (argc == 0)
        return;
    if (builtin_cmd(argv, argc))
        return;

    /* second part for checking command line for pipes validity */
    int pipe_check = check_pipes(argv, argc);

    if (pipe_check == PIPE_BAD) {
        printf("We allow only space separated pipes!\n");
        return;
    }

    /* parsing into array of arguments per each process in the job */
    char *margs[MAXARGS][MAXARGS];
    int proc_count = parse_margs(margs, argv, argc);
    int pipes_present = (pipe_check == PIPE_OK);

    /* setting up file descriptos */
    int pipefds[proc_count][2];
    if (pipes_present)
        for (int i = 0; i < proc_count; i++)
            if (pipe(pipefds[i]) == -1)
                unix_error("pipe");

    /* variables used in the fork/exec logic */
    int pid;
    int pgid = -1;
    int stdindup = dup(0);
    for (int i = 0; i < proc_count; i++) {
        if ((pid = fork()) == -1)
            unix_error("fork");

        if (pid == 0) {
            /* set signals to their defalt state in the child */
            signal(SIGINT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGQUIT, SIG_DFL); 

            /* set pgid of the whole job to the pgid(=pid) of the first process */
            if (pgid == -1)
                pgid = getpid();

            /* setup inputs/outputs to pipes if piped job*/
            if (pipes_present) {
                if (i > 0)
                    if (dup2(pipefds[i-1][0], 0) == -1)
                        perror("dup2");

                if (i < proc_count - 1)
                    if (dup2(pipefds[i][1], 1) == -1)
                        perror("dup2");

                close_all_fds(pipefds, proc_count);
            }

            /* set correct groupid and exec */
            if (setpgid(0, pgid) != 0)
                perror("setpgid");
            if (execvp(margs[i][0], margs[i]) == -1)
                unix_error("execvp");
        } else {
            signal(SIGTTOU, SIG_IGN);
            /* get group id of the whole job */
            if (pgid == -1) {
                pgid = pid;
                int status = bg ? BG : FG;
                addjob(jobs, pgid, status, cmdline);
            }

            /* set group id of the process */
            if (setpgid(pid, pgid) != 0)
                perror("setpgid");
        }
    }
    set_last_pid(jobs, pgid, pid);
    if (pipes_present)
        close_all_fds(pipefds, proc_count);
    if (!bg)
        waitfg(pid, stdindup);
}

/**
 * Sanity check for pipes in cmdline
 *
 * @param argv Array of args
 * @param argc Args count
 *
 * @return PIPE_OK if pipes are in the cmdline and are correct, PIPE_BAD if
 * contained and incorrect, PIPE_NO if no pipes
 */
int check_pipes(char **argv, int argc)
{
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "|") != 0 && strchr(argv[i], '|') != NULL)
            return PIPE_BAD;

    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "|") == 0)
            return PIPE_OK;

    return PIPE_NO;
}

/**
 * Skeleton function used for tokenizing the input
 *
 * @param cmdline Command line input
 * @param argv Allocated space for parsed values in arg style
 * @param argc Return variable for argc
 *
 * @return BG if background job
 */
int parseline(const char *cmdline, char **argv, int *cmdline_len)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */

    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) { /* ignore spaces */
            buf++;
        }

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0) {  /* ignore blank line */
        *cmdline_len = argc;
        return 1;
    }

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    *cmdline_len = argc;
    return bg;
}

/**
 * Function for running shell builtins
 *
 * @param argv Arguments
 * @param argc Argument count
 *
 * @return 1 if it was builting command, 0 otherwise
 */
int builtin_cmd(char **argv, int argc)
{
    int ret = 1;
    if (strcmp(argv[0], "exit") == 0) {
        quit();
        return ret;
    }
    else if (strcmp(argv[0], "jobs") == 0)
        listjobs(jobs);
    else if (strcmp(argv[0], "cd") == 0) {
        if (chdir(argv[1]) != 0)
            perror("chdir");
        else {
            setenv("OLDPWD", cwd, 1);
            getcwd(cwd, MAXLINE);
            setenv("PWD", cwd, 1);
        }
    } else if (strcmp(argv[0], "fg") == 0 || strcmp(argv[0], "bg") == 0)
        do_bgfg(argv, argc);
    else
        ret = 0;

    want_exit = 0;
    return ret;
}

/**
 * Handle bg/fg commands
 *
 * @param argv Arguments
 * @param argc Argument count
 */
void do_bgfg(char **argv, int argc) 
{
    int jid;
    if (argc == 2)
        jid = atoi(argv[1]);
    else
        jid = maxjid(jobs);

    struct job_t *j = getjobjid(jobs, jid);
    if (j == NULL) {
        printf("Nope, this is not a valid PID!\n");
        return;
    }

    if (strcmp(argv[0], "fg") == 0) {
        if (j->state == ST)
            kill(-j->pgid, SIGCONT);
        j->state = FG;
        waitfg(j->last_pid, 0);
    } else if (strcmp(argv[0], "bg") == 0) {
        if (j->state == BG)
            return;
        kill(-j->pgid, SIGCONT);
        j->state = BG;
    }
}

/**
 * Function will block until pid is no longer in foreround
 *
 * @param pid pid of process we are waiting for
 * @param stdindup File descriptor of controling terminal
 */
void waitfg(pid_t pid, int stdindup)
{
WAIT:
    tcsetpgrp(stdindup, getpgid(pid));
    int wstatus;
    int terminated_pid = waitpid(pid, &wstatus, WUNTRACED);
    if (terminated_pid == -1)
        perror("waitpid");
    if (WIFSTOPPED(wstatus)) {
        struct job_t *j = getjobpgid(jobs, getpgid(terminated_pid));
        j->state = ST;
    } else if (WIFEXITED(wstatus)) {
        deletejob(jobs, pid);
        //printf("Exit status of program %u is %d\n", pid, WEXITSTATUS(wstatus));
        last_exit = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        deletejob(jobs, pid);
        //printf("Exit status of program %u is %d\n", pid, CTRLC);
        last_exit = CTRLC;
    } else
        goto WAIT;
    tcsetpgrp(stdindup, getpid());
}

/**
 * SIGCHLD handler
 */
void sigchld_handler(int __attribute__((unused)) sig) 
{
    int wstatus;
    int pid;
WAIT:
    while ((pid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
        if (WIFEXITED(wstatus)) {
            tcsetpgrp(2, getpgid(getpid()));
            deletejob(jobs, pid);
            //printf("Exit status of background program %u is %d\n", pid, WEXITSTATUS(wstatus));
        } else
            goto WAIT;
    }
}

/**
 * Clear job entries
 */
void clearjob(struct job_t *job) {
    job->pgid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/**
 * Initialize job list
 */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/**
 * @return largest allocated jid
 */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/**
 * @return Largest pgid
 */
int maxpgid(struct job_t *jobs) 
{
    int max = 0;
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid > max)
            max = jobs[i].pgid;
    return max;
}

/**
 * @return 1 if there are running jobs, 0 otherwise
 */
int any_job_running()
{
    if (maxpgid(jobs) > 0)
        return 1;
    return 0;
}
/**
 * Add job to the list
 *
 * @param pid pid of process
 * @param state FG or BG
 * @param cmdline Commandline
 */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    if (pid < 1)
        return 0;

    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pgid == 0) {
            jobs[i].pgid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strncpy(jobs[i].cmdline, cmdline, strlen(cmdline)-1);
            char *c = strstr(jobs[i].cmdline, " &");
            if (c != NULL)
                *c = '\0';
            return i;
        }
    }
    printf("Tried to create too many jobs\n");
    return -1;
}

/**
 * Delete job from job list
 *
 * @param pid pid of deleted process
 *
 * @return 1 if OK 0 otherwise
 */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    if (pid < 1)
        return 0;

    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].last_pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

/**
 * @param pgid pgid of the job
 *
 * @return PID of last process in the piped job
 */
pid_t get_last_pid(struct job_t *jobs, int pgid)
{
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid == pgid)
            return jobs[i].last_pid;
    return 0;
}

/**
 * @param pgid pgid of the job
 * @param pid pid of the last process in the job
 */
void set_last_pid(struct job_t *jobs, int pgid, int pid)
{
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid == pgid)
            jobs[i].last_pid = pid;
}

/**
 * @param pgid pgid of the job we are looking for
 *
 * @return job from corresponding pgid
 */
struct job_t *getjobpgid(struct job_t *jobs, pid_t pgid) {
    if (pgid < 1)
        return NULL;
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid == pgid)
            return &jobs[i];
    return NULL;
}


/**
 * @param jid jid of the job we are looking for
 *
 * @return job structure of corresponding jid
 */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    if (jid < 1)
        return NULL;
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/*
 * printout jobs list
 */
void listjobs(struct job_t *jobs) 
{
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid != 0)
            printf("%d: %s\n", jobs[i].jid, jobs[i].cmdline);
}

/*
 * syscall error handling
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * application error handling
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * set signal handlers
 */
handler_t *set_signal_handler(int signum, handler_t *handler) 
{
    struct sigaction action = { .sa_handler = handler, .sa_flags = SA_RESTART };
    sigemptyset(&action.sa_mask);

    struct sigaction old_action;
    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * handler for sigquit
 */
void sigquit_handler(int __attribute__((unused)) sig) 
{
    quit();
}

/**
 * Quiting the shell nicely with asking about background jobs
 */
void quit()
{
    if (any_job_running() && !want_exit) {
        printf("\nThere are running background jobs! If you really want to quit, repeat your command!\n");
        fflush(stdout);
        want_exit = 1;
        return;
    }
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
