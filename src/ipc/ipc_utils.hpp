#pragma once

#include "shared_mem.hpp"
#include <string>
#include <memory>

#ifdef _WIN32
    // Windows implementation would use named pipes/memory mapped files
    // For now, focus on POSIX implementation
    #error "Windows IPC not implemented yet - use POSIX system"
#else
    #include <semaphore.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

// IPC Manager class for handling shared memory and semaphores
class IPCManager {
private:
    SharedMem* shared_mem_ptr;
    int shared_mem_file_descriptor;
    
    // Semaphores, auto increment by sem_post, auto decrement by sem_wait.
    sem_t* sem_request_items[MAX_WORKERS]; // Counts tasks in the queue. Acting as the counter for the number of requests in the queue. decrement by worker, increment by server.
    sem_t* sem_req_space[MAX_WORKERS]; // Counts empty slots in the queue.
    sem_t* sem_resp[MAX_WORKERS];      // Counts responses from the worker.
    
    bool is_server;
    int worker_index; // Only used by worker

public:
    IPCManager(bool server = true, int worker_idx = -1);
    ~IPCManager();
    
    // Initialize shared memory and semaphores
    bool initialize();
        
    // Server operations
    // Enqueue a request for a specific worker
    bool enqueue_request(int worker_idx, const std::string& message, uint64_t& task_id);
    
    // Wait for a response from a specific worker
    bool wait_for_response(int worker_idx, uint64_t task_id, std::string& result);
    
    // Worker operations
    // Dequeue a request for this worker
    bool dequeue_request(int worker_idx, ReqSlot& slot);
    
    // Send a response from this worker
    bool send_response(int worker_idx, uint64_t task_id, const std::string& result);
    
    // Utility functions
    bool is_shutdown_requested() const;
    void request_shutdown();
    uint64_t get_next_task_id();
    
    // Getters
    SharedMem* get_shared_mem() const { return shared_mem_ptr; }
};
