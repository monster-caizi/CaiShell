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
#include <fcntl.h>


/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJOBPS      16   /* max jobs at any point in time */
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

pid_t Fpgid;

char PATH[MAXARGS][MAXLINE];

struct alias_t {
    char new_command[MAXLINE];
    char old_command[MAXLINE];
    struct alias_t *next;
};

struct job_t {              /* The job struct */
    pid_t pid[MAXJOBPS];              /* job PID */
    pid_t pgid;                /* job group pid */
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

int is_pipe(char **argv);

int is_pipe2(char **argv, char **argv1, int *index);

int builtin_cmd(char **argv);

void do_bgfg(char **argv);

void alias_add(char **argv);

void alias_free(void);

void rebulid_command(char **argv);

int is_accessable(char **argv);

void waitfg(pid_t pid);

void sigchld_handler(int sig);

void sigtstp_handler(int sig);

void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);

void sigquit_handler(int sig);

void clearjob(struct job_t *job);

void initjobs(void);

int maxjid(void);

int addjob(pid_t pid, pid_t pgid, int state, char *cmdline);

int deletejob(pid_t pid);

pid_t fgpgid(void);

struct job_t *getjobpid(pid_t pid);

struct job_t *getjobjid(int jid);

int pid2jid(pid_t pid);

void listjobs(void);

void usage(void);

void unix_error(char *msg);

void app_error(char *msg);

typedef void handler_t(int);

handler_t *Signal(int signum, handler_t *handler);



/*
Note by caizi
在实现Shell的过程中，发现很多其他很多人的做法存在了概念混淆，纯粹的模拟输出，
却没有将shell中的前台进程组真正的设置为前台进程组，而是利用shell来转发信号，
但是这样的存在一个严重的问题，即他们的子进程所在的新组都无法获得控制终端，即
无法从终端读取输入。

但是本人是实现中，存在一个暂时无法解决的问题，即如何将一个转让出去的控制终端
在前台进程组结束的时候重新获得，因为子进程执行了exev。

在此后的版本中需要尝试解决这个问题。

*/



/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE + 1];
    int emit_prompt = 1; /* emit prompt (default) */
	int pid,ffd;
    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);
    atexit(alias_free);  /* set the free when exit */

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

    /* Create a new section */
    if ((pid = fork()) < 0)
        app_error("Create Shell failed!");
    else if( pid != 0) 
        exit(0);
	
    setsid();

	if((ffd = open("/dev/tty",O_RDWR))<0)
	{
		app_error("open the terminal fail");
	}
    /* Initialize the environment*/
    init();
    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, SIG_IGN);   /* ctrl-c */
    Signal(SIGTSTP, SIG_IGN);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, SIG_IGN);

    /* Initialize the job list */
    initjobs();

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
    while (ch != p[index] && p[index] != '\0') {
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
    char *EnviromentPATH = "myconf";
    char bashrcLine[MAXLINE], *buf, *delim;
    int argc, index;

    memset(PATH, 0, sizeof(char) * MAXLINE * MAXARGS);

    FILE *file = fopen(EnviromentPATH, "r");
    if (file == NULL)
        fprintf(stdout, "Fail to initialize the environment PATH!\n");

    argc = 0;
    while (fgets(bashrcLine, MAXLINE, file)) {
        /* Find the key word PATH*/
        buf = bashrcLine;
        while (*buf && (*buf == ' ')) buf++;/* ignore leading spaces */
        char *p = strstr(buf, "export");
        if (p != NULL) {
            buf = p + 6;
            while (*buf && (*buf == ' ')) buf++;
        }

        if (buf[0] != 'P' || (buf = strstr(buf, "PATH=")) == NULL) continue;

        /* Fill the PATH array*/
        buf = buf + 5;
        index = myStrchr(buf, ':');

        while (index != -1) {
            if (index != 0)
                strncpy(PATH[argc++], buf, index);
            buf = buf + index + 1;
            //while (*buf && (*buf == ' ')) /* ignore spaces */
            //	   buf++;
            index = myStrchr(buf, ':');
        }
        if (*buf != '\0')
            strncpy(PATH[argc++], buf, strlen(buf) - 1);
        //index = myStrchr(PATH[argc-1],'\"');
        //PATH[argc-1][index] = NULL;
    }

    /* test the PATH
    int i = 0;
    while (PATH[i][0] != '\0')
    {
        fprintf(stdout, "%s/:",PATH[i]);

        i++;
    }*/

    if (PATH[0][0] == '\0')
        fprintf(stdout, "Fail to initialize the environment PATH!\n");


    fflush(stdout);
}

