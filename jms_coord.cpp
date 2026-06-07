#include <iostream>
#include <string>
#include <queue> 
#include <unordered_map>
#include <vector>
#include <sstream>
#include <ctime>
#include <cstring> // for memcpy

#include <unistd.h> // for getopt, read, write, close
#include <fcntl.h> // for open
#include <sys/socket.h> // for socket, bind, listen, accept
#include <sys/stat.h> // for mkdir
#include <sys/wait.h> // for waitpid
#include <netdb.h> // for gethostbyname
#include <netinet/in.h> // for sockaddr_in and htons
#include <stdlib.h> // for exit
#include <pthread.h> // for threads, mutexes and condition variables

#include "protocol.h"
#include "job_manager.h"
#include "commands.h"
using namespace std;

string path;
void child_function(struct Job &job, const string &path);

void* handler_function(void* arg){
    int* newsock_ptr = (int*)arg;
    int newsock = *newsock_ptr;
    delete newsock_ptr; // free the dynamically allocated memory for newsock

    // detach
    int err;
    if((err = pthread_detach(pthread_self())) != 0){
        cerr << "Error detaching thread" << endl;
        close(newsock);
        return nullptr;
    }

    // Read commands in repeat and send results back to client until disconnection (read() returns 0)
    string command;
    while(receive_message(newsock, command)){
        if(command.empty()){
            continue; // ignore empty commands
        }

        // process command

        string reply;

        switch(encode(command)){
            case SUBMIT: {
                reply = handle_submit(command);
                break;
            }

            case STATUS: {
                reply = handle_status(command);
                break;
            }

            case STATUS_ALL: {
                reply = handle_status_all(command);
                break;
            }

            case SHOW_ACTIVE: {
                reply = handle_show_active(command);
                break;
            }

            case SHOW_WORKERS: {
                reply = handle_show_workers(command);
                break;
            }

            case SHOW_FINISHED: {
                reply = handle_show_finished(command);
                break;
            }

            case SHUTDOWN: {
                reply = handle_shutdown(command);
                break;
            }

            case INVALID: {
                reply = "Coord says: Invalid command!";
                break;
            }
        }

        if(!send_message(newsock, reply)){
            cerr << "Error sending message to client" << endl;
            break; // exit the loop and close the connection
        }
    }

    close(newsock);
    
    pthread_exit(nullptr);
}

void* worker_function(void* arg){
    (void)arg; // to silence unused parameter warning

    // initialize worker stats

    WorkerStats worker;

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&worker_stats_mutex)) != 0){
        cerr << "Error locking worker stats mutex" << endl;
        exit(1);
    }

    worker.thread_id = pthread_self();
    worker.is_idle = true;
    worker.current_job_id = -1;
    worker.jobs_served = 0;

    worker_pool_stats[worker.thread_id] = worker;
    
    // unlock mutex
    if((err = pthread_mutex_unlock(&worker_stats_mutex)) != 0){
        cerr << "Error unlocking worker stats mutex" << endl;
        exit(1);
    }

    while(1){ // repeatedly check for new jobs to execute

        // lock mutex
        int err;
        if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
            cerr << "Error locking mutex" << endl;
            exit(1);
        }

        // while + wait
        while(job_queue.empty() && !shutting_down){
            pthread_cond_wait(&available_job_exists, &shared_state_mutex);
        }

        // check if wake-up was due to shutdown signal
        if(shutting_down){
            // unlock mutex before exiting
            if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                cerr << "Error unlocking mutex" << endl;
                exit(1);
            }

            // return to main to be joined
            break;
        }

        // change shared data
        int job_id = job_queue.front();
        job_queue.pop();

        // unlock mutex
        if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
            cerr << "Error unlocking mutex" << endl;
            exit(1);
        }

        // update worker status and current_job_id

        // lock mutex
        if((err = pthread_mutex_lock(&worker_stats_mutex)) != 0){
            cerr << "Error locking worker stats mutex" << endl;
            exit(1);
        }

        // update stats
        worker_pool_stats[pthread_self()].is_idle = false;
        worker_pool_stats[pthread_self()].current_job_id = job_id;

        // unlock mutex
        if((err = pthread_mutex_unlock(&worker_stats_mutex)) != 0){
            cerr << "Error unlocking worker stats mutex" << endl;
            exit(1);
        }

        // execute the job

        // lock mutex
        if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
            cerr << "Error locking mutex" << endl;
            exit(1);
        }

        // update job status to "Active" and start_time
        job_table[job_id].status = "Active";
        job_table[job_id].start_time = time(nullptr);
        
        // unlock mutex
        if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
            cerr << "Error unlocking mutex" << endl;
            exit(1);
        }

        // fork
        pid_t child_pid = fork();
        switch(child_pid){
            case -1: // error
                cerr << "Error forking" << endl;
                exit(1);
                break;
            case 0: // child process - this will execute the job
                child_function(job_table[job_id], path);
                break;
            default: // parent process

                // wait for child to finish
                if(waitpid(child_pid, NULL, 0) == -1){
                    cerr << "Error waiting for child process" << endl;
                    exit(1);
                }

                // lock mutex
                if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
                    cerr << "Error locking mutex" << endl;
                    exit(1);
                }

                // update job status to "Finished" and end_time and job pid
                job_table[job_id].status = "Finished";
                job_table[job_id].end_time = time(nullptr);
                job_table[job_id].pid = child_pid;

                // unlock mutex
                if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                    cerr << "Error unlocking mutex" << endl;
                    exit(1);
                }

                // update worker stats to mark worker as idle

                // lock mutex
                if((err = pthread_mutex_lock(&worker_stats_mutex)) != 0){
                    cerr << "Error locking worker stats mutex" << endl;
                    exit(1);
                }

                // update stats
                worker_pool_stats[pthread_self()].is_idle = true;
                worker_pool_stats[pthread_self()].current_job_id = -1;
                worker_pool_stats[pthread_self()].jobs_served++;

                // unlock mutex
                if((err = pthread_mutex_unlock(&worker_stats_mutex)) != 0){
                    cerr << "Error unlocking worker stats mutex" << endl;
                    exit(1);
                }

                break;
        }
        
    }
    
    return nullptr;
}

