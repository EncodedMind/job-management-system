// for messages, we append length + | + message, so the receiver can read the length first and know how many bytes to read for the message

#include <iostream>
#include <string>
#include <cstring> // for memcpy
#include <unistd.h> // for getopt, read, write, close
#include <fcntl.h> // for open
#include <sys/socket.h> // for socket, connect
#include <netdb.h> // for gethostbyname
#include <netinet/in.h> // for sockaddr_in and htons
#include <stdlib.h> // for exit

#include "protocol.h"
using namespace std;

int main(int argc, char *argv[]){

    string h_arg = "";
    string p_arg = "";
    string o_arg = "";

    int opt;
    while((opt = getopt(argc, argv, "h:p:o:")) != -1){
        switch(opt){
            case 'h':
                h_arg = optarg;
                break;
            case 'p':
                p_arg = optarg;
                break;
            case 'o':
                o_arg = optarg;
                break;
            default:
                cout << "Usage: jms_console -h <host> -p <port> [-o <operations_file>]\n";
                exit(1);
        }
    }

    if(h_arg.empty() || p_arg.empty()){
        cout << "Usage: jms_console -h <host> -p <port> [-o <operations_file>]\n";
        exit(1);
    }

    string host = h_arg;
    int port = stoi(p_arg);
    int fd_commands;

    if(!o_arg.empty()){
        if((fd_commands = open(o_arg.c_str(), O_RDONLY)) < 0){
            cerr << "Error opening operations file" << endl;
            exit(1);
        }
    }
    else{
        fd_commands = STDIN_FILENO;
    }

    // ==== Open TCP socket and connect to coord ====

    // create socket
    int sock;
    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        cerr << "Error creating socket" << endl;
        exit(1);
    }

    // find server address
    struct hostent *rem;
    if((rem = gethostbyname(host.c_str())) == NULL){
        cerr << "Error finding host" << endl;
        exit(1);
    }

    struct sockaddr_in server;
    struct sockaddr *serverptr = (struct sockaddr *)&server;
    server.sin_family = AF_INET;
    memcpy(&server.sin_addr, rem->h_addr, rem->h_length);
    server.sin_port = htons(port);

    // connect to server
    if(connect(sock, serverptr, sizeof(server)) < 0){
        cerr << "Error connecting to server" << endl;
        exit(1);
    }

    // Read commands from fd_commands and send to coord through socket
    // Read responses from coord through socket and print them

    string command;
    string response;
    char c;

    while(1){

        int bytes = read(fd_commands, &c, 1);

        if(bytes < 0){
            cerr << "Error reading command" << endl;
            exit(1);
        }
        else if(bytes == 0){
            if(fd_commands == STDIN_FILENO){
                break; // EOF on stdin, exit the loop
            }
            close(fd_commands);
            fd_commands = STDIN_FILENO; // switch to stdin if EOF on file is reached
            continue;
        }

        if(c != '\n'){
            command += c;
            continue;
        }
        else{
            if(command.empty()) continue; // ignore empty commands
        }

        // send command to coord
        if(!send_message(sock, command)){
            cerr << "Error writing to socket" << endl;
            exit(1);
        }

        // read response from coord
        if(!receive_message(sock, response)){
            cerr << "Error reading from socket" << endl;
            exit(1);
        }
        
        cout << response << endl;

        command.clear();
    }

    if(!o_arg.empty()) close(fd_commands);
    close(sock);

    return 0;
}