/*
 *  judge if the cmdline contain pipe
 */
int is_pipe(char **argv) {
    for (int i = 0; argv[i] != NULL; i++)
        if (!strcmp(argv[i], "|"))
            return 1;

    return 0;
}

/*
* use the alias list to rebulid the command
*/
void rebulid_command(char **argv) {
    struct alias_t *p;
    int argc = 0, argcM = 0;
    char *delim;
    static char array[MAXLINE];

    while (argv[argc] != NULL) argcM++;
    while (argv[argc] != NULL || strcmp(argv[argc], "|")) argc++;

    p = alias_p;
    while (p != NULL) {
        if (!strcmp(p->new_command, argv[0])) {
            /* Build the argv list */
            char *buf = array;
            stpcpy(buf, p->old_command);

            buf[strlen(buf)] = ' ';

            while (*buf && (*buf == ' ')) /* ignore leading spaces */
                buf++;

            delim = strchr(buf, ' ');
            argv[0] = buf;
            *delim = '\0';
            buf = delim + 1;
            while (*buf && (*buf == ' ')) /* ignore leading spaces */
                buf++;
            delim = strchr(buf, ' ');
            while (delim) {
                if (argc == argcM) {
                    argv[argc++] = buf;
                    argcM++;
                } else {
                    for (int temp = argcM; temp >= argc; temp--)
                        argv[temp + 1] = argv[temp];

                    argcM++;
                    argv[argc++] = buf;
                }

                *delim = '\0';
                buf = delim + 1;
                while (*buf && (*buf == ' ')) /* ignore spaces */
                    buf++;
                delim = strchr(buf, ' ');
            }
            if (argc == argcM)
                argv[argc] = NULL;

            break;

        } else
            p = p->next;
    }

    if (argc != argcM)
        rebulid_command(&argv[argc]);
    return;
}

