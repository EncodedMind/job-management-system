#include <iostream>
#include <string>
#include <sstream>
#include <ctime>

#include <pthread.h> // for mutexes and condition variables
#include <sys/socket.h> // for shutdown()

#include "commands.h"
#include "job_manager.h"

using namespace std;

string handle_submit(string command) {
    string reply;

    // remove "submit " from command
    command.erase(0, 7);
    
    // create new Job
    Job new_job;
    new_job.JobID = next_job_id++;
    new_job.status = "Queued";
    new_job.submit_time = time(nullptr);
    new_job.command = command;

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        exit(1);
    }

    // change shared data
    job_queue.push(new_job.JobID);
    job_table[new_job.JobID] = new_job;

    // signal condition variable
    pthread_cond_signal(&available_job_exists);

    // unlock mutex
    if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        exit(1);
    }

    reply = "JobID: " + to_string(new_job.JobID);
    return reply;
}

string handle_status(string command) {
    string reply;
    
    // Argument check
    command.erase(0, 7);
    if(command.empty()){
        reply = "Error: Please provide a JobID. \n";
        return reply;
    }

    // Get JobID from command
    int job_id = stoi(command);

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        return reply;
    }

    // Check if job exists
    if(job_table.find(job_id) == job_table.end()){
        reply = "JobID " + to_string(job_id) + " not found.\n";
    }
    else{
        // Get status of the job (Queued, Active, Finished)
        Job target_job = job_table[job_id];
        reply = "JobID: " + to_string(job_id) + " Status: " + target_job.status;

        // If active, add for how many seconds it has been running
        if(target_job.status == "Active"){
            int seconds = difftime(time(nullptr), target_job.start_time);
            reply += " (running for " + to_string(seconds) + " seconds)";
        }
        else if(target_job.status == "Queued"){ // If queued, add message
            reply += " (waiting in job queue)";
        }

        reply += "\n";
    }

    // unlock mutex
    if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        return reply;
    }
    
    return reply;
}

string handle_status_all(string command) {
    string reply;
    
    int n = -1;

    // Argument check
    if(command.size() > 11){
        command.erase(0, 11);
        if(command.empty()){
            reply = "Error: Invalid argument. \n";
            return reply;
        }
        else{
            n = stoi(command);
        }
    }

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        return reply;
    }

    // If no n provided, print status of all jobs
    // If n is provided, print status for jobs submitted in last n seconds

    reply = "";
    for(const auto& pair : job_table){
        bool print_job = false;

        if(n == -1){
            print_job = true;
        }
        else{
            if(difftime(time(nullptr), pair.second.submit_time) <= n){
                print_job = true;
            }
        }

        if(print_job){
            reply += "JobID: " + to_string(pair.first) + " Status: " + pair.second.status;
            if(pair.second.status == "Active"){
                int seconds = difftime(time(nullptr), pair.second.start_time);
                reply += " (running for " + to_string(seconds) + " seconds)";
            }
            else if(pair.second.status == "Queued"){
                reply += " (waiting in job queue)";
            }
            reply += "\n";
        }
    }

    if(reply.empty()){
        reply = "No jobs found.\n";
    }

    // unlock mutex
    if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        return reply;
    }
    
    return reply;
}

string handle_show_active(string command) {
    string reply;
    
    // Argument check
    if(command.size() > 11){
        reply = "Error: SHOW_ACTIVE does not take any arguments. \n";
        return reply;
    }

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        return reply;
    }

    reply = "Active jobs:\n";

    for(const auto& pair : job_table){
        if(pair.second.status == "Active"){
            reply += "JobID " + to_string(pair.first) + "\n";
        }
    }

    // unlock mutex
    if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        return reply;
    }

    return reply;
}

string handle_show_workers(string command) {
    string reply;
    
    // Argument check
    if(command.size() > 12){
        reply = "Error: SHOW_WORKERS does not take any arguments. \n";
        return reply;
    }

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&worker_stats_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        return reply;
    }

    reply = "Worker TID, State, Served:\n";

    for(const auto& pair : worker_pool_stats){
        const WorkerStats& worker = pair.second;
        stringstream ss;
        ss << "0x" << hex << (unsigned long)worker.thread_id;
        reply += ss.str() + " ";
        if(worker.is_idle){
            reply += "idle ";
        }
        else{
            reply += "running JobID " + to_string(worker.current_job_id) + " ";
        }
        reply += "served " + to_string(worker.jobs_served) + "\n";
    }

    // unlock mutex
    if((err = pthread_mutex_unlock(&worker_stats_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        return reply;
    }

    return reply;
}

string handle_show_finished(string command) {
    string reply;
    
    // Argument check
    if(command.size() > 13){
        reply = "Error: show-finished does not take any arguments. \n";
        return reply;
    }

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        return reply;
    }

    reply = "Finished jobs:\n";

    for(const auto& pair : job_table){
        if(pair.second.status == "Finished"){
            reply += "JobID " + to_string(pair.first) + "\n";
        }
    }

    // unlock mutex
    if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        return reply;
    }             

    return reply;
}

string handle_shutdown(string command) {
    string reply;
    
    // Argument check
    if(command.size() > 8){
        reply = "Error: SHUTDOWN does not take any arguments. \n";
        return reply;
    }

    // lock mutex
    int err;
    if((err = pthread_mutex_lock(&shared_state_mutex)) != 0){
        cerr << "Error locking mutex" << endl;
        return reply;
    }

    // set shutting_down variable to true
    shutting_down = true;

    // find how many are running and how many are still queued
    int in_progress = 0, queued = 0, finished = 0;
    for(const auto& job : job_table){
        if(job.second.status == "Active"){
            in_progress++;
        }
        else if(job.second.status == "Queued"){
            queued++;
        }
        else if(job.second.status == "Finished"){
            finished++;
        }
    }

    reply = "Served " + to_string(finished) + " jobs, ";
    reply += to_string(in_progress) + " were running, ";
    reply += to_string(queued) + " were still queued\n";

    // unlock mutex
    if((err = pthread_mutex_unlock(&shared_state_mutex)) != 0){
        cerr << "Error unlocking mutex" << endl;
        return reply;
    }

    // wake up all worker threads
    if((err = pthread_cond_broadcast(&available_job_exists)) != 0){
        cerr << "Error broadcasting condition variable" << endl;
        return reply;
    }

    // make main thread stop accepting new connections
    shutdown(server_socket, SHUT_RDWR); // shutdown the socket to unblock accept()
    
    return reply;
}