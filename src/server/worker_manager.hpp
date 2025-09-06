#pragma once

#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <chrono>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#endif

class IPCManager; // Forward declaration

struct WorkerInfo {
    pid_t pid;
    int index;
    std::chrono::steady_clock::time_point last_activity;
    std::atomic<bool> is_active;
    std::atomic<int> tasks_processed;
    
    WorkerInfo(pid_t p, int idx) : pid(p), index(idx), is_active(false), tasks_processed(0) {
        last_activity = std::chrono::steady_clock::now();
    }
};

class WorkerManager {
public:
    WorkerManager(IPCManager* ipc, const std::string& worker_exec_path, int min_w, int max_w);
    ~WorkerManager();
    
    bool initialize();
    void cleanup();
    
    void on_request_start(int worker_index);
    void on_request_complete(int worker_index);
    
    int assign_task_to_worker();
    void check_and_scale();
    void restart_unhealthy_workers();
    
    int get_active_worker_count() const { return active_worker_count.load(); }
    void print_stats() const;

private:
    std::vector<std::unique_ptr<WorkerInfo>> workers;
    std::atomic<int> active_worker_count;
    std::atomic<int> min_workers;
    std::atomic<int> max_workers;
    std::string worker_executable_path;
    std::atomic<int> pending_requests;
    std::atomic<int> total_requests_processed;
    std::chrono::steady_clock::time_point last_scale_check;

    IPCManager* ipc_manager;

    // Scaling constants
    const int SCALE_UP_THRESHOLD = 2; // This is now unused for scaling up, but kept for context or future use
    const int SCALE_DOWN_THRESHOLD = 0;
    const std::chrono::seconds SCALE_CHECK_INTERVAL{2};
    const std::chrono::seconds WORKER_IDLE_TIMEOUT{10};

    // Private helper methods
    bool spawn_worker(int worker_index);
    bool terminate_worker(int worker_index);
    bool is_worker_deployed(int worker_index) const;
    bool is_worker_healthy(int worker_index);
    void update_worker_activity(int worker_index);
    bool should_scale_down() const;
    int count_idle_workers() const;
    int find_least_loaded_worker();
};