/*
* if the file executable
*/
int is_accessable(char **argv) {
    int argc = 0;
    static char argv0[MAXLINE];

    if (access(argv[0], X_OK) != -1 && argv[0][0] == '.' && argv[0][1] == '/') {
        while (argv[argc] != NULL || strcmp(argv[argc], "|"))
            argc++;
        if (argv[argc] != NULL)
            return is_accessable(&argv[argc + 1]);
        else
            return 1;
    }

    for (int i = 0; i < MAXARGS && PATH[i][0] != '\0'; i++) {
        strcpy(argv0, PATH[i]);
        int len = strlen(argv0);
        argv0[len] = '/';
        strcpy(&argv0[len + 1], argv[0]);
        if (access(argv0, X_OK) != -1) {
            argv[0] = argv0;
            while (argv[argc] != NULL || strcmp(argv[argc], "|"))
                argc++;
            if (argv[argc] != NULL)
                return is_accessable(&argv[argc + 1]);
            else
                return 1;
        }
    }
    fprintf(stderr, "%s: Command not found\n", argv[0]);
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
    char *argv[MAXARGS], *argv1[MAXARGS];
    int bg, flag, index = 0;
	int pgid = 0;
    bg = parseline(cmdline, argv);
    if (argv[0] == NULL) {
        return; /* Ignore empty lines */
    }

    rebulid_command(argv);

    if (!(flag = builtin_cmd(argv))) /* built-in command */
        /* program (file) */
    {
        if (flag == -1) {
            fprintf(stderr, " Wrong pipe command\n");
            return;
        }
        if (!is_accessable(argv)) /* do not fork and addset! This process is much better.*/
            return;

        pid_t pid;
        sigset_t mask, prev;
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_SETMASK, &mask, &prev); /* block SIG_CHLD */
		
		/*if ((pid = fork()) == 0) // child 
		{
			Signal(SIGCHLD, SIG_IG);  // Terminated or stopped child 
			
			if (setpgid(0, 0)) 
				unix_error("Failed to invoke setpgid(0, 0)");
			
			int state;
			while(wait(&state)){};

			tcsetpgrp(0, getppid()); //set the group as the frount group
			exit(0);
		} else{
			pgid= pid;
		}*/
		
		if(is_pipe(argv))
		{
			int pipe_in;
			while(flag = is_pipe2(&argv[index],argv1,&index))
			{
				if(flag == 1)
				{
					int fd1[2];
					if(pipe(fd1)<0)
					{
						app_error("pipe error");
					}
					
					if ((pid = fork()) == 0) /* child */
					{
						
						close(fd1[0]);
						if(fd1[1] != STDOUT_FILENO)
						{
							if(dup2(fd1[1],STDOUT_FILENO) != STDOUT_FILENO)
								app_error("dup2 error to stdout");
							close(fd1[1]);
						}
						
						sigprocmask(SIG_UNBLOCK, &prev, NULL);

						if (!setpgid(0, pgid)) {
							
							if (execve(argv1[0], argv1, environ))
								fprintf(stderr, "%s: Failed to execve\n", argv[0]);
							/* context changed */
						} else 
							unix_error("Failed to invoke setpgid(0, 0)");
					} else {
						/* Parent process */
					
						dup2(fd1[0],pipe_in);
						close(fd1[0]);
						close(fd1[1]);
						/*if(fd1[0] != STDOUT_FILENO)
						{
							if(dup2(fd1[1],STDOUT_FILENO) != STDOUT_FILENO)
								app_error("dup2 error to stdout");
							close(fd[1]);
						}
						*/
						if(pgid ==0)
						{
							pgid = pid;
						}
						addjob(pid, pgid, (bg) ? BG : FG, cmdline);
					}
				}
				if(flag == 2)
				{
					int fd1[2];
					if(pipe(fd1)<0)
					{
						app_error("pipe error");
					}
					
					int fd2[2];
					if(pipe(fd2)<0)
					{
						app_error("pipe error");
					}
					
					if ((pid = fork()) == 0) /* child */
					{
						
						close(fd1[0]);
						if(fd1[1] != STDOUT_FILENO)
						{
							if(dup2(fd1[1],STDOUT_FILENO) != STDOUT_FILENO)
								app_error("dup2 error to stdout");
							close(fd1[1]);
						}
						
						close(fd2[1]);
						close(pipe_in);
						if(fd2[0] != STDIN_FILENO)
						{
							if(dup2(fd2[0],STDIN_FILENO) != STDIN_FILENO)
								app_error("dup2 error to stdin");
							close(fd2[1]);
						}
						
						sigprocmask(SIG_UNBLOCK, &prev, NULL);

						if (!setpgid(0, pgid)) {
							
							if (execve(argv1[0], argv1, environ))
								fprintf(stderr, "%s: Failed to execve\n", argv[0]);
							/* context changed */
						} else 
							unix_error("Failed to invoke setpgid(0, 0)");
					} else {
						/* Parent process */
						/*if(pgid ==0)
						{
							pgid = pid;
						}*/
						char line[MAXLINE];
						int n;
						close(fd2[0]);
						while((n=read(pipe_in,line,MAXLINE)) >0)
						{
							//int n = strlen(line);
							if(write(fd2[1],line, n) !=n)
								app_error("write error to pipe");
						}
						if(n<0)
							app_error("read error from pipe");
						close(fd2[1]);
						close(pipe_in);
						
						
						dup2(fd1[0],pipe_in);
						close(fd1[0]);
						close(fd1[1]);

						addjob(pid, pgid, (bg) ? BG : FG, cmdline);
					}
				}
				if(flag == 3)
				{
					int fd2[2];
					if(pipe(fd2)<0)
					{
						app_error("pipe error");
					}
					
					if ((pid = fork()) == 0) /* child */
					{
						
						close(fd2[1]);
						close(pipe_in);
						if(fd2[0] != STDIN_FILENO)
						{
							if(dup2(fd2[0],STDIN_FILENO) != STDIN_FILENO)
								app_error("dup2 error to stdin");
							close(fd2[1]);
						}
						sigprocmask(SIG_UNBLOCK, &prev, NULL);

						if (!setpgid(0, pgid)) {
							
							if (execve(argv1[0], argv1, environ))
								fprintf(stderr, "%s: Failed to execve\n", argv[0]);
							/* context changed */
						} else 
							unix_error("Failed to invoke setpgid(0, 0)");
					} else {
						/* Parent process */
						/*if(pgid ==0)
						{
							pgid = pid;
						}*/
						char line[MAXLINE];
						int n;
						close(fd2[0]);
						while((n = read(pipe_in,line,MAXLINE)) >0)
						{
							//int n = strlen(line);
							if(write(fd2[1],line, n) !=n)
								app_error("write error to pipe");
						}
						if(n<0)
							app_error("read error from pipe");
						close(fd2[1]);
						close(pipe_in);
						
						
						addjob(pid, pgid, (bg) ? BG : FG, cmdline);
					}
				}
				
			}
		}
		else{
			if ((pid = fork()) == 0) /* child */
			{
				sigprocmask(SIG_UNBLOCK, &prev, NULL);
				if (!setpgid(0, pgid)) {			
					if (execve(argv1[0], argv1, environ))
						fprintf(stderr, "%s: Failed to execve\n", argv[0]);
					/* context changed */
				} else 
					unix_error("Failed to invoke setpgid(0, 0)");
			} else {
				/* Parent process */
				if(pgid ==0)
				{
					pgid = pid;
				}
				addjob(pid, pgid, (bg) ? BG : FG, cmdline);
			}
		}
		
		//if(!bg)
				//    sigaddset(&prev, SIGCHLD);
		sigprocmask(SIG_SETMASK, &prev, NULL);
		if (!bg) 
		{
			//tcsetpgrp(0, pgid); //set the group as the frount group
			waitfg(pgid);
		}
		else 
			printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);

				// sigprocmask(SIG_SETMASK, &prev, NULL);
    }

    return;
}

