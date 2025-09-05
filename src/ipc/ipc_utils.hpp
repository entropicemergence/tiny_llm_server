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
    SharedMem* shm_ptr;
    int shm_fd;
    
    // Semaphores
    sem_t* sem_req_items[MAX_WORKERS]; // One per worker queue
    sem_t* sem_req_space[MAX_WORKERS]; // One per worker queue
    sem_t* sem_resp[MAX_WORKERS];      // One per worker
    
    bool is_server;
    int worker_index; // Only used by worker

public:
    IPCManager(bool server = true, int worker_idx = -1);
    ~IPCManager();
    
    // Initialize shared memory and semaphores
    bool initialize();
    
    // Cleanup resources
    void cleanup();
    
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
    SharedMem* get_shared_mem() const { return shm_ptr; }
};
