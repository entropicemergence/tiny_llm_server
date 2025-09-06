#pragma once

#include "../ipc/ipc_utils.hpp"
#include "worker_manager.hpp"
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>

// Task dispatcher manages worker pool and task assignment
class TaskDispatcher {
private:
    std::unique_ptr<IPCManager> ipc_manager;
    std::unique_ptr<WorkerManager> worker_manager;
    
    // Background monitoring thread
    std::unique_ptr<std::thread> monitor_thread;
    std::atomic<bool> should_stop_monitoring;
public:
    TaskDispatcher();
    ~TaskDispatcher();

    // Initialize the dispatcher and shared memory
    bool initialize();

    void process_message(std::function<void(const std::string&)> chunk_callback, const std::string& message, int max_tokens);
    void stop_monitor_thread();
    void start_monitor_thread();
    void monitor_thread_loop();
private:

};