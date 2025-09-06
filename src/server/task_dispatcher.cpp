#include "task_dispatcher.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <functional>
#include <sstream>
#include <iomanip>


// #define DEBUG_PRINT


#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#else
#define DEBUG_COUT(x)
#endif


std::string build_json_response_chunk(const std::string &s, bool is_last) {
    std::ostringstream o;
    for (auto c = s.cbegin(); c != s.cend(); c++) {
        switch (*c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= *c && *c <= '\x1f') {
                    o << "\\u"
                      << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
                } else {
                    o << *c;
                }
        }
    }
    std::stringstream json_chunk;
    json_chunk << "{\"chunk\": \"" << o.str() << "\", \"is_last\": " << (is_last ? "true" : "false") << "}";
    return json_chunk.str();
}

TaskDispatcher::TaskDispatcher() : should_stop_monitoring(false) {
    ipc_manager = std::make_unique<IPCManager>(true);  // true = server mode
    worker_manager = std::make_unique<WorkerManager>(ipc_manager.get(), "./build_linux/worker", 2, 4);  // min=2, max=4 workers
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
    if (!ipc_manager->enqueue_request(assigned_worker, message, task_id)) {
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

        std::string escaped_chunk_json_data = build_json_response_chunk(chunk_data, is_last);

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