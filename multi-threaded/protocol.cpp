#include "protocol.h"
#include <errno.h>

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

bool send_message(int fd, const std::string& message){
    // we will send the header first, then the message
    std::string header = std::to_string(message.length()) + "|";
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

bool receive_message(int fd, std::string& message){
    // read header first
    std::string header;
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

    int message_length = std::stoi(header);
    message.resize(message_length, '\0');

    if(message_length > 0){
        if(read_all(fd, &message[0], message_length) == -1){
            return false; // error
        }
    }

    return true;
}