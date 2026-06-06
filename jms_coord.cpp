/*

Commands:

5) show-workers (similar to hw1)
Finds each worker thread's id (pthread_self()) and their state (idle or JobID serving at the moment) and number of jobs each one has served
thread id => cast to unsigned long
Returns:
Worker Thread ID & State & Served:
<id1> idle served X
...
<idn> running JobID X served X
(ίσως με tabs μεταξύ κάθε κατηγορίας για να είναι πιο όμορφο, όπως στο pdf)

7) shutdown

- Stops accepting new connections
- Creates shared variable "shutting_down"
- Broadcast to conditions so that they wake up all worker threads

- Each idle thread is terminated
- Workers wait with waitpid() for their children to finish and then they terminate
- Jobs in queue are discarded
- Main thread pthread_join() to all worker threads
- Closes listening socket
- Statistics (Served X jobs, Y were running, Z were still queued) (βλ. hw1)

*/

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
using namespace std;

struct Job{
    int JobID;
    pid_t pid;
    string status; // Queued, Active, Finished
    time_t submit_time;
    time_t start_time;
    time_t end_time;
    string command;
};

// shared data
queue<int> job_queue; // holds JobIDs of waiting jobs
unordered_map<int, Job> job_table; // maps JobID to Job struct
int next_job_id = 1;

// mutexes and condition variables for synchronization
pthread_mutex_t shared_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t available_job_exists = PTHREAD_COND_INITIALIZER;

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

        cout << "Client sent: " << command << endl; // DEBUG

        // process command

        string reply;

        switch(encode(command)){
            case SUBMIT: {
                // remove "submit " from command
                command.erase(0, 7);
                
                // create new Job
                Job new_job;
                new_job.JobID = next_job_id++;
                new_job.status = "Queued";
                new_job.submit_time = time(nullptr);
                new_job.command = command;

                // lock mutex
                int err;
                if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
                    cerr << "Error locking mutex" << endl;
                    exit(1);
                }

                // change shared data
                job_queue.push(new_job.JobID);
                job_table[new_job.JobID] = new_job;

                // signal condition variable
                pthread_cond_signal(&available_job_exists);

                // unlock mutex
                if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                    cerr << "Error unlocking mutex" << endl;
                    exit(1);
                }

                reply = "JobID: " + to_string(new_job.JobID);
                break;
            }

            case STATUS: {

                // Argument check
                command.erase(0, 7);
                if(command.empty()){
                    reply = "Error: Please provide a JobID. \n";
                    break;
                }

                // Get JobID from command
                int job_id = stoi(command);

                // lock mutex
                int err;
                if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
                    cerr << "Error locking mutex" << endl;
                    break;
                }

                // Check if job exists
                if(job_table.find(job_id) == job_table.end()){
                    reply = "JobID " + to_string(job_id) + " not found.\n";
                }
                else{
                    // Get status of the job (Queued, Active, Finished)
                    Job target_job = job_table[job_id];
                    reply = "JobID: " + to_string(job_id) + " Status: " + target_job.status;

                    // If active, add for how many seconds it has been running
                    if(target_job.status == "Active"){
                        int seconds = difftime(time(nullptr), target_job.start_time);
                        reply += " (running for " + to_string(seconds) + " seconds)";
                    }
                    else if(target_job.status == "Queued"){ // If queued, add message
                        reply += " (waiting in job queue)";
                    }

                    reply += "\n";
                }

                // unlock mutex
                if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                    cerr << "Error unlocking mutex" << endl;
                    break;
                }

                break;
            }

            case STATUS_ALL: {

                int n = -1;

                // Argument check
                if(command.size() > 11){
                    command.erase(0, 11);
                    if(command.empty()){
                        reply = "Error: Invalid argument. \n";
                        break;
                    }
                    else{
                        n = stoi(command);
                    }
                }

                // lock mutex
                int err;
                if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
                    cerr << "Error locking mutex" << endl;
                    break;
                }

                // If no n provided, print status of all jobs
                // If n is provided, print status for jobs submitted in last n seconds

                reply = "";
                for(const auto& pair : job_table){
                    bool print_job = false;

                    if(n == -1){
                        print_job = true;
                    }
                    else{
                        if(difftime(time(nullptr), pair.second.submit_time) <= n){
                            print_job = true;
                        }
                    }

                    if(print_job){
                        reply += "JobID: " + to_string(pair.first) + " Status: " + pair.second.status;
                        if(pair.second.status == "Active"){
                            int seconds = difftime(time(nullptr), pair.second.start_time);
                            reply += " (running for " + to_string(seconds) + " seconds)";
                        }
                        else if(pair.second.status == "Queued"){
                            reply += " (waiting in job queue)";
                        }
                        reply += "\n";
                    }
                }

                if(reply.empty()){
                    reply = "No jobs found.\n";
                }

                // unlock mutex
                if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                    cerr << "Error unlocking mutex" << endl;
                    break;
                }

                break;
            }

            case SHOW_ACTIVE: {

                // Argument check
                if(command.size() > 11){
                    reply = "Error: SHOW_ACTIVE does not take any arguments. \n";
                    break;
                }

                // lock mutex
                int err;
                if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
                    cerr << "Error locking mutex" << endl;
                    break;
                }

                reply = "Active jobs:\n";

                for(const auto& pair : job_table){
                    if(pair.second.status == "Active"){
                        reply += "JobID " + to_string(pair.first) + "\n";
                    }
                }

                // unlock mutex
                if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                    cerr << "Error unlocking mutex" << endl;
                    break;
                }

                break;
            }

            case SHOW_WORKERS: {
                reply = "Coord says: SHOW-WORKERS executed successfully.";
                break;
            }

            case SHOW_FINISHED: {

                // Argument check
                if(command.size() > 13){
                    reply = "Error: show-finished does not take any arguments. \n";
                    break;
                }

                // lock mutex
                int err;
                if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
                    cerr << "Error locking mutex" << endl;
                    break;
                }

                reply = "Finished jobs:\n";

                for(const auto& pair : job_table){
                    if(pair.second.status == "Finished"){
                        reply += "JobID " + to_string(pair.first) + "\n";
                    }
                }

                // unlock mutex
                if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
                    cerr << "Error unlocking mutex" << endl;
                    break;
                }                

                break;
            }

            case SHUTDOWN: {
                reply = "Coord says: SHUTDOWN executed successfully.";
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

    cout << "Client disconnected!" << endl; // DEBUG
    close(newsock);
    
    pthread_exit(nullptr);
}

