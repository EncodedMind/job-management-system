/*
Communication with jms_coord, through named-pipes
Job submission
Query submission (status of service: How many jobs are being executed)
Presentation of the results from jms_coord

./jms_console -w <jms_in> -r <jms_out> -o <operations_file>
jms_in amd jms_out: Name of named-pipes to jms_coord
operations_file: Contains commands to be executed by jms_coord and the tree of jobs
If EOF or there is no -o <file> argument, it reads commands from the terminal (stdin)
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

int main(int argc, char *argv[]){

    string w_arg = "";
    string r_arg = "";
    string o_arg = "";

    int opt;
    while((opt = getopt(argc, argv, "w:r:o:")) != -1){
        switch(opt){
            case 'w':
                w_arg = optarg;
                break;
            case 'r':
                r_arg = optarg;
                break;
            case 'o':
                o_arg = optarg;
                break;
            default:
                cout << "Usage: jms_console -w <jms_in> -r <jms_out> [-o <operations_file>]\n";
                exit(1);
        }
    }

    if(w_arg.empty() || r_arg.empty()){
        cout << "Usage: jms_console -w <jms_in> -r <jms_out> [-o <operations_file>]\n";
        exit(1);
    }

    string path_out = w_arg;
    string path_in = r_arg;
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

    // open named-pipes for reading and writing
    int fd_out;
    if((fd_out = open(path_out.c_str(), O_WRONLY)) < 0){
        cerr << "Error opening fifo for writing" << endl;
        exit(1);
    }

    int fd_in;
    if((fd_in = open(path_in.c_str(), O_RDONLY)) < 0){
        cerr << "Error opening fifo for reading" << endl;
        exit(1);
    }

    // Read commands from fd_commands and send them to jms_coord through the named-pipe fd_out
    // Read responses from jms_coord through the named-pipe fd_in and print them

    string command;
    string response;
    char c;

    while(1){
        response.resize(256, '\0');

        int bytes = read(fd_commands, &c, 1);

        if(bytes < 0){
            cerr << "Error reading from fifo" << endl;
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
            if(command.empty()) continue;
        }

        if(write(fd_out, command.c_str(), command.length()) < 0){
            cerr << "Error writing to fifo" << endl;
            exit(1);
        }

        bytes = read(fd_in, &response[0], response.size()-1);
        if(bytes < 0){
            cerr << "Error reading from fifo" << endl;
            exit(1);
        }
        else if(bytes > 0){
            response.resize(bytes);
            cout << response << endl;
        }

        command.clear();
    }

    if(!o_arg.empty()) close(fd_commands);
    close(fd_in);
    close(fd_out);

    return 0;
}