/* 
 * tsh - A tiny shell program with job control
 * 
 * <Put your name and login ID here>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>



/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "CaiShell> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

char PATH[MAXARGS][MAXLINE];

struct alias_t {
    char new_command[MAXLINE];
    char old_command[MAXLINE];
    struct alias_t *next;
};

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
struct alias_t *alias_p = NULL;
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void init(void);

int myStrchr(char *p, char ch);

void eval(char *cmdline);

int builtin_cmd(char **argv);

void do_bgfg(char **argv);

void alias_add(char **argv);

void alias_free(void);

void rebulid_command(char ** argv);

void is_accessable(char **argv);

void waitfg(pid_t pid);

void sigchld_handler(int sig);

void sigtstp_handler(int sig);

void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);

void sigquit_handler(int sig);

void clearjob(struct job_t *job);

void initjobs(struct job_t *jobs);

int maxjid(struct job_t *jobs);

int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);

int deletejob(struct job_t *jobs, pid_t pid);

pid_t fgpid(struct job_t *jobs);

struct job_t *getjobpid(struct job_t *jobs, pid_t pid);

struct job_t *getjobjid(struct job_t *jobs, int jid);

int pid2jid(pid_t pid);

void listjobs(struct job_t *jobs);

void usage(void);

void unix_error(char *msg);

void app_error(char *msg);

typedef void handler_t(int);

handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE + 1];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);
    at_exit(alias_free);  /* set the free when exit */

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }


    /* Initialize the environment*/
    init();
    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE + 1, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (strlen(cmdline) == MAXLINE + 1)
            app_error("too long command");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
*	find the index of the ch in the string
*/
int myStrchr(char *p, char ch) {
    int index = 0;
    int flag = 0;
    while (ch != p[index] && p[index] != NULL) {
        index++;
    }
    if (ch == p[index])
        return index;
    else
        return -1;
}

/*
 * initialize the environment of PATH
 */
void init(void) {
    char EnviromentPATH = "myconf";
    char bashrcLine[MAXLINE], *buff, *delim;
    int argc, index;

    memset(PATH, 0, sizeof(char) * MAXLINE * MAXARGS);

    FIFE *file = fopen(EnviromentPATH, "r");
    if (file == NULL)
        fprintf(stdout, "Fail to initialize the environment PATH");


    while (fgets(bashrcLine, MAXLINE, file)) {
        /* Find the key word PATH*/
        buff = bashrcLine;
        while (*buff && (*buff == ' ')) buff++;/* ignore leading spaces */
        char *p = strpbrk(buff, "export");
        if (p != NULL) {
            buff = p + 6;
            while (*buff && (*buff == ' ')) buff++;
        }

        if (buff[0] != 'P' || buff = strpbrk(buff, "PATH=") == NULL) continue;

        /* Fill the PATH array*/
        buff = buff + 6;
        index = myStrchr(buf, ':');
        argc = 0;
        while (index != -1) {
            if (index != 0)
                strncpy(PATH[argc++], buff, index);
            buff = buff + index + 1;
            //while (*buf && (*buf == ' ')) /* ignore spaces */
            //	   buf++;
            index = myStrchr(buf, ':');
        }
        //index = myStrchr(PATH[argc-1],'\"');
        //PATH[argc-1][index] = NULL;
    }

    if (PATH[0][0] == NULL)
        fprintf(stdout, "Fail to initialize the environment PATH");

    fflush(stdout);
}


/*
* use the alias list to rebulid the command
*/
void rebulid_command(char **argv)
{
	alias_t p = alias_p;
	int argc = 0;
	while(argv[argc] !=NULL) argc++;
    while (p != NULL) {
        if (!strcmp(p->new_command, argv[0])) {
           /* Build the argv list */
			static char buf[MAXLINE];
			stpcpy(buf,p->old_command);

			delim = strchr(buf, ' ');
			argv[0] = buf;
			*delim = '\0';
			buf = delim + 1;
			delim = strchr(buf, ' ');
			while (delim) {
				argv[argc++] = buf;
				*delim = '\0';
				buf = delim + 1;
				while (*buf && (*buf == ' ')) /* ignore spaces */
					buf++;
				delim = strchr(buf, ' ');
			}
			argv[argc] = NULL;
        } else
            p = p->next;
    }
	return ;
}

/*
* if the file executable
*/
int is_accessable(char **argv)
{
	if(access(argv[0],X_OK)!=-1)   
		return 1; 
	static char arg0[MAXLINE];
	
	for(int i=0; i<MAXARGS && PATH[i] != NULL; i++){
		strcpy(arg0,PATH[i]);
		int len = strlen(arg0);
		arg0[len] = '/';
		strcpy(&arg0[len+1],argv[0]);
		if(access(argv0,X_OK)!=-1)
		{
			argv[0] = arg0;
			return 1; 
		}
	}
	
	return 0;
	
}