void* worker_function(void* arg){
    (void)arg; // to silence unused parameter warning
    
    while(1){ // repeatedly check for new jobs to execute

        // lock mutex
        int err;
        if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
            cerr << "Error locking mutex" << endl;
            exit(1);
        }

        // while + wait
        while(job_queue.empty()){
            pthread_cond_wait(&available_job_exists, &shared_state_mutex);
        }

        // change shared data
        int job_id = job_queue.front();
        job_queue.pop();

        // unlock mutex
        if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
            cerr << "Error unlocking mutex" << endl;
            exit(1);
        }

        cout << "Worker thread picked up JobID " << job_id << endl; // DEBUG

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

    int sock;
    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
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
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){ // allow reuse of address
        cerr << "Error setting socket options" << endl;
        exit(1);
    }

    if(bind(sock, serverptr, sizeof(server)) < 0){
        cerr << "Error binding socket" << endl;
        exit(1);
    }

    if(listen(sock, workers) < 0){ // backlog = workers, since we can only handle that many clients at a time
        cerr << "Error listening on socket" << endl;
        exit(1);
    }

    int newsock;
    
    cout << "Coord started. Listening on port " << port << "..." << endl; // DEBUG

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

        if((newsock = accept(sock, clientptr, &clientlen)) < 0){
            cerr << "Error accepting connection" << endl;
            continue; // try accepting the next connection
        }

        cout << "New client connected!" << endl; // DEBUG

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