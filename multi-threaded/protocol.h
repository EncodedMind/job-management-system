#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <unistd.h>
#include <sys/socket.h>

ssize_t write_all(int fd, const void* buff, size_t size);
ssize_t read_all(int fd, void* buff, size_t size);
bool send_message(int fd, const std::string& message);
bool receive_message(int fd, std::string& message);

#endif // PROTOCOL_H