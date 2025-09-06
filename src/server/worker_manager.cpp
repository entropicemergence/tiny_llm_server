#include "worker_manager.hpp"
#include "../ipc/ipc_utils.hpp"
#include "../ipc/shared_mem.hpp"
#include <iostream>
#include <thread>
#include <filesystem>
#include <iomanip>

#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <signal.h>

// #define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#else
#define DEBUG_COUT(x)
#endif


WorkerManager::WorkerManager(IPCManager* ipc, const std::string& worker_exec_path, int min_w, int max_w)
    : ipc_manager(ipc), active_worker_count(0), min_workers(min_w), max_workers(max_w), 
      worker_executable_path(worker_exec_path), pending_requests(0), total_requests_processed(0) {
    std::cout << "Building WorkerManager with: min=" << min_w << ", max=" << max_w 
              << ", executable=" << worker_exec_path << std::endl;
    workers.resize(MAX_WORKERS);
    last_scale_check = std::chrono::steady_clock::now();
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
    
    std::cout << "WorkerManager successfully started " << active_worker_count.load() << " workers" << std::endl;
    return true;
}

void WorkerManager::cleanup() {
    std::cout << "Cleaning up worker processes..." << std::endl;
    
    // Terminate all active workers
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && is_worker_deployed(i)) {
            terminate_worker(i);
        }
    }
    
    // Wait a bit for graceful shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Force kill any remaining workers
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && workers[i]->pid > 0) {
            kill(workers[i]->pid, SIGKILL);
            waitpid(workers[i]->pid, nullptr, WNOHANG);
        }
    }
    
    workers.clear();
    active_worker_count.store(0);
    std::cout << "Worker cleanup complete" << std::endl;
}

bool WorkerManager::spawn_worker(int worker_index) {
    if (worker_index < 0 || worker_index >= MAX_WORKERS) {
        DEBUG_COUT("Invalid worker index: " << worker_index);
        return false;
    }
    
    if (workers[worker_index] && is_worker_deployed(worker_index)) {
        DEBUG_COUT("Worker " << worker_index << " is already active");
        return true;
    }
    
    DEBUG_COUT("Spawning worker " << worker_index << "...");
    
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
    
    DEBUG_COUT("Worker " << worker_index << " spawned with PID " << pid);
    
    // Give worker a moment to initialize
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return true;
}

bool WorkerManager::terminate_worker(int worker_index) {
    if (worker_index < 0 || worker_index >= MAX_WORKERS || !workers[worker_index]) {
        return false;
    }
    
    pid_t pid = workers[worker_index]->pid;
    if (pid > 0) {
        DEBUG_COUT("Terminating worker " << worker_index << " (PID " << pid << ")");

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

        DEBUG_COUT("Worker " << worker_index << " terminated");
    }
    
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

int WorkerManager::find_least_loaded_worker() {
    int least_loaded_worker = -1;
    int min_queue_size = -1;

    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (is_worker_deployed(i)) {
            int current_queue_size = 0;
            if (ipc_manager->get_request_queue_size(i, current_queue_size)) {
                if (least_loaded_worker == -1 || current_queue_size < min_queue_size) {
                    min_queue_size = current_queue_size;
                    least_loaded_worker = i;
                }
            }
        }
    }
    return least_loaded_worker;
}

int WorkerManager::assign_task_to_worker() {
    // 1. Find an idle worker (round-robin for fairness)
    static std::atomic<int> round_robin_counter(0);
    for (int i = 0; i < MAX_WORKERS; ++i) {
        int worker_idx = round_robin_counter.fetch_add(1) % MAX_WORKERS;
        if (is_worker_deployed(worker_idx) && !workers[worker_idx]->is_active.load()) {
            return worker_idx;
        }
    }

    // 2. If no idle workers, try to scale up
    if (active_worker_count.load() < max_workers.load()) {
        for (int i = 0; i < MAX_WORKERS; ++i) {
            if (!is_worker_deployed(i)) {
                DEBUG_COUT("Scaling up on demand: adding worker " << i);
                if (spawn_worker(i)) {
                    return i; // Return new worker immediately
                } else {
                    // Failed to spawn, proceed to find least loaded instead.
                    break; 
                }
            }
        }
    }

    // 3. If at max workers or spawn failed, find the least loaded one
    DEBUG_COUT("All workers busy and at max capacity. Finding least loaded worker...");
    return find_least_loaded_worker();
}

