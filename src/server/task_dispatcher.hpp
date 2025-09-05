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
    std::atomic<bool> task_dispatcher_initialized_state ;
    
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
    std::string process_message(const std::string& message, int max_tokens);
    
    // Check if dispatcher is ready
    bool is_ready() const { return task_dispatcher_initialized_state.load(); }
    
    // Request graceful shutdown of all workers
    void shutdown();

    // Worker management
    void print_worker_stats() const;

private:
    // Monitoring thread methods
    void start_monitor_thread();
    void stop_monitor_thread();
    void monitor_thread_loop();
};
