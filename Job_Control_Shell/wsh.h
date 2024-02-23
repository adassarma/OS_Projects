#ifndef WSH_H_
#define WSH_H_

#include <sys/types.h>

#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

#define PIPE_OK  0 /* cmdline is with pipes and correct */
#define PIPE_NO  1 /* cmdline is without pipes */
#define PIPE_BAD 2 /* cmdline is with pipes and incorrect */

#define CTRLC 130 /* sigterm termination exit code */

/**
 * Macro for memory allocation on stack.
 */
#define alloc_argv(argv) \
    do { \
        argv = alloca(sizeof(char*) * MAXARGS); \
        if (!argv) \
            app_error("Cannot allocate memory for command line arguments!\n"); \
        for (int i = 0; i < MAXARGS; i++) { \
            argv[i] = alloca(sizeof(char) * MAXLINE); \
            if (argv[i] == NULL) \
                app_error("Cannot allocate memory for command line arguments!\n"); \
        } \
    } while (0)

char prompt[] = "> ";       /* prompt */
int last_exit = 0;          /* last exited process exit status */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */
char cwd[MAXLINE];          /* current working directory */
int want_exit = 0;          /* flag signalling that user wants to exit and
                               background jobs are running */

struct job_t {              /* The job struct */
    pid_t pgid;             /* job PGID */
    pid_t last_pid;         /* last process' in the job pid */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};

struct job_t jobs[MAXJOBS]; /* The job list */

/**
 * Functions declarations, see .c file for description
 */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
void app_error(char *msg);
int deletejob(struct job_t *jobs, pid_t pid); 
int builtin_cmd(char **argv, int argc);
void clearjob(struct job_t *job);
typedef void handler_t(int);
int check_pipes(char **argv, int argc);
void do_bgfg(char **argv, int argc);
void eval(char *cmdline);
pid_t fgpid(struct job_t *jobs);
pid_t get_last_pid(struct job_t *jobs, int pgid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
struct job_t *getjobpgid(struct job_t *jobs, pid_t pgid);
void initjobs(struct job_t *jobs);
void listjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int maxpid(struct job_t *jobs); 
int parseline(const char *cmdline, char **argv, int *cmdline_len); 
int pid2jid(pid_t pid); 
void quit();
void set_last_pid(struct job_t *jobs, int pgid, int pid);
handler_t *set_signal_handler(int signum, handler_t *handler);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
void sigtstp_handler(int sig);
void unix_error(char *msg);
void usage(void);
void waitfg(pid_t pid, int fd);

#endif