void WorkerManager::check_and_scale() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_scale_check < SCALE_CHECK_INTERVAL) {
        return;  // Too soon to check again
    }
    
    last_scale_check = now;
    
    int current_workers = active_worker_count.load();
    int current_pending = pending_requests.load();
    
    DEBUG_COUT("Scaling check: " << current_workers << " workers, " << current_pending << " pending requests");

    // Scale down if needed
    if (should_scale_down()) {
        // Find an idle worker to terminate
        auto now_idle = std::chrono::steady_clock::now();
        for (int i = MAX_WORKERS - 1; i >= 0; --i) {  // Start from highest index
            if (workers[i] && is_worker_deployed(i) && !workers[i]->is_active.load()) {
                auto idle_time = now_idle - workers[i]->last_activity;
                if (idle_time > WORKER_IDLE_TIMEOUT) {
                    DEBUG_COUT("Scaling down: removing idle worker " << i);
                    terminate_worker(i);
                    break;
                }
            }
        }
    }
}

bool WorkerManager::is_worker_healthy(int worker_index) {
    if (!workers[worker_index]) {
        return false;
    }
    
    pid_t pid = workers[worker_index]->pid;
    if (pid <= 0) return false;

    // Check if process is still alive
    int result = kill(pid, 0);  // Signal 0 just checks if process exists
    return result == 0;
}

void WorkerManager::restart_unhealthy_workers() {
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && !is_worker_healthy(i)) {
            DEBUG_COUT("Restarting unhealthy worker " << i);
            terminate_worker(i);
            if (active_worker_count.load() < min_workers.load()) {
                spawn_worker(i);
            }
        }
    }
}





// Private helper methods
bool WorkerManager::is_worker_deployed(int worker_index) const {
    return worker_index >= 0 && worker_index < MAX_WORKERS && 
           workers[worker_index] && workers[worker_index]->pid > 0;
}

void WorkerManager::update_worker_activity(int worker_index) {
    if (worker_index >= 0 && worker_index < MAX_WORKERS && workers[worker_index]) {
        workers[worker_index]->last_activity = std::chrono::steady_clock::now();
    }
}

bool WorkerManager::should_scale_down() const {
    int current_workers = active_worker_count.load();
    int current_pending = pending_requests.load();
    
    // Scale down if we have very few pending requests and more than minimum workers
    return current_pending < SCALE_DOWN_THRESHOLD && 
           count_idle_workers() > 1;  // Keep at least one worker busy
}

int WorkerManager::count_idle_workers() const {
    int idle_count = 0;
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && is_worker_deployed(i) && !workers[i]->is_active.load()) {
            idle_count++;
        }
    }
    return idle_count;
}



