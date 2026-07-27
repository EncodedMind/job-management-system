#ifndef JOB_MANAGER_H
#define JOB_MANAGER_H

#include <string>
#include <queue>
#include <unordered_map>
#include <pthread.h>

using namespace std;

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

Command encode(const string& command);

// worker stats

struct WorkerStats{
    pthread_t thread_id;
    bool is_idle; // true or false
    int current_job_id; // -1 if idle
    int jobs_served;
};

struct Job{
    int JobID;
    pid_t pid;
    string status; // Queued, Active, Finished
    time_t submit_time;
    time_t start_time;
    time_t end_time;
    string command;
};

// shared data
extern bool shutting_down; // indicates whether the coordinator is shutting down
extern queue<int> job_queue; // holds JobIDs of waiting jobs
extern unordered_map<int, Job> job_table; // maps JobID to Job struct
extern unordered_map<pthread_t, WorkerStats> worker_pool_stats; // maps worker thread id to its stats
extern int next_job_id;
extern int server_socket; // to be initialized in main, used in signal handler

// mutexes and condition variables for synchronization
extern pthread_mutex_t shared_state_mutex;
extern pthread_cond_t available_job_exists;
extern pthread_mutex_t worker_stats_mutex;

#endif // JOB_MANAGER_H