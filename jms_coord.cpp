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

Each job creates a directory as soon as it starts where it saves the results of its execution:
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
#include <sstream>
#include <vector>
#include <algorithm>
#include <ctime>
#include <unordered_map>

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

using namespace std;

struct Pool{
    pid_t pid;
    int active_jobs;
    int fd_coord_in; // where the pool writes to (coord reads from)
    int fd_coord_out; // where the pool reads from (coord writes to)
};

struct Job{
    int id;
    pid_t pid;
    string status; // Active, Finished, Suspended
};

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

    vector<Pool> pools;
    unordered_map<int, Job> jobs;
    int total_pools = 0;
    int next_job_id = 1;

    string command;
    while(1){

        // zombie cleanup for pools (update vector accordingly)
        int pool_status;
        pid_t finished_pool_pid;
        while((finished_pool_pid = waitpid(-1, &pool_status, WNOHANG)) > 0){
            // a pool process has finished
            for(auto it = pools.begin(); it != pools.end(); ++it){
                if(it->pid == finished_pool_pid){
                    close(it->fd_coord_in);
                    close(it->fd_coord_out);
                    pools.erase(it);
                    break;
                }
            }
        }

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
            case SUBMIT: {
                command.erase(0, 7);

                // Check capacity of current pools
                Pool* selected_pool = nullptr;

                for(Pool& pool : pools){
                    if(pool.active_jobs < jobs_pool){
                        selected_pool = &pool;
                        break;
                    }
                }

                // Create new pool if necessary
                if(selected_pool == nullptr){
                    total_pools++;

                    // Create a new named pipe for communication with new pool
                    string pool_fifo_in = path + "/pool_" + to_string(total_pools) + "_in";
                    string pool_fifo_out = path + "/pool_" + to_string(total_pools) + "_out";

                    // delete if they exist
                    unlink(pool_fifo_in.c_str());
                    unlink(pool_fifo_out.c_str());

                    // coord will read from:
                    if(mkfifo(pool_fifo_in.c_str(), 0666) == -1){
                        cerr << "Error creating fifo" << endl;
                        exit(1);
                    }

                    // coord will write to:
                    if(mkfifo(pool_fifo_out.c_str(), 0666) == -1){
                        cerr << "Error creating fifo" << endl;
                        exit(1);
                    }

                    pid_t pool_pid = fork();
                    if(pool_pid == -1){
                        cerr << "Failed to fork" << endl;
                        exit(1);
                    }
                    
                    if(pool_pid == 0){ // Child process (pool)

                        int pool_fd_in, pool_fd_out;

                        if((pool_fd_out = open(pool_fifo_in.c_str(), O_WRONLY)) < 0){
                            cerr << "Error opening fifo for writing" << endl;
                            exit(1);
                        }
                        if((pool_fd_in = open(pool_fifo_out.c_str(), O_RDONLY)) < 0){
                            cerr << "Error opening fifo for reading" << endl;
                            exit(1);
                        }
                        
                        int finished_jobs = 0;
                        int received_jobs = 0;
                        string pool_message;
                        while(1){

                            if(received_jobs >= jobs_pool){
                                // Pool has received max numbers of jobs, so reading should be stopped
                                pid_t finished_pid = wait(NULL); // wait for any child process (job) to finish
                                if(finished_pid < 0){
                                    cerr << "Error waiting for child process" << endl;
                                    exit(1);
                                }
                                else if(finished_pid > 0){
                                    finished_jobs++;
                                }
                                if(finished_jobs >= jobs_pool){
                                    // Pool has finished max numbers of jobs, so it exits
                                    close(pool_fd_in);
                                    close(pool_fd_out);
                                    unlink(pool_fifo_in.c_str());
                                    unlink(pool_fifo_out.c_str());
                                    exit(0);
                                }
                                continue;
                            }

                            // clean up zombie processes
                            pid_t finished_pid;
                            while((finished_pid = waitpid(-1, NULL, WNOHANG)) > 0){
                                // a child process has finished
                                finished_jobs++;
                            }

                            if(finished_jobs >= jobs_pool){
                                // Pool has finished max numbers of jobs, so it exits
                                close(pool_fd_in);
                                close(pool_fd_out);
                                unlink(pool_fifo_in.c_str());
                                unlink(pool_fifo_out.c_str());
                                exit(0);
                            }

                            pool_message.resize(256, '\0');
                            // read command from new_pool.fd_in
                            int pool_bytes = read(pool_fd_in, &pool_message[0], pool_message.size() - 1);
                            if(pool_bytes < 0){
                                cerr << "Error reading from fifo" << endl;
                                exit(1);
                            }
                            else if(pool_bytes > 0){
                                pool_message.resize(pool_bytes);
                            }
                            else{
                                continue;
                            }

                            auto pos = pool_message.find('|');
                            string job_id_str = pool_message.substr(0, pos);
                            string pool_command = pool_message.substr(pos + 1);
                            int job_id = stoi(job_id_str);

                            istringstream iss(pool_command);
                            vector<string> arguments;
                            while(iss){
                                string arg;
                                iss >> arg;
                                if(!arg.empty()){
                                    arguments.push_back(arg);
                                }
                            }

                            int job_pid = fork();
                            if(job_pid == -1){
                                cerr << "Failed to fork" << endl;
                                exit(1);
                            }
                            if(job_pid == 0){ // Child process (job)
                                // Execute command (mkdir, dup2, execve) and send result back to files in directory

                                time_t timestamp = time(nullptr);
                                struct tm* timeinfo = localtime(&timestamp);
                                char date_str[9];
                                char time_str[7];
                                strftime(date_str, sizeof(date_str), "%Y%m%d", timeinfo);
                                strftime(time_str, sizeof(time_str), "%H%M%S", timeinfo);
                                string job_path = path + "/outputs_" + to_string(job_id) + "_" + to_string(getpid()) + "_" + date_str + "_" + time_str;
                            
                                if(mkdir(job_path.c_str(), 0777) == -1){
                                    cerr << "Error creating directory" << endl;
                                    exit(1);
                                }

                                string filedout_path = job_path + "/stdout_" + to_string(job_id);
                                string filederr_path = job_path + "/stderr_" + to_string(job_id);

                                int filedout;
                                if((filedout = open(filedout_path.c_str(), O_WRONLY | O_CREAT, 0666)) < 0){
                                    cerr << "Error opening fifo for reading" << endl;
                                    exit(1);
                                }

                                int filederr;
                                if((filederr = open(filederr_path.c_str(), O_WRONLY | O_CREAT, 0666)) < 0){
                                    cerr << "Error opening fifo for writing" << endl;
                                    exit(1);
                                }

                                dup2(filedout, STDOUT_FILENO);
                                dup2(filederr, STDERR_FILENO);

                                char* args[arguments.size() + 1];
                                for(size_t i = 0; i < arguments.size(); i++){
                                    args[i] = const_cast<char*>(arguments[i].c_str());
                                }
                                args[arguments.size()] = nullptr;
                                execvp(args[0], args);

                                // if execl returns, it means there was an error
                                cerr << "Error executing command" << endl;
                                exit(1);
                            }

                            // Return the job's pid
                            write(pool_fd_out, &job_pid, sizeof(job_pid));
                            received_jobs++;
                        }

                    }
                    else{ // Parent process (jms_coord)
                        Pool new_pool;
                        new_pool.pid = pool_pid;
                        new_pool.active_jobs = 0;
                        if((new_pool.fd_coord_in = open(pool_fifo_in.c_str(), O_RDONLY)) < 0){
                            cerr << "Error opening fifo for reading" << endl;
                            exit(1);
                        }
                        if((new_pool.fd_coord_out = open(pool_fifo_out.c_str(), O_WRONLY)) < 0){
                            cerr << "Error opening fifo for writing" << endl;
                            exit(1);
                        }

                        pools.push_back(new_pool);
                        selected_pool = &pools.back();
                    }                    
                }

                int current_job_id = next_job_id++;
                selected_pool->active_jobs++;
                
                string msg = to_string(current_job_id) + "|" + command; // send all the necessary data at once, separated with a '|'

                // Send job to selected pool through selected_pool.fd_out
                write(selected_pool->fd_coord_out, msg.c_str(), msg.size()+1);
                // Read the job's pid from pool through selected_pool.fd_coord_in
                pid_t job_pid;
                read(selected_pool->fd_coord_in, &job_pid, sizeof(job_pid));

                // Write JobID and its PID to jms_out
                string message = "JobID: " + to_string(current_job_id) + ", PID: " + to_string(job_pid) + "\n";
                write(fd_out, message.c_str(), message.size());
                break;
            }

            case STATUS: {
                
                // Argument check
                command.erase(0, 7);
                if(command.empty()){
                    write(fd_out, "Error: Please provide a JobID. \n", 32);
                    break;
                }

                // Get JobID from command
                int job_id = stoi(command);
                
                // Get status of the job (active, finished, suspended)
                if(jobs.find(job_id) == jobs.end()){
                    write(fd_out, "Error: JobID not found. \n", 26);
                    break;
                }
                
                Job target_job = jobs[job_id];

                // JobID <JobID> Status: <status>
                string status_message = "JobID: " + to_string(job_id) + ", Status: " + target_job.status;
                
                // If active, add for how many seconds it has been running
                if(target_job.status == "Active"){
                    int seconds = difftime(time(nullptr), target_job.start_time);
                    status_message += " (running for " + to_string(seconds) + " seconds)";
                }
                string status_message += "\n";

                // write to fd_out
                write(fd_out, status_message.c_str(), status_message.size());

                break;
            }

            case STATUS_ALL: {
                write(fd_out, "Coord says: STATUS_ALL received!", 32);
                break;
            }

            case SHOW_ACTIVE: {
                write(fd_out, "Coord says: SHOW_ACTIVE received!", 34);
                break;
            }

            case SHOW_POOLS: {
                write(fd_out, "Coord says: SHOW_POOLS received!", 33);
                break;
            }

            case SHOW_FINISHED: {
                write(fd_out, "Coord says: SHOW_FINISHED received!", 35);
                break;
            }

            case SUSPEND: {
                write(fd_out, "Coord says: SUSPEND received!", 29);
                break;
            }

            case RESUME: {
                write(fd_out, "Coord says: RESUME received!", 29);
                break;
            }

            case SHUTDOWN: {
                write(fd_out, "Coord says: SHUTDOWN received!", 30);
                break;
            }
            
            case INVALID: {
                write(fd_out, "Coord says: Invalid command!", 28);
                break;
            }
        } 
    }

    close(fd_in);
    close(fd_out);
    unlink(fifopath_in.c_str());
    unlink(fifopath_out.c_str());

    return 0;
}