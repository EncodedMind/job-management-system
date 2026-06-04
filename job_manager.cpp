#include "job_manager.h"
using namespace std;

Command encode(const string& command){

    size_t space_pos = command.find(' ');
    string command_part = command.substr(0, space_pos); // get the command part only

    if(command_part == "submit") return SUBMIT;
    else if(command_part == "status-all") return STATUS_ALL;
    else if(command_part == "status") return STATUS;
    else if(command_part == "show-active") return SHOW_ACTIVE;
    else if(command_part == "show-workers") return SHOW_WORKERS;
    else if(command_part == "show-finished") return SHOW_FINISHED;
    else if(command_part == "shutdown") return SHUTDOWN;
    else return INVALID; // Invalid command
}