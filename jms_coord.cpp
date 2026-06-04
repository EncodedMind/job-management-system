/*

jms_coord:

jobs: worker threads (thread pool) in jms_coord: fork() and exec() for each job
Max jobs in parallel = # worker threads

Coord creates thread pool (the worker threads) and monitors if jobs are active or have terminated.
Coord places jobs in job queue, protected from a pthread_mutex_t and condition variables.
When a worker thread is available, removes job from queue, calls fork()/ exec() and waitpid() until it is over
If exec() fails, child must terminate and job status finished

Each job creates a directory as soon as it starts where it saves the results of its execution: (same as hw1)
outputs_jobid_pid_date_time (outputs_3_1234_20260421_173000)
Directories in <path> given in coord (could be different than working directory) (!)
files in dir: stdout_jobid (the output of the job to stdout) and stderr_jobid (the output to stderr)

SYNCHRONIZATION:

Coord creates thread pool with pthread_create()
An extra listener thread (or the initial thread) is responsible for accept() to socket for new clients.
For each client, coord creates a handler thread which: reads from socket, directs them, returns results to client.
Handler thread can be either detached (pthread_detach) or with pthread_join() from coord
Make sure there are no resource leaks or zombie threads

*/

#include <iostream>
#include <string>
#include <cstring> // for memcpy
#include <unistd.h> // for getopt, read, write, close
#include <fcntl.h> // for open
#include <sys/socket.h> // for socket, bind, listen, accept
#include <sys/stat.h> // for mkdir
#include <netdb.h> // for gethostbyname
#include <netinet/in.h> // for sockaddr_in and htons
#include <stdlib.h> // for exit

#include "protocol.h"
#include "job_manager.h"
using namespace std;

void* handler_function(void* arg){
    int* newsock_ptr = (int*)arg;
    int newsock = *newsock_ptr;
    delete newsock_ptr; // free the dynamically allocated memory for newsock

    // detach
    int err;
    if(err = pthread_detach(pthread_self()) != 0){
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
                reply = "Coord says: SUBMIT executed successfully.";
                break;
            }
            case STATUS: {
                reply = "Coord says: STATUS executed successfully.";
                break;
            }
            case STATUS_ALL: {
                reply = "Coord says: STATUS-ALL executed successfully.";
                break;
            }
            case SHOW_ACTIVE: {
                reply = "Coord says: SHOW-ACTIVE executed successfully.";
                break;
            }
            case SHOW_WORKERS: {
                reply = "Coord says: SHOW-WORKERS executed successfully.";
                break;
            }
            case SHOW_FINISHED: {
                reply = "Coord says: SHOW-FINISHED executed successfully.";
                break;
            }
            case SHUTDOWN: {
                reply = "Coord says: SHUTDOWN executed successfully.";
                break;
            }
            case INVALID: {
                reply = "Coord says: INVALID command.";
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
    string path = l_arg;
    int workers = stoi(n_arg);

    // create directory if it doesn't exist, otherwise delete and recreate it

    if(mkdir(path.c_str(), 0777) == -1 && errno != EEXIST){ // if exists, don't try to remove it (security issue)
        cerr << "Error creating directory" << endl;
        exit(1);
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
        if(err = pthread_create(&handler_thread, NULL, handler_function, (void*)newsock_ptr) != 0){
            cerr << "Error creating handler thread" << endl;
            close(newsock);
            continue; // try accepting the next connection
        }

    }

    return 0;
}