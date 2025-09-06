#include "../ipc/ipc_utils.hpp"
#include "worker_process.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <signal.h>
#include <unistd.h>

// Global variables for signal handling
static volatile bool keep_running = true;
static IPCManager* g_ipc_manager = nullptr;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    keep_running = false;
}


std::string produce_chunked_response(IPCManager& ipc_manager, int worker_index, ReqSlot& request){
    // Process the message
    std::string input(request.data, request.len);
    std::string result = WorkerProcess::process_message(input);
    ipc_manager.send_response(worker_index, request.task_id, result);
    return result;
}


int main(int argc, char* argv[]) {  // argc is argument count(including program name). argv is argument vector. format : executable --index=process_index
    std::string arg = argv[1];
    int worker_index = std::atoi(arg.substr(8).c_str());   // extract process index from argument vector

    // // Set up signal handlers
    // signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize IPC
    IPCManager ipc_manager(false, worker_index);  // false = not server
    g_ipc_manager = &ipc_manager;
    
    if (!ipc_manager.initialize()) {
        std::cerr << "Failed to initialize IPC" << std::endl;
        return 1;
    }
    
    std::cout << "Worker #" << worker_index << " initialized, waiting for tasks..." << std::endl;
    
    // Main worker loop
    ReqSlot request;
    int processed_count = 0;
    
    while (keep_running && !ipc_manager.is_shutdown_requested()) {
        // Try to dequeue a request from this worker's queue
        if (!ipc_manager.dequeue_request(worker_index, request)) {
            if (ipc_manager.is_shutdown_requested()) {
                std::cout << "Shutdown requested, worker " << worker_index << " exiting..." << std::endl;
                break;
            }
            // An error or signal interruption occurred, loop and retry
            continue;
        }
        
        std::cout << "Worker #" << worker_index << " processing task " << request.task_id 
                  << " (message: \"" << std::string(request.data, request.len) << "\")" << std::endl;
        

        std::string result = produce_chunked_response(ipc_manager, worker_index, request);
        processed_count++;
        std::cout << "Worker #" << worker_index << " completed task " << request.task_id 
                  << " (result: \"" << result << "\")" << std::endl;
    }
    
    std::cout << "Worker #" << worker_index << " processed " << processed_count 
              << " tasks. Shutting down..." << std::endl;
    
    g_ipc_manager = nullptr;
    return 0;
}