/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) {
    char *argv[MAXARGS];
    int bg;
    bg = parseline(cmdline, argv);
    if (argv[0] == NULL) {
        return; /* Ignore empty lines */
    }
	rebulid_command(argv);
    if (!builtin_cmd(argv)) /* built-in command */
        /* program (file) */
    {
        if (!is_accessable(argv)) /* do not fork and addset! This process is much better.*/
        {
            fprintf(stderr, "%s: Command not found\n", argv[0]);
            return;
        }

        pid_t pid;
        sigset_t mask, prev;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_SETMASK, &mask, &prev); /* block SIG_CHLD */

        if ((pid = fork()) == 0) /* child */
        {
            sigprocmask(SIG_UNBLOCK, &prev, NULL);

            if (!setpgid(0, 0)) {
				if(execve(argv[0], argv, environ))
					fprintf(stderr, "%s: Failed to execve\n", argv[0]);
                /* context changed */
            } else {
                unix_error("Failed to invoke setpgid(0, 0)");
            }
        } else {
            /* Parent process */
            addjob(jobs, pid, (bg) ? BG : FG, cmdline);
            sigprocmask(SIG_SETMASK, &prev, NULL);
            if (!bg) {
                waitfg(pid);
            } else {
                printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
            }
        }

    }

    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE + 1]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    } else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        } else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    if (argc == 0)  /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0) {
        argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) {

    if (!strcmp(argv[0], "quit")) {
        exit(0);
    } else if (!strcmp(argv[0], "jobs")) {
        listjobs(jobs);
        return 1;
    } else if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
        do_bgfg(argv);
        return 1;
    } else if (!strcmp(argv[0], "alias")) {
        alias_add(argv);
        return 1;
    }

    return 0;     /* not a builtin command */
}

/* 
 * alias_add - add the rename command
 */
void alias_add(char **argv) {
    struct alias_t *p;
    char *delim;

    if (argv[4] != NULL) {
        printf("Error command of alias");
        return;
    }

    if (argv[3] == NULL && delim = strchr(argv[2], '=') && (delim + 1) != '\'') {
        printf("Error command of alias");
        return;
    } else {
        delim += 2;
        argv[3] = delim;
        delim = strchr(argv[3], '\'');
        *delim = NULL;
    }

    if (argv[2] == NULL && delim = strchr(argv[1], '=') && (delim + 1) != '\'') {
        printf("Error command of alias");
        return;
    } else {

        *delim = NULL;
        delim += 2;
        argv[3] = delim;
        delim = strchr(argv[3], '\'');
        *delim = NULL;

    }
	p = alias_p;
    while (p != NULL) {
        if (!strcmp(p->new_command, argv[1])) {

            strcpy(p->old_command, argv[3]);

            return;
        } else
            p = p->next;
    }

    p = (alias_t *) malloc(sizeof(struct alias_t));

    strcpy(p->new_command, argv[1]);
    strcpy(p->old_command, argv[3]);


    p->next = alias_p;
    alias_p = p;
    return;
}

/*
 * alias_free - free the memory of all rename command
 */
void alias_free(void) {
    while (alias_p != NULL) {
        alias_t *p = alias_p->next;
        free(p);
    }
    return;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {

    struct job_t *job;


    /* have no argument */
    if (argv[1] == NULL) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }

    /* job id */
    if (argv[1][0] == '%') {
        int jid = atoi(&argv[1][1]);
        job = getjobjid(jobs, jid);
        if (job == NULL) {
            printf("%%%d: no such job\n", jid);
            return;
        }
    } else if (isdigit(argv[1][0])) {//pid
        int pid = atoi(argv[1]);
        job = getjobpid(jobs, pid);
        if (job == NULL) {
            printf("(%d): no such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    kill(-(job->pid), SIGCONT);

    if (!strcmp(argv[0], "bg")) {
        job->state = BG;
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    } else {
        job->state = FG;
        waitfg(job->pid);
    }
    return;

}

/* 
 * waitfg - Block until process pid is no longer the foreground process 不推荐使用 waitpid 函数
 */
void waitfg(pid_t pid) {
    if (pid == 0) {
        return;
    }
    while (pid == fgpid(jobs)) {
        sleep(1);
    }
    return;

}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) {

    int olderrno = errno, status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { //WNOHANG不打算阻塞等待子进程返回时，可以这样使用。
        // printf("1");
        if (WIFEXITED(status)) /* returns true if the child terminated normally */
        {
            deletejob(jobs, pid);
        } else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
            deletejob(jobs, pid);
        } else if (WIFSTOPPED(status)) {
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
            struct job_t *job = getjobpid(jobs, pid);
            if (job != NULL) {
                job->state = ST;
            }
        }
        if (errno != ECHILD) {
            unix_error("waitpid error\n");
        }
    }

    errno = olderrno;
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) {
    int olderrno = errno;

    pid_t pid = fgpid(jobs);
    if (pid) {
        //printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));不行，因为会输出在子进程,而且子进程execve后不存在现在的变量了
        //deletejob(jobs,Localpid); //可以试试

        kill(-pid, sig);
    }

    errno = olderrno;
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) {
    pid_t pid = fgpid(jobs);
    if (pid != 0) {
        struct job_t *job = getjobpid(jobs, pid);
        if (job->state == ST) {
            return;
        } else {
            // job->state = ST;
            // printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));不行，因为会输出在子进程,而且子进程execve后不存在现在的变量了
            kill(-pid, sig);
        }
    }

    return;

}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose) {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG:
                    printf("Running ");
                    break;
                case FG:
                    printf("Foreground ");
                    break;
                case ST:
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ",
                           i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}




