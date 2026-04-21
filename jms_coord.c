/*
Creates hierarchy of jobs and checks which are active and which have been terminated
Does not execute jobs immediately
Creates pools. Each pool is a forked process from jms_coord. Communication through named pipes
Each pool can handle a max amount of jobs
If a pool has handled max jobs, sends the collected data to jms_coord process and gets removed from the tree hierarchy

Input from jms_in (commands), output to jms_out

Commands:
1) submit <job>
2) status <JobID>
3) status-all [n]
4) show-active
5) show-pools
6) show-finished
7) suspend <JobID>
8) resume <JobID>
9) shutdown

Each job creates a directory as soon as it starts where is saves the results of its execution:
outputs_jobid_pid_date_time (outputs_3_1234_20260421_173000)
files in dir: stdout_jobid (the output of the job to stdout) and stderr_jobid (the output to stderr)

./jms_coord -l <path> -n <jobs_pool>
path: Directory where files and directories will be produced during the execution of the programs
jobs_pool: Max number of jobs each pool can handle

Clear output files and pipes at start
jms_coord must check if named pipes already exist and if they do, they must be deleted
named pipes: One process blocked (until other end opens pipe) OR open(O_NONBLOCK - and read only)
For the second case, if write end opens without having opened read end, error is returned
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

typedef enum{
    SUBMIT,
    STATUS,
    STATUS_ALL,
    SHOW_ACTIVE,
    SHOW_POOLS,
    SHOW_FINISHED,
    SUSPEND,
    RESUME,
    SHUTDOWN
} Command;

Command encode(char* command){
    if(strncmp(command, "submit", 6) == 0) return SUBMIT;
    else if(strncmp(command, "status-all", 10) == 0) return STATUS_ALL;
    else if(strncmp(command, "status", 6) == 0) return STATUS;
    else if(strncmp(command, "show-active", 11) == 0) return SHOW_ACTIVE;
    else if(strncmp(command, "show-pools", 10) == 0) return SHOW_POOLS;
    else if(strncmp(command, "show-finished", 13) == 0) return SHOW_FINISHED;
    else if(strncmp(command, "suspend", 7) == 0) return SUSPEND;
    else if(strncmp(command, "resume", 6) == 0) return RESUME;
    else if(strncmp(command, "shutdown", 8) == 0) return SHUTDOWN;
    else return -1; // Invalid command
}

int main(int argc, char *argv[]){
    
    // parse command line arguments

    if(argc != 5) {
        printf("Usage: jms_coord -l <path> -n <jobs_pool>\n");
        exit(1);
    }

    char* l_arg = NULL;
    char* n_arg = NULL;

    int opt;
    while((opt = getopt(argc, argv, "l:n:")) != -1){
        switch(opt){
            case 'l':
                l_arg = optarg;
                break;
            case 'n':
                n_arg = optarg;
                break;
            default:
                printf("Usage: jms_coord -l <path> -n <jobs_pool>\n");
                exit(1);
        }
    }

    if(l_arg == NULL || n_arg == NULL){
        printf("Usage: jms_coord -l <path> -n <jobs_pool>\n");
        exit(1);
    }

    char* path = l_arg;
    int jobs_pool = atoi(n_arg);

    // create directory and named pipes if they don't exist, otherwise delete and recreate them

    if(mkdir(path, 0777) == -1 && errno != EEXIST){ // if exists, don't try to remove it (security issue)
        perror("Error creating directory");
        exit(1);
    }

    char fifopath_in[256];
    char fifopath_out[256];
    snprintf(fifopath_in, sizeof(fifopath_in), "%s/jms_in", path);
    snprintf(fifopath_out, sizeof(fifopath_out), "%s/jms_out", path);

    // delete fifopaths if they exist
    if(unlink(fifopath_in) == -1 && errno != ENOENT){
        perror("Error deleting existing fifo");
        exit(1);
    }

    if(unlink(fifopath_out) == -1 && errno != ENOENT){
        perror("Error deleting existing fifo");
        exit(1);
    }

    if(mkfifo(fifopath_in, 0666) == -1){
        perror("mkfifo");
        exit(1);
    }

    if(mkfifo(fifopath_out, 0666) == -1){
        perror("mkfifo");
        exit(1);
    }

    // open fifopaths for reading and writing
    int fd_in;
    if((fd_in = open(fifopath_in, O_RDONLY)) < 0){
        perror("Error opening fifo for reading");
        exit(1);
    }

    int fd_out;
    if((fd_out = open(fifopath_out, O_WRONLY)) < 0){
        perror("Error opening fifo for writing");
        exit(1);
    }

    // enter main loop to read commands from jms_in and process them accordingly

    char command[256];

    while(1){
        // read command from jms_in
        int bytes = read(fd_in, command, sizeof(command)-1);
        if(bytes < 0){
            perror("Error reading from fifo");
            exit(1);
        }
        else if(bytes > 0){
            command[bytes] = '\0'; // null-terminate the command string
        }

        // process command
        switch(encode(command)){
            case SUBMIT:
                break;
            case STATUS:
                break;
            case STATUS_ALL:
                break;
            case SHOW_ACTIVE:
                break;
            case SHOW_POOLS:
                break;
            case SHOW_FINISHED:
                break;
            case SUSPEND:
                break;
            case RESUME:
                break;
            case SHUTDOWN:
                break;
            case -1:
                printf("Invalid command\n");
                break;
        } 
    }

    close(fd_in);
    close(fd_out);
    unlink(fifopath_in);
    unlink(fifopath_out);

    return 0;
}

/*
So far, it creates directory and named pipes (with blocking), and enters a loop to read commands from jms_in.
*/