#include "task_dispatcher.hpp"
#include <iostream>
#include <chrono>
#include <thread>

TaskDispatcher::TaskDispatcher() : initialized(false), should_stop_monitoring(false) {
    ipc_manager = std::make_unique<IPCManager>(true);  // true = server mode
    worker_manager = std::make_unique<WorkerManager>("./worker", 2, 4);  // min=2, max=4 workers
}

TaskDispatcher::~TaskDispatcher() {
    cleanup();
}

bool TaskDispatcher::initialize() {
    if (initialized.load()) {
        return true;  // Already initialized
    }
    
    std::cout << "Initializing task dispatcher..." << std::endl;
    
    // Initialize IPC first
    if (!ipc_manager->initialize()) {
        std::cerr << "Failed to initialize IPC manager" << std::endl;
        return false;
    }
    
    // Initialize worker manager and spawn initial workers
    if (!worker_manager->initialize()) {
        std::cerr << "Failed to initialize worker manager" << std::endl;
        return false;
    }
    
    // Start background monitoring thread
    should_stop_monitoring.store(false);
    monitor_thread = std::make_unique<std::thread>([this]() {
        while (!should_stop_monitoring.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            if (!should_stop_monitoring.load()) {
                worker_manager->check_and_scale();
                worker_manager->restart_unhealthy_workers();
            }
        }
    });
    
    initialized.store(true);
    std::cout << "Task dispatcher initialized successfully" << std::endl;
    std::cout << "Started with " << worker_manager->get_active_worker_count() << " workers" << std::endl;
    
    return true;
}

void TaskDispatcher::cleanup() {
    if (initialized.load()) {
        std::cout << "Cleaning up task dispatcher..." << std::endl;
        
        // Stop monitoring thread
        should_stop_monitoring.store(true);
        if (monitor_thread && monitor_thread->joinable()) {
            monitor_thread->join();
        }
        
        // Request shutdown of all workers
        ipc_manager->request_shutdown();
        
        // Cleanup worker manager (this will terminate all workers)
        worker_manager.reset();
        
        initialized.store(false);
        std::cout << "Task dispatcher cleanup complete" << std::endl;
    }
}

std::string TaskDispatcher::process_message(const std::string& message) {
    if (!initialized.load()) {
        return "{\"error\": \"Task dispatcher not initialized\"}";
    }
    
    if (message.empty()) {
        return "{\"error\": \"Empty message\"}";
    }
    
    // Check for shutdown
    if (ipc_manager->is_shutdown_requested()) {
        return "{\"error\": \"Server is shutting down\"}";
    }
    
    // Get next available worker
    int assigned_worker = worker_manager->get_next_worker();
    if (assigned_worker == -1) {
        return "{\"error\": \"No workers available\"}";
    }
    
    // Notify worker manager about request start
    worker_manager->on_request_start(assigned_worker);
    
    uint64_t task_id;
    int temp_worker;  // This will be overridden by our assigned_worker logic
    
    // Enqueue the request
    if (!ipc_manager->enqueue_request(message, task_id, temp_worker)) {
        worker_manager->on_request_complete(assigned_worker);  // Clean up on failure
        return "{\"error\": \"Failed to enqueue request - server may be overloaded\"}";
    }
    
    // Override the worker assignment to use our round-robin selection
    assigned_worker = static_cast<int>((task_id - 1) % worker_manager->get_active_worker_count());
    
    std::cout << "Dispatched task " << task_id << " to worker " << assigned_worker 
              << " (message: \"" << message << "\")" << std::endl;
    
    // Wait for response from the assigned worker
    std::string result;
    bool success = ipc_manager->wait_for_response(assigned_worker, task_id, result);
    
    // Notify worker manager about request completion
    worker_manager->on_request_complete(assigned_worker);
    
    if (!success) {
        return "{\"error\": \"Failed to receive response from worker\"}";
    }
    
    std::cout << "Received response for task " << task_id << " from worker " << assigned_worker 
              << " (result: \"" << result << "\")" << std::endl;
    
    // Format as JSON response
    // Escape quotes in the result for proper JSON
    std::string escaped_result = result;
    size_t pos = 0;
    while ((pos = escaped_result.find("\"", pos)) != std::string::npos) {
        escaped_result.replace(pos, 1, "\\\"");
        pos += 2;
    }
    
    return "{\"result\": \"" + escaped_result + "\"}";
}
void TaskDispatcher::shutdown() {
    if (initialized.load()) {
        std::cout << "Requesting shutdown of all workers..." << std::endl;
        ipc_manager->request_shutdown();
        
        // Print final stats
        print_worker_stats();
    }
}

void TaskDispatcher::print_worker_stats() const {
    if (worker_manager) {
        worker_manager->print_stats();
    }
}

