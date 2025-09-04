#include "worker_manager.hpp"
#include <iostream>
#include <algorithm>
#include <thread>
#include <sstream>
#include <filesystem>

#ifndef _WIN32
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#endif

WorkerManager::WorkerManager(const std::string& worker_exec_path, int min_w, int max_w)
    : active_worker_count(0), min_workers(min_w), max_workers(max_w), 
      worker_executable_path(worker_exec_path), pending_requests(0), total_requests_processed(0) {
    
    workers.resize(MAX_WORKERS);
    last_scale_check = std::chrono::steady_clock::now();
    
    std::cout << "WorkerManager initialized: min=" << min_w << ", max=" << max_w 
              << ", executable=" << worker_exec_path << std::endl;
}

WorkerManager::~WorkerManager() {
    cleanup();
}

bool WorkerManager::initialize() {
    // Check if worker executable exists
    if (!std::filesystem::exists(worker_executable_path)) {
        std::cerr << "Worker executable not found: " << worker_executable_path << std::endl;
        std::cerr << "Make sure to build the project first!" << std::endl;
        return false;
    }
    
    std::cout << "Starting initial " << min_workers.load() << " worker processes..." << std::endl;
    
    // Start minimum number of workers
    for (int i = 0; i < min_workers.load() && i < MAX_WORKERS; ++i) {
        if (!spawn_worker(i)) {
            std::cerr << "Failed to spawn initial worker " << i << std::endl;
            cleanup();
            return false;
        }
    }
    
    std::cout << "Successfully started " << active_worker_count.load() << " workers" << std::endl;
    return true;
}

void WorkerManager::cleanup() {
    std::cout << "Cleaning up worker processes..." << std::endl;
    
    // Terminate all active workers
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && is_worker_active(i)) {
            terminate_worker(i);
        }
    }
    
    // Wait a bit for graceful shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Force kill any remaining workers
#ifndef _WIN32
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && workers[i]->pid > 0) {
            kill(workers[i]->pid, SIGKILL);
            waitpid(workers[i]->pid, nullptr, WNOHANG);
        }
    }
#endif
    
    workers.clear();
    active_worker_count.store(0);
    std::cout << "Worker cleanup complete" << std::endl;
}

bool WorkerManager::spawn_worker(int worker_index) {
    if (worker_index < 0 || worker_index >= MAX_WORKERS) {
        std::cerr << "Invalid worker index: " << worker_index << std::endl;
        return false;
    }
    
    if (workers[worker_index] && is_worker_active(worker_index)) {
        std::cout << "Worker " << worker_index << " is already active" << std::endl;
        return true;
    }
    
#ifdef _WIN32
    std::cerr << "Worker spawning not implemented on Windows yet" << std::endl;
    return false;
#else
    std::cout << "Spawning worker " << worker_index << "..." << std::endl;
    
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Failed to fork worker " << worker_index << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    if (pid == 0) {
        // Child process - execute worker
        std::string index_arg = "--index=" + std::to_string(worker_index);
        
        // Redirect stdout/stderr to avoid cluttering server output
        // You can remove this if you want to see worker output
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull != -1) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        
        execl(worker_executable_path.c_str(), "worker", index_arg.c_str(), nullptr);
        
        // If we reach here, exec failed
        std::cerr << "Failed to exec worker: " << strerror(errno) << std::endl;
        _exit(1);
    }
    
    // Parent process - store worker info
    workers[worker_index] = std::make_unique<WorkerInfo>(pid, worker_index);
    active_worker_count.fetch_add(1);
    
    std::cout << "Worker " << worker_index << " spawned with PID " << pid << std::endl;
    
    // Give worker a moment to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return true;
#endif
}

