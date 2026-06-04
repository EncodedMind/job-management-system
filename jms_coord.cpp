/*

jms_coord:

jobs: worker threads (thread pool) in jms_coord: fork() and exec() for each job
Max jobs in parallel = # worker threads

Coord creates thread pool (the worker threads) and monitors if jobs are active or have terminated.
Coord places jobs in job queue, protected from a pthread_mutex_t and condition variables.
When a worker thread is available, removes job from queue, calls fork()/ exec() and waitpid() until it is over
If exec() fails, child must terminate and job status finished

Coord listens to a port, accept() connections and assigns each client at a handler thread, which reads the operations, directs them and returns results.
Needs a text-based protocol for operation - result exchange to TCP stream.
Thread accepts socket => handler thread (pthread_create + pthread_detach), ώστε ο listener να πηγαίνει αμέσως στο επόμενο accept()

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

using namespace std;

ssize_t write_all(int fd, const void* buff, size_t size);
ssize_t read_all(int fd, void* buff, size_t size);
bool send_message(int fd, const string& message);
bool receive_message(int fd, string& message);

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

        // Read commands in repeat and send results back to client until disconnection (read() returns 0)
        string command;
        while(receive_message(newsock, command)){
            if(command.empty()){
                continue; // ignore empty commands
            }

            cout << "Client sent: " << command << endl; // DEBUG

            string reply = command + " processed"; // Placeholder

            if(!send_message(newsock, reply)){
                cerr << "Error sending message to client" << endl;
                break; // exit the loop and close the connection
            }
        }

        cout << "Client disconnected!" << endl; // DEBUG
        close(newsock);
    }

    return 0;
}

ssize_t write_all(int fd, const void* buff, size_t size){
    size_t sent = 0;
    ssize_t n;

    while(sent < size){
        // use send() with MSG_NOSIGNAL to avoid SIGPIPE if client has disconnected
        if((n = send(fd, (const char*)buff + sent, size - sent, MSG_NOSIGNAL)) == -1){
            if(errno == EINTR){
                continue; // interrupted by signal, try again
            }
            return -1; // error
        }
        sent += n;
    }
    return sent;
}

ssize_t read_all(int fd, void* buff, size_t size){
    size_t received = 0;
    ssize_t n;

    while(received < size){

        if((n = read(fd, (char*)buff + received, size - received)) == -1){
            if(errno == EINTR){
                continue; // interrupted by signal, try again
            }
            return -1; // error
        }
        else if(n == 0){
            return 0; // EOF
        }
        received += n;
    }
    return received;
}

bool send_message(int fd, const string& message){
    // we will send the header first, then the message
    string header = to_string(message.length()) + "|";
    if(write_all(fd, header.c_str(), header.length()) == -1){
        return false;
    }

    if(message.length() > 0){
        if(write_all(fd, message.c_str(), message.length()) == -1){
            return false;
        }
    }
    return true;
}

bool receive_message(int fd, string& message){
    // read header first
    string header;
    char c;

    while(1){
        ssize_t bytes = read(fd, &c, 1);
        if(bytes < 0){
            return false; // error
        }
        else if(bytes == 0){
            return false; // EOF
        }

        if(c == '|'){ // end of header
            break;
        }
        header += c;
    }

    int message_length = stoi(header);
    message.resize(message_length, '\0');

    if(message_length > 0){
        if(read_all(fd, &message[0], message_length) == -1){
            return false; // error
        }
    }

    return true;
}