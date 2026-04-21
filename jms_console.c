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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char *argv[]){

    char* w_arg = NULL;
    char* r_arg = NULL;
    char* o_arg = NULL;

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
                printf("Usage: jms_console -w <jms_in> -r <jms_out> [-o <operations_file>]\n");
                exit(1);
        }
    }

    if(w_arg == NULL || r_arg == NULL){
        printf("Usage: jms_console -w <jms_in> -r <jms_out> [-o <operations_file>]\n");
        exit(1);
    }

    char* path_out = w_arg;
    char* path_in = r_arg;
    int fd_commands;

    if(o_arg != NULL){
        if((fd_commands = open(o_arg, O_RDONLY)) < 0){
            perror("Error opening operations file");
            exit(1);
        }
    }
    else{
        fd_commands = STDIN_FILENO;
    }

    // open named-pipes for reading and writing
    int fd_out;
    if((fd_out = open(path_out, O_WRONLY)) < 0){
        perror("Error opening fifo for writing");
        exit(1);
    }

    int fd_in;
    if((fd_in = open(path_in, O_RDONLY)) < 0){
        perror("Error opening fifo for reading");
        exit(1);
    }

    // Read commands from fd_commands and send them to jms_coord through the named-pipe fd_out
    // Read responses from jms_coord through the named-pipe fd_in and print them

    char command[256];
    char response[256];
    char c;
    int chars_read = 0;

    while(1){

        int bytes = read(fd_commands, &c, 1);

        if(bytes < 0){
            perror("Error reading from fifo");
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
            if(chars_read < sizeof(command)-1){
                command[chars_read++] = c;
            }
            continue;
        }
        else{
            command[chars_read] = '\0';
            chars_read = 0;
            if(strlen(command) == 0) continue;
        }

        if(write(fd_out, command, strlen(command)) < 0){
            perror("Error writing to fifo");
            exit(1);
        }

        bytes = read(fd_in, response, sizeof(response)-1);
        if(bytes < 0){
            perror("Error reading from fifo");
            exit(1);
        }
        else if(bytes > 0){
            response[bytes] = '\0'; // null-terminate the response string
            printf("%s\n", response);
        }
    }

    if(o_arg != NULL) close(fd_commands);
    close(fd_in);
    close(fd_out);

    return 0;
}