#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <string>

typedef enum{
    SUBMIT,
    STATUS,
    STATUS_ALL,
    SHOW_ACTIVE,
    SHOW_WORKERS,
    SHOW_FINISHED,
    SHUTDOWN,
    INVALID
} Command;

Command encode(const std::string& command);

#endif // JOB_MANAGER_H