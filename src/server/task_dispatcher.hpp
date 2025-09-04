#pragma once

#include "../ipc/ipc_utils.hpp"
#include "worker_manager.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

// Task dispatcher manages worker pool and task assignment
class TaskDispatcher {
private:
    std::unique_ptr<IPCManager> ipc_manager;
    std::unique_ptr<WorkerManager> worker_manager;
    std::atomic<bool> initialized;
    
    // Background monitoring thread
    std::unique_ptr<std::thread> monitor_thread;
    std::atomic<bool> should_stop_monitoring;
    
public:
    TaskDispatcher();
    ~TaskDispatcher();
    
    // Initialize the dispatcher and shared memory
    bool initialize();
    
    // Cleanup resources
    void cleanup();
    
    // Process a message by dispatching to a worker
    // Returns the processed result or error message
    std::string process_message(const std::string& message);
    
    // Check if dispatcher is ready
    bool is_ready() const { return initialized.load(); }
    
    // Request graceful shutdown of all workers
    void shutdown();
    
    // Worker management
    void print_worker_stats() const;
};