bool WorkerManager::terminate_worker(int worker_index) {
    if (worker_index < 0 || worker_index >= MAX_WORKERS || !workers[worker_index]) {
        return false;
    }
    
#ifndef _WIN32
    pid_t pid = workers[worker_index]->pid;
    if (pid > 0) {
        std::cout << "Terminating worker " << worker_index << " (PID " << pid << ")" << std::endl;
        
        // Send SIGTERM for graceful shutdown
        kill(pid, SIGTERM);
        
        // Wait for worker to exit
        int status;
        if (waitpid(pid, &status, WNOHANG) == 0) {
            // Worker hasn't exited yet, give it a moment
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (waitpid(pid, &status, WNOHANG) == 0) {
                // Force kill
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
        }
        
        std::cout << "Worker " << worker_index << " terminated" << std::endl;
    }
#endif
    
    workers[worker_index].reset();
    active_worker_count.fetch_sub(1);
    return true;
}

void WorkerManager::on_request_start(int worker_index) {
    pending_requests.fetch_add(1);
    
    if (worker_index >= 0 && worker_index < MAX_WORKERS && workers[worker_index]) {
        workers[worker_index]->is_active.store(true);
        update_worker_activity(worker_index);
    }
}

void WorkerManager::on_request_complete(int worker_index) {
    pending_requests.fetch_sub(1);
    total_requests_processed.fetch_add(1);
    
    if (worker_index >= 0 && worker_index < MAX_WORKERS && workers[worker_index]) {
        workers[worker_index]->is_active.store(false);
        workers[worker_index]->tasks_processed.fetch_add(1);
        update_worker_activity(worker_index);
    }
}

int WorkerManager::get_next_worker() {
    static std::atomic<int> round_robin_counter(0);
    
    // Find next active worker using round-robin
    int start_idx = round_robin_counter.fetch_add(1) % MAX_WORKERS;
    
    for (int i = 0; i < MAX_WORKERS; ++i) {
        int worker_idx = (start_idx + i) % MAX_WORKERS;
        if (is_worker_active(worker_idx)) {
            return worker_idx;
        }
    }
    
    // No active workers found - this shouldn't happen if we maintain min_workers
    std::cerr << "Warning: No active workers found!" << std::endl;
    return -1;
}

void WorkerManager::check_and_scale() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_scale_check < SCALE_CHECK_INTERVAL) {
        return;  // Too soon to check again
    }
    
    last_scale_check = now;
    
    int current_workers = active_worker_count.load();
    int current_pending = pending_requests.load();
    
    std::cout << "Scaling check: " << current_workers << " workers, " 
              << current_pending << " pending requests" << std::endl;
    
    // Scale up if needed
    if (should_scale_up() && current_workers < max_workers.load()) {
        // Find next available worker slot
        for (int i = 0; i < MAX_WORKERS; ++i) {
            if (!is_worker_active(i)) {
                std::cout << "Scaling up: adding worker " << i << std::endl;
                spawn_worker(i);
                break;
            }
        }
    }
    
    // Scale down if needed
    else if (should_scale_down() && current_workers > min_workers.load()) {
        // Find an idle worker to terminate
        auto now = std::chrono::steady_clock::now();
        for (int i = MAX_WORKERS - 1; i >= 0; --i) {  // Start from highest index
            if (workers[i] && is_worker_active(i) && !workers[i]->is_active.load()) {
                auto idle_time = now - workers[i]->last_activity;
                if (idle_time > WORKER_IDLE_TIMEOUT) {
                    std::cout << "Scaling down: removing idle worker " << i << std::endl;
                    terminate_worker(i);
                    break;
                }
            }
        }
    }
}

bool WorkerManager::is_worker_healthy(int worker_index) {
    if (worker_index < 0 || worker_index >= MAX_WORKERS || !workers[worker_index]) {
        return false;
    }
    
#ifndef _WIN32
    pid_t pid = workers[worker_index]->pid;
    if (pid <= 0) return false;
    
    // Check if process is still alive
    int result = kill(pid, 0);  // Signal 0 just checks if process exists
    return result == 0;
#else
    return true;  // Simplified for Windows
#endif
}

void WorkerManager::restart_unhealthy_workers() {
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && !is_worker_healthy(i)) {
            std::cout << "Restarting unhealthy worker " << i << std::endl;
            terminate_worker(i);
            if (active_worker_count.load() < min_workers.load()) {
                spawn_worker(i);
            }
        }
    }
}

void WorkerManager::print_stats() const {
    std::cout << "\n=== Worker Manager Stats ===" << std::endl;
    std::cout << "Active workers: " << active_worker_count.load() << "/" << max_workers.load() << std::endl;
    std::cout << "Pending requests: " << pending_requests.load() << std::endl;
    std::cout << "Total processed: " << total_requests_processed.load() << std::endl;
    
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && is_worker_active(i)) {
            std::cout << "  Worker " << i << ": PID=" << workers[i]->pid 
                      << ", processed=" << workers[i]->tasks_processed.load()
                      << ", active=" << (workers[i]->is_active.load() ? "yes" : "no") << std::endl;
        }
    }
    std::cout << "========================\n" << std::endl;
}

// Private helper methods
bool WorkerManager::is_worker_active(int worker_index) const {
    return worker_index >= 0 && worker_index < MAX_WORKERS && 
           workers[worker_index] && workers[worker_index]->pid > 0;
}

void WorkerManager::update_worker_activity(int worker_index) {
    if (worker_index >= 0 && worker_index < MAX_WORKERS && workers[worker_index]) {
        workers[worker_index]->last_activity = std::chrono::steady_clock::now();
    }
}

bool WorkerManager::should_scale_up() const {
    int current_pending = pending_requests.load();
    int current_workers = active_worker_count.load();
    
    // Scale up if we have too many pending requests relative to workers
    return current_pending > SCALE_UP_THRESHOLD && 
           current_workers < max_workers.load();
}

bool WorkerManager::should_scale_down() const {
    int current_workers = active_worker_count.load();
    int current_pending = pending_requests.load();
    
    // Scale down if we have very few pending requests and more than minimum workers
    return current_pending < SCALE_DOWN_THRESHOLD && 
           current_workers > min_workers.load() &&
           count_idle_workers() > 1;  // Keep at least one worker busy
}

int WorkerManager::count_idle_workers() const {
    int idle_count = 0;
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && is_worker_active(i) && !workers[i]->is_active.load()) {
            idle_count++;
        }
    }
    return idle_count;
}
