#pragma once

#include "../ipc/shared_mem.hpp"
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>
#include <string>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

// Worker process information
struct WorkerInfo {
    pid_t pid;                          // Process ID
    int worker_index;                   // Worker index (0 to MAX_WORKERS-1)
    std::chrono::steady_clock::time_point last_activity;  // Last time this worker was used
    std::atomic<bool> is_active;        // Whether worker is currently processing
    std::atomic<int> tasks_processed;   // Total tasks processed by this worker
    
    WorkerInfo(pid_t p, int idx) : pid(p), worker_index(idx), is_active(false), tasks_processed(0) {
        last_activity = std::chrono::steady_clock::now();
    }
};

// Manages worker processes - spawning, monitoring, scaling
class WorkerManager {
private:
    std::vector<std::unique_ptr<WorkerInfo>> workers;
    std::atomic<int> active_worker_count;
    std::atomic<int> min_workers;
    std::atomic<int> max_workers;
    std::string worker_executable_path;
    
    // Load monitoring
    std::atomic<int> pending_requests;
    std::atomic<int> total_requests_processed;
    std::chrono::steady_clock::time_point last_scale_check;
    
    // Scaling thresholds
    static constexpr int SCALE_UP_THRESHOLD = 5;    // Scale up if >5 pending requests
    static constexpr int SCALE_DOWN_THRESHOLD = 2;  // Scale down if <2 avg requests per worker
    static constexpr std::chrono::seconds SCALE_CHECK_INTERVAL{10}; // Check every 10 seconds
    static constexpr std::chrono::seconds WORKER_IDLE_TIMEOUT{60};  // Kill idle workers after 60s

public:
    WorkerManager(const std::string& worker_exec_path = "./worker", int min_w = 2, int max_w = MAX_WORKERS);
    ~WorkerManager();
    
    // Initialize and start initial workers
    bool initialize();
    
    // Cleanup all workers
    void cleanup();
    
    // Worker lifecycle management
    bool spawn_worker(int worker_index);
    bool terminate_worker(int worker_index);
    int get_active_worker_count() const { return active_worker_count.load(); }
    
    // Load monitoring and scaling
    void on_request_start(int worker_index);
    void on_request_complete(int worker_index);
    void check_and_scale();
    
    // Get worker for task assignment (round-robin among active workers)
    int get_next_worker_round_robin();
    
    // Health monitoring
    bool is_worker_healthy(int worker_index);
    void restart_unhealthy_workers();
    
    // Statistics
    void print_stats() const;
    // void start_realtime_monitor(int refresh_interval_ms = 1000) const;
    
private:
    // Helper methods
    bool is_worker_deployed(int worker_index) const;
    void update_worker_activity(int worker_index);
    int count_idle_workers() const;
    bool should_scale_up() const;
    bool should_scale_down() const;
};
