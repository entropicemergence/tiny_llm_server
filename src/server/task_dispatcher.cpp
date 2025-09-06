#include "task_dispatcher.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <functional>


#define DEBUG_PRINT


#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#else
#define DEBUG_COUT(x)
#endif


std::string build_valid_json_response(const std::string& result) {  // Format as JSON response, Escape quotes in the result for proper JSON
    std::string escaped_result = result;
    size_t pos = 0;
    while ((pos = escaped_result.find("\"", pos)) != std::string::npos) {
        escaped_result.replace(pos, 1, "\\\"");
            pos += 2;
    }
    return "{\"result\": \"" + escaped_result + "\"}";
}


TaskDispatcher::TaskDispatcher() : should_stop_monitoring(false) {
    ipc_manager = std::make_unique<IPCManager>(true);  // true = server mode
    worker_manager = std::make_unique<WorkerManager>("./build_linux/worker", 2, 4);  // min=2, max=4 workers
}

TaskDispatcher::~TaskDispatcher() {
    // stop_monitor_thread();                  // stop all monitoring first bfore dealocating worker resources.
    std::cout << "Cleaning up task dispatcher..." << std::endl;
    ipc_manager->request_shutdown();        // Request shutdown of all workers
    worker_manager->print_stats();
    worker_manager.reset();                 // Cleanup worker manager (this will terminate all workers) ??
    std::cout << "Task dispatcher cleanup complete" << std::endl;
}

bool TaskDispatcher::initialize() {
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
    start_monitor_thread();
    std::cout << "Task dispatcher initialized successfully, started with " << worker_manager->get_active_worker_count() << " workers" << std::endl;
    return true;
}



void TaskDispatcher::process_message(std::function<void(const std::string&)> chunk_callback, const std::string& message, int max_tokens) {  
    // Get next available worker in a round-robin fashion
    int assigned_worker = worker_manager->get_next_worker_round_robin();
    if (assigned_worker == -1) {
        chunk_callback("{\"error\": \"No workers available\"}");
        return;
    }

    uint64_t task_id;
    
    // Notify worker manager that we are starting a request for this worker
    worker_manager->on_request_start(assigned_worker);

    // Enqueue the request specifically for the assigned worker
    if (!ipc_manager->enqueue_request(assigned_worker, message, task_id)) {
        worker_manager->on_request_complete(assigned_worker); // Clean up on failure
        chunk_callback("{\"error\": \"Failed to enqueue request - server may be overloaded\"}");
        return;
    }

    DEBUG_COUT("Dispatched task " << task_id << " to worker " << assigned_worker << " (message: \"" << message << "\")");

    // Wait for response from the assigned worker
    std::string result;
    bool success = ipc_manager->wait_for_response(assigned_worker, task_id, result);
    
    // Notify worker manager about request completion
    worker_manager->on_request_complete(assigned_worker);
    
    if (!success) {
        chunk_callback("{\"error\": \"Failed to receive response from worker\"}");
        return;
    }

    DEBUG_COUT("Received response for task " << task_id << " from worker " << assigned_worker << " (result: \"" << result << "\")");
    
    std::string full_response = build_valid_json_response(result);

    // Mock chunking
    size_t chunk_size = 10;
    for (size_t i = 0; i < full_response.length(); i += chunk_size) {
        std::string chunk = full_response.substr(i, chunk_size);
        std::cout << "Sending chunk: " << chunk << std::endl;
        chunk_callback(chunk);
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // simulate delay
    }
}


void TaskDispatcher::monitor_thread_loop() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // std::cout << "\033[50B";
    // for (int i = 0; i < 30; i++) {
    //     std::cout << std::endl;
    // }
    while (!should_stop_monitoring.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!should_stop_monitoring.load()) {
            worker_manager->check_and_scale();
            worker_manager->restart_unhealthy_workers();
            // worker_manager->print_stats();
        }
    }
}

void TaskDispatcher::start_monitor_thread() {

    should_stop_monitoring.store(false);
    monitor_thread = std::make_unique<std::thread>([this]() {
        monitor_thread_loop();
    });
}

void TaskDispatcher::stop_monitor_thread() {
    std::cout << "\033[1A";
    should_stop_monitoring.store(true);
    if (monitor_thread && monitor_thread->joinable()) {
        monitor_thread->join();
    }
    
}