int is_pipe2(char **argv, char **argv1, int *index)
{
	int argc = 0;
	memset(argv,0,sizeof(argv));
	
	while (argv[argc] != NULL || strcmp(argv[argc], "|")) 
	{
		argv1[argc] = argv[argc];
		argc++;
	}
	

	if(argv[argc] != NULL && index == 0)
	{
		index += argc;
		return 1;
	}else if (argv[argc] != NULL && argc != 0)
	{
		index += argc;
		return 2;
	}
	else if (argc != 0)
	{
		index += argc;
		return 3;
	}
	return 0;
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
        if (is_pipe(argv))
            return -1;
        exit(0);
    } else if (!strcmp(argv[0], "jobs")) {
        listjobs();
        if (is_pipe(argv))
            return -1;
        return 1;
    } else if (!strcmp(argv[0], "bg") || !strcmp(argv[0], "fg")) {
        do_bgfg(argv);
        if (is_pipe(argv))
            return -1;
        return 1;
    } else if (!strcmp(argv[0], "alias")) {
        alias_add(argv);
        if (is_pipe(argv))
            return -1;
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

    /* if (argv[2] == NULL) {
         if ((delim = strchr(argv[1], '=')) && delim[1] != '\'') {
             fprintf(stderr, "Error command of alias\n");
             return;
         } else {

             *delim = '\0';
             delim += 2;
             argv[3] = delim;
             delim = strchr(argv[3], '\'');
             *delim = '\0';

         }
     } else if (argv[3] == NULL) {
         if (argv[2][0] != '=' && argv[2][1] != '\'') {
             fprintf(stderr, "Error command of alias\n");
             return;
         } else {
             delim = &argv[2][2];
             argv[3] = delim;
             delim = strchr(argv[3], '\'');
             *delim = '\0';
         }
     }else*/ if (argv[4] != NULL) {
        fprintf(stderr, "Error command of alias\n");
        return;
    } else if (argv[2][1] == '\'') {
        fprintf(stderr, "Error command of alias\n");
        return;
    }


    p = alias_p;
    while (p != NULL) {
        if (!strcmp(p->new_command, argv[1])) {

            strcpy(p->old_command, argv[3]);

            return;
        } else
            p = p->next;
    }

    p = (struct alias_t *) malloc(sizeof(struct alias_t));

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
        struct alias_t *p = alias_p->next;
        free(alias_p);
        alias_p = p;

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
        job = getjobjid(jid);
        if (job == NULL) {
            printf("%%%d: no such job\n", jid);
            return;
        }
    } else if (isdigit(argv[1][0])) {//pid
        int pid = atoi(argv[1]);
        job = getjobpid(pid);
        if (job == NULL) {
            printf("(%d): no such process\n", pid);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }

    kill(-(job->pgid), SIGCONT);

    if (!strcmp(argv[0], "bg")) {
        job->state = BG;
        printf("[%d] (%d) %s", job->jid, job->pgid, job->cmdline);
    } else {
        job->state = FG;
        waitfg(job->pgid);
    }
    return;

}

