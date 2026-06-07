#include <queue>
#include <unordered_map>
#include <pthread.h>

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

// shared data
bool shutting_down = false; // indicates whether the coordinator is shutting down
queue<int> job_queue; // holds JobIDs of waiting jobs
unordered_map<int, Job> job_table; // maps JobID to Job struct
unordered_map<pthread_t, WorkerStats> worker_pool_stats; // maps worker thread id to its stats
int next_job_id = 1;
int server_socket = -1; // to be initialized in main, used in signal handler

// mutexes and condition variables for synchronization
pthread_mutex_t shared_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t available_job_exists = PTHREAD_COND_INITIALIZER;
pthread_mutex_t worker_stats_mutex = PTHREAD_MUTEX_INITIALIZER;