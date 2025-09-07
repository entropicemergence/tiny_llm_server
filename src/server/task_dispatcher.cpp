#include "task_dispatcher.hpp"
#include "../utils/http_utils.hpp"
#include "../utils/config.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <functional>


// #define DEBUG_PRINT


#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#else
#define DEBUG_COUT(x)
#endif


TaskDispatcher::TaskDispatcher() : should_stop_monitoring(false) {
    auto& config = AppConfig::get_instance();
    std::string worker_path = config.get_string("WORKER_EXECUTABLE_PATH", "./build/worker");
    int min_workers = config.get_int("MIN_WORKERS", 2);
    int max_workers = config.get_int("MAX_WORKERS_DYNAMIC", 4);
    max_workers = std::min(max_workers, static_cast<int>(MAX_WORKERS));

    ipc_manager = std::make_unique<IPCManager>(true);  // true = server mode
    worker_manager = std::make_unique<WorkerManager>(ipc_manager.get(), worker_path, min_workers, max_workers);
}

TaskDispatcher::~TaskDispatcher() {
    // stop_monitor_thread();                  // stop all monitoring first bfore dealocating worker resources.
    std::cout << "Cleaning up task dispatcher..." << std::endl;
    ipc_manager->request_shutdown();        // Request shutdown of all workers
    // worker_manager->print_stats();
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



void TaskDispatcher::process_message(std::function<bool(const std::string&)> chunk_callback, const std::string& message, int max_tokens) {  
    // Get next available worker in a round-robin fashion
    int assigned_worker = worker_manager->assign_task_to_worker();
    if (assigned_worker == -1) {
        chunk_callback("{\"error\": \"No workers available\"}");
        return;
    }

    uint64_t task_id;
    
    // Notify worker manager that we are starting a request for this worker
    worker_manager->on_request_start(assigned_worker);

    // Enqueue the request specifically for the assigned worker
    std::string encoded_message = std::to_string(max_tokens) + '\x01' + message;
    if (!ipc_manager->enqueue_request(assigned_worker, encoded_message, task_id)) {
        worker_manager->on_request_complete(assigned_worker); // Clean up on failure
        chunk_callback("{\"error\": \"Failed to enqueue request - server may be overloaded\"}");
        return;
    }

    DEBUG_COUT("Dispatched task " << task_id << " to worker " << assigned_worker << " (message: \"" << message << "\")");

    // Wait for response from the assigned worker
    bool is_last = false;
    bool client_disconnected = false;
    while(!is_last) {
        std::string chunk_data;
        bool success = ipc_manager->wait_for_response_chunk(assigned_worker, task_id, chunk_data, is_last, chunk_callback, client_disconnected);
        
        if (!success) {
            if (!client_disconnected) { // Only send error if client was still connected
                chunk_callback("{\"error\": \"Failed to receive response from worker\"}");
            }
            // ipc_manager->cancel_request(assigned_worker, task_id);
            break;
        }

        if(client_disconnected) {
            continue; // Client is gone, just drain the queue until the worker is done
        }

        std::string escaped_chunk_json_data = HttpUtils::build_json_response_chunk(chunk_data, is_last);

        DEBUG_COUT("Received chunk for task " << task_id << " from worker " << assigned_worker << " (chunk: \"" << escaped_chunk_json_data << "\")");
        if (!chunk_callback(escaped_chunk_json_data)) {
            client_disconnected = true;
            DEBUG_COUT("Client disconnected for task " << task_id << ". Attempting to cancel.");
            // ipc_manager->cancel_request(assigned_worker, task_id);
            // We will now continue the loop to drain remaining chunks from the worker,
            // in case cancellation was too late and the worker is already processing.
        }
    }
    
    // Notify worker manager about request completion
    worker_manager->on_request_complete(assigned_worker);
}


void TaskDispatcher::monitor_thread_loop() {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "\033[50B";
    for (int i = 0; i < 30; i++) {
        std::cout << std::endl;
    }
    while (!should_stop_monitoring.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!should_stop_monitoring.load()) {
            worker_manager->check_and_scale();
            worker_manager->restart_unhealthy_workers();
            worker_manager->print_stats();
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