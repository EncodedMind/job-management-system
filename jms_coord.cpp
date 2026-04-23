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

#include <iostream>
#include <string>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

using namespace std;

typedef enum{
    SUBMIT,
    STATUS,
    STATUS_ALL,
    SHOW_ACTIVE,
    SHOW_POOLS,
    SHOW_FINISHED,
    SUSPEND,
    RESUME,
    SHUTDOWN,
    INVALID
} Command;

Command encode(const string& command){

    size_t space_pos = command.find(' ');
    string command_part = command.substr(0, space_pos); // get the command part only

    if(command_part == "submit") return SUBMIT;
    else if(command_part == "status-all") return STATUS_ALL;
    else if(command_part == "status") return STATUS;
    else if(command_part == "show-active") return SHOW_ACTIVE;
    else if(command_part == "show-pools") return SHOW_POOLS;
    else if(command_part == "show-finished") return SHOW_FINISHED;
    else if(command_part == "suspend") return SUSPEND;
    else if(command_part == "resume") return RESUME;
    else if(command_part == "shutdown") return SHUTDOWN;
    else return INVALID; // Invalid command
}

int main(int argc, char *argv[]){
    
    // parse command line arguments

    if(argc != 5) {
        cout << "Usage: jms_coord -l <path> -n <jobs_pool>\n";
        exit(1);
    }

    string l_arg = "";
    string n_arg = "";

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
                cout << "Usage: jms_coord -l <path> -n <jobs_pool>\n";
                exit(1);
        }
    }

    if(l_arg.empty() || n_arg.empty()){
        cout << "Usage: jms_coord -l <path> -n <jobs_pool>\n";
        exit(1);
    }

    string path = l_arg;
    int jobs_pool = stoi(n_arg);

    // create directory and named pipes if they don't exist, otherwise delete and recreate them

    if(mkdir(path.c_str(), 0777) == -1 && errno != EEXIST){ // if exists, don't try to remove it (security issue)
        cerr << "Error creating directory" << endl;
        exit(1);
    }

    string fifopath_in = path + "/jms_in";
    string fifopath_out = path + "/jms_out";

    // delete fifopaths if they exist
    if(unlink(fifopath_in.c_str()) == -1 && errno != ENOENT){
        cerr << "Error deleting existing fifo" << endl;
        exit(1);
    }

    if(unlink(fifopath_out.c_str()) == -1 && errno != ENOENT){
        cerr << "Error deleting existing fifo" << endl;
        exit(1);
    }

    if(mkfifo(fifopath_in.c_str(), 0666) == -1){
        cerr << "Error creating fifo" << endl;
        exit(1);
    }

    if(mkfifo(fifopath_out.c_str(), 0666) == -1){
        cerr << "Error creating fifo" << endl;
        exit(1);
    }

    // open fifopaths for reading and writing
    int fd_in;
    if((fd_in = open(fifopath_in.c_str(), O_RDONLY)) < 0){
        cerr << "Error opening fifo for reading" << endl;
        exit(1);
    }

    int fd_out;
    if((fd_out = open(fifopath_out.c_str(), O_WRONLY)) < 0){
        cerr << "Error opening fifo for writing" << endl;
        exit(1);
    }

    // enter main loop to read commands from jms_in and process them accordingly

    string command;
    while(1){
        command.resize(256, '\0');

        // read command from jms_in
        int bytes = read(fd_in, &command[0], command.size() - 1);
        if(bytes < 0){
            cerr << "Error reading from fifo" << endl;
            exit(1);
        }
        else if(bytes > 0){
            command.resize(bytes);
        }
        else{ // EOF reached, restart connection
            close(fd_in);
            fd_in = open(fifopath_in.c_str(), O_RDONLY); // reopen fifo to avoid blocking
            if(fd_in < 0){
                cerr << "Error reopening fifo for reading" << endl;
                exit(1);
            }
            continue;
        }

        // process command
        switch(encode(command)){
            case SUBMIT:
                write(fd_out, "Coord says: SUBMIT received!", 28);
                break;
            case STATUS:
                write(fd_out, "Coord says: STATUS received!", 28);
                break;
            case STATUS_ALL:
                write(fd_out, "Coord says: STATUS_ALL received!", 32);
                break;
            case SHOW_ACTIVE:
                write(fd_out, "Coord says: SHOW_ACTIVE received!", 34);
                break;
            case SHOW_POOLS:
                write(fd_out, "Coord says: SHOW_POOLS received!", 33);
                break;
            case SHOW_FINISHED:
                write(fd_out, "Coord says: SHOW_FINISHED received!", 35);
                break;
            case SUSPEND:
                write(fd_out, "Coord says: SUSPEND received!", 29);
                break;
            case RESUME:
                write(fd_out, "Coord says: RESUME received!", 29);
                break;
            case SHUTDOWN:
                write(fd_out, "Coord says: SHUTDOWN received!", 30);
                break;
            case INVALID:
                write(fd_out, "Coord says: Invalid command!", 28);
                break;
        } 
    }

    close(fd_in);
    close(fd_out);
    unlink(fifopath_in.c_str());
    unlink(fifopath_out.c_str());

    return 0;
}