/* 
 * waitfg - Block until process pid is no longer the foreground process 不推荐使用 waitpid 函数
 */
void waitfg(pid_t pgid) {
    if (pgid == 0) 
        return;

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTSTP);
    sigaddset(&mask, SIGQUIT);
    while (pgid==fgpgid()) 
        sigsuspend(&mask);
    
    return;

}

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    memset(job->pid, 0, sizeof(pid_t) * MAXJOBPS);
    job->pgid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
    return;
}

/* initjobs - Initialize the job list */
void initjobs(void) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
    return;
}

/* maxjid - Returns largest allocated job ID */
int maxjid(void) {
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(pid_t pid, pid_t pgid, int state, char *cmdline) {
    int i,j;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pgid == pgid) {
			for (j = 0; j < MAXJOBPS; j++) {
				if(jobs[i].pid[j] == 0)
				{
					jobs[i].pid[j] = pid;
					jobs[i].state = state;
					jobs[i].jid = nextjid++;
					if (nextjid > MAXJOBS)
						nextjid = 1;
					strcpy(jobs[i].cmdline, cmdline);
					if (verbose) {
						printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid[j], jobs[i].cmdline);
					}
					return 1;
				}
			}
        }
    }
	
	for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pgid == 0) {
			for (j = 0; j < MAXJOBPS; j++) {
				if(jobs[i].pid[j] == 0)
				{
					jobs[i].pid[j] = pid;
					jobs[i].state = state;
					jobs[i].jid = nextjid++;
					if (nextjid > MAXJOBS)
						nextjid = 1;
					strcpy(jobs[i].cmdline, cmdline);
					if (verbose) {
						printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid[j], jobs[i].cmdline);
					}
					return 1;
				}
			}
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(pid_t pgid) {
    int i;

    if (pgid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pgid == pgid) {
            clearjob(&jobs[i]);
            nextjid = maxjid() + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpgid - Return PID of current foreground job, 0 if no such job */
int fgpgid(void) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].state == FG)
	    return jobs[i].pgid;
    return 0;

}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(pid_t pid) {
    int i, j;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid != 0)
            for (j = 0; j < MAXJOBPS; j++)
            {
                if (jobs[i].pid[j] == 0)
                    break;
                if (jobs[i].pid[j] == pid)
                    return &jobs[i];
            }

    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(int jid) {
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
    int i, j;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pgid != 0)
            for (j = 0; j < MAXJOBPS; j++)
            {
                if (jobs[i].pid[j] == 0)
                    break;
                if (jobs[i].pid[j] == pid)
                    return jobs[i].jid;
            }

    return 0;
}

/* listjobs - Print the job list */
void listjobs(void) {
    int i, j;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pgid != 0) {
            for (j = 0; j < MAXJOBPS; j++) {
                if (jobs[i].pid[j] == 0)
                    break;
                printf("[%d] (%d) (%d)", jobs[i].jid, jobs[i].pid[j], jobs[i].pgid);
				
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
}
/******************************
 * end job list helper routines
 ******************************/

/*****************
 * Signal handlers
 *****************/

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
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig) {

    int status;
    //   int test = ECHILD;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { //WNOHANG不打算阻塞等待子进程返回时，可以这样使用。
        // printf("1");
        if (WIFEXITED(status)) /* returns true if the child terminated normally */
        {
            deletejob(pid);
        } else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
            deletejob(pid);
        } else if (WIFSTOPPED(status)) {
            printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
            struct job_t *job = getjobpid(pid);
            if (job != NULL) {
                job->state = ST;
            }
        }
    }
    if (errno != ECHILD) {
        unix_error("waitpid error");
    }


    return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig) {
    int olderrno = errno;

    pid_t pid = fgpgid();
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
    pid_t pid = fgpgid();
    if (pid != 0) {
        struct job_t *job = getjobpid(pid);
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

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

/*********************
 * End signal handlers
 *********************/

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
    fprintf(stdout, "%s : %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}