void WorkerManager::print_stats() const {
    // ANSI color codes
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string RED = "\033[31m";
    const std::string BLUE = "\033[34m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    const std::string BG_BLUE = "\033[44m";
    const std::string BG_GREEN = "\033[42m";
    const std::string BG_RED = "\033[41m";

    std::cout << "\033[27A";
        
    std::cout << BG_BLUE << WHITE << BOLD << "  WORKER TASK MANAGER                                                             " << RESET << std::endl;
    std::cout << std::endl;
    
    // System overview
    int active_count = active_worker_count.load();
    int max_count = max_workers.load();
    int pending_count = pending_requests.load();
    int total_processed = total_requests_processed.load();
    
    // Calculate utilization
    double worker_utilization = (active_count > 0) ? ((double)active_count / max_count * 100.0) : 0.0;
    
    std::cout << CYAN << BOLD << "┌─ SYSTEM OVERVIEW " << std::setfill('-') << std::setw(62) << "-┐" << RESET << std::endl;
    std::cout << CYAN << "│" << RESET;
    std::cout << " Workers Active: " << GREEN << BOLD << std::setfill(' ') << std::setw(3) << active_count 
              << WHITE << "/" << max_count << RESET;
    
    // Worker utilization bar
    std::cout << " [";
    int bar_width = 20;
    int filled = (int)(worker_utilization / 100.0 * bar_width);
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) {
            if (worker_utilization > 80) std::cout << RED << "█" << RESET;
            else if (worker_utilization > 50) std::cout << YELLOW << "█" << RESET;
            else std::cout << GREEN << "█" << RESET;
        } else {
            std::cout << "░";
        }
    }
    std::cout << "] " << std::fixed << std::setprecision(1) << worker_utilization << "%";
    std::cout << CYAN << " │" << RESET << std::endl;
    
    std::cout << CYAN << "│" << RESET;
    std::cout << " Pending Queue: " << YELLOW << BOLD << std::setfill(' ') << std::setw(8) << pending_count << RESET;
    std::cout << "                                      ";
    std::cout << CYAN << " │" << RESET << std::endl;
    
    std::cout << CYAN << "│" << RESET;
    std::cout << " Total Processed: " << GREEN << BOLD << std::setfill(' ') << std::setw(8) << total_processed << RESET;
    std::cout << "                                    ";
    std::cout << CYAN << " │" << RESET << std::endl;
    
    std::cout << CYAN << "└" << std::setfill('-') << std::setw(78) << "-┘" << RESET << std::endl;
    std::cout << std::endl;
    
    // Worker details table
    std::cout << CYAN << BOLD << "┌─ WORKER PROCESSES " << std::setfill('-') << std::setw(59) << "-┐" << RESET << std::endl;
    std::cout << CYAN << "│" << RESET << BOLD << " ID │   PID   │  STATUS  │ TASKS │ UPTIME │ ACTIVITY        │" << RESET << CYAN << " │" << RESET << std::endl;
    std::cout << CYAN << "├" << std::setfill('-') << std::setw(4) << "-┼" << std::setw(9) << "-┼" << std::setw(10) << "-┼" 
              << std::setw(7) << "-┼" << std::setw(8) << "-┼" << std::setw(17) << "-┤" << RESET << std::endl;
    
    // Display worker information
    for (int i = 0; i < MAX_WORKERS; ++i) {
        std::cout << CYAN << "│" << RESET;
        
        if (workers[i] && is_worker_deployed(i)) {
            // Worker ID
            std::cout << " " << BLUE << BOLD << std::setfill(' ') << std::setw(2) << i << RESET << " │";
            
            // PID
            std::cout << " " << WHITE << std::setfill(' ') << std::setw(7) << workers[i]->pid << RESET << " │";
            
            // Status with color
            bool is_processing = workers[i]->is_active.load();
            if (is_processing) {
                std::cout << " " << BG_GREEN << WHITE << " ACTIVE " << RESET << "  │";
            } else {
                std::cout << " " << YELLOW << " IDLE  " << RESET << "  │";
            }
            
            // Tasks processed
            std::cout << " " << GREEN << std::setfill(' ') << std::setw(5) << workers[i]->tasks_processed.load() << RESET << " │";
            
            // Uptime calculation
            auto uptime = std::chrono::steady_clock::now() - workers[i]->last_activity;
            auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
            std::cout << " " << WHITE << std::setfill(' ') << std::setw(6);
            if (uptime_seconds < 60) {
                std::cout << uptime_seconds << "s" << RESET << " │";
            } else if (uptime_seconds < 3600) {
                std::cout << uptime_seconds/60 << "m" << RESET << " │";
            } else {
                std::cout << uptime_seconds/3600 << "h" << RESET << " │";
            }
            
            // Activity indicator with animation
            std::cout << " ";
            if (is_processing) {
                // Animated processing indicator
                static int anim_frame = 0;
                const char* frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
                std::cout << GREEN << frames[anim_frame % 10] << " Processing..." << RESET;
                anim_frame++;
            } else {
                std::cout << BLUE << "● Waiting      " << RESET;
            }
            std::cout << " │" << CYAN << " │" << RESET << std::endl;
        } else {
            // Empty worker slot
            std::cout << " " << std::setfill(' ') << std::setw(2) << i << " │";
            std::cout << " " << RED << "   ---   " << RESET << " │";
            std::cout << " " << RED << " OFFLINE " << RESET << " │";
            std::cout << " " << RED << "  --- " << RESET << " │";
            std::cout << " " << RED << "  --- " << RESET << " │";
            std::cout << " " << RED << "● Not started   " << RESET << " │" << CYAN << " │" << RESET << std::endl;
        }
    }
    
    std::cout << CYAN << "└" << std::setfill('-') << std::setw(78) << "-┘" << RESET << std::endl;
    
    // Performance metrics
    std::cout << std::endl;
    std::cout << CYAN << BOLD << "┌─ PERFORMANCE METRICS " << std::setfill('-') << std::setw(56) << "-┐" << RESET << std::endl;
    std::cout << CYAN << "│" << RESET;
    
    // Calculate average tasks per worker
    double avg_tasks = 0.0;
    int active_workers_with_tasks = 0;
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (workers[i] && is_worker_deployed(i)) {
            int tasks = workers[i]->tasks_processed.load();
            if (tasks > 0) {
                avg_tasks += tasks;
                active_workers_with_tasks++;
            }
        }
    }
    if (active_workers_with_tasks > 0) {
        avg_tasks /= active_workers_with_tasks;
    }
    
    std::cout << " Avg Tasks/Worker: " << GREEN << std::fixed << std::setprecision(1) << avg_tasks << RESET;
    std::cout << "   Queue Load: ";
    if (pending_count == 0) {
        std::cout << GREEN << "LOW" << RESET;
    } else if (pending_count < 5) {
        std::cout << YELLOW << "MEDIUM" << RESET;
    } else {
        std::cout << RED << "HIGH" << RESET;
    }
    std::cout << "        ";
    std::cout << CYAN << " │" << RESET << std::endl;
    std::cout << CYAN << "└" << std::setfill('-') << std::setw(78) << "-┘" << RESET << std::endl;
    
    // Footer
    std::cout << std::endl;
    std::cout << WHITE << "Press " << CYAN << "Ctrl+C" << WHITE << " to stop monitoring" << RESET << std::endl;
    std::cout << std::endl;
    
    // Scroll to bottom
    std::cout << std::flush;
    
}