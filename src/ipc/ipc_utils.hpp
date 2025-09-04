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
    sem_t* sem_req_items;   // Signals available requests
    sem_t* sem_req_space;   // Signals space in ring
    sem_t* sem_resp[MAX_WORKERS];  // Response semaphores (one per worker)
    
    bool is_server;         // True if this is the server process
    int worker_index;       // Worker index (-1 for server)

public:
    IPCManager(bool server = true, int worker_idx = -1);
    ~IPCManager();
    
    // Initialize shared memory and semaphores
    bool initialize();
    
    // Cleanup resources
    void cleanup();
    
    // Server operations
    bool enqueue_request(const std::string& message, uint64_t& task_id, int& assigned_worker);
    bool wait_for_response(int worker_idx, uint64_t task_id, std::string& result);
    
    // Worker operations
    bool dequeue_request(ReqSlot& slot);
    bool send_response(int worker_idx, uint64_t task_id, const std::string& result);
    
    // Utility functions
    bool is_shutdown_requested() const;
    void request_shutdown();
    uint64_t get_next_task_id();
    
    // Getters
    SharedMem* get_shared_mem() const { return shm_ptr; }
};