int main(int argc, char* argv[]){

    // parse command line arguments (port, path, workers)

    if(argc != 7){
        cout << "Usage: jms_coord -p <port> -l <path> -n <workers>\n";
        exit(1);
    }

    string p_arg = "";
    string l_arg = "";
    string n_arg = "";

    int opt;
    while((opt = getopt(argc, argv, "p:l:n:")) != -1){
        switch(opt){
            case 'p':
                p_arg = optarg;
                break;
            case 'l':
                l_arg = optarg;
                break;
            case 'n':
                n_arg = optarg;
                break;
            default:
                cout << "Usage: jms_coord -p <port> -l <path> -n <workers>\n";
                exit(1);
        }
    }

    if(p_arg.empty() || l_arg.empty() || n_arg.empty()){
        cout << "Usage: jms_coord -p <port> -l <path> -n <workers>\n";
        exit(1);
    }

    int port = stoi(p_arg);
    path = l_arg;
    int workers = stoi(n_arg);

    // create directory if it doesn't exist

    if(mkdir(path.c_str(), 0777) == -1 && errno != EEXIST){ // if exists, don't try to remove it (security issue)
        cerr << "Error creating directory" << endl;
        exit(1);
    }

    // clean up previous execution files
    string cleanup_cmd = "rm -rf " + path + "/*";
    if (system(cleanup_cmd.c_str()) == -1) {
        cerr << "Warning: Failed to clean up directory" << endl;
    }

    // create socket, bind, listen

    if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr << "Error creating socket" << endl;
        exit(1);
    }

    struct sockaddr_in server, client;
    struct sockaddr *serverptr = (struct sockaddr *)&server;
    struct sockaddr *clientptr = (struct sockaddr *)&client;
    socklen_t clientlen = sizeof(client);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    int optval = 1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){ // allow reuse of address
        cerr << "Error setting socket options" << endl;
        exit(1);
    }

    if(bind(server_socket, serverptr, sizeof(server)) < 0){
        cerr << "Error binding socket" << endl;
        exit(1);
    }

    if(listen(server_socket, workers) < 0){ // backlog = workers, since we can only handle that many clients at a time
        cerr << "Error listening on socket" << endl;
        exit(1);
    }

    int newsock;

    // create worker threads
    for(int i = 0; i < workers; i++){
        pthread_t worker_thread;
        int err;
        if((err = pthread_create(&worker_thread, NULL, worker_function, NULL)) != 0){
            cerr << "Error creating worker thread" << endl;
            exit(1);
        }
    }

    while(1){
        // accept new client connection

        if((newsock = accept(server_socket, clientptr, &clientlen)) < 0){
            if(!shutting_down){ // if error is not due to shutdown, print error
                cerr << "Error accepting connection" << endl;
                continue; // try accepting the next connection
            }
            else{
                break; // if shutting down, exit the loop
            }
        }

        // create new handler thread

        pthread_t handler_thread;
        int err;
        int* newsock_ptr = new int(newsock); // dynamically allocate memory for newsock to pass to thread
        if((err = pthread_create(&handler_thread, NULL, handler_function, (void*)newsock_ptr)) != 0){
            cerr << "Error creating handler thread" << endl;
            close(newsock);
            continue; // try accepting the next connection
        }

    }

    // shutdown sequence

    // clear job queue (optional, but nice practice)
    while(!job_queue.empty()){
        job_queue.pop();
    }

    // join all worker threads
    for(const auto& pair : worker_pool_stats){
        pthread_t worker_thread_id = pair.first;
        int err;
        if((err = pthread_join(worker_thread_id, NULL)) != 0){
            cerr << "Error joining worker thread" << endl;
            continue; // try joining the next thread
        }
    }

    // close the socket
    close(server_socket);

    return 0;
}

void child_function(struct Job &job, const string &path){
    
    // create directory and files for job outputs

    time_t timestamp = time(nullptr);
    struct tm* timeinfo = localtime(&timestamp);
    char date_str[9];
    char time_str[7];
    strftime(date_str, sizeof(date_str), "%Y%m%d", timeinfo);
    strftime(time_str, sizeof(time_str), "%H%M%S", timeinfo);
    
    string job_path = path + "/outputs_" + to_string(job.JobID) + "_" + to_string(getpid()) + "_" + date_str + "_" + time_str;
                            
    if(mkdir(job_path.c_str(), 0777) == -1){
        cerr << "Error creating directory" << endl;
        exit(1);
    }

    string filedout_path = job_path + "/stdout_" + to_string(job.JobID);
    string filederr_path = job_path + "/stderr_" + to_string(job.JobID);

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

    // execute the command

    istringstream iss(job.command);
    vector<string> arguments;
    while(iss){
        string arg;
        iss >> arg;
        if(!arg.empty()){
            arguments.push_back(arg);
        }
    }

    char* args[arguments.size() + 1];
    for(size_t i = 0; i < arguments.size(); i++){
        args[i] = const_cast<char*>(arguments[i].c_str());
    }
    args[arguments.size()] = nullptr;
    execvp(args[0], args);

    // exec failed. Terminate child process
    cerr << "Error executing command" << endl;
    exit(1);
    
}