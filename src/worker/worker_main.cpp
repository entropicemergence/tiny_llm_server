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
    std::cout << "\nWorker received signal " << signal << ", shutting down..." << std::endl;
    keep_running = false;
    if (g_ipc_manager) {
        // Note: This is not fully signal-safe, but good enough for this demo
        g_ipc_manager->request_shutdown();
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " --index=<worker_index>" << std::endl;
    std::cout << "  worker_index: 0 to " << (MAX_WORKERS - 1) << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    if (argc != 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string arg = argv[1];
    if (arg.find("--index=") != 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    int worker_index = std::atoi(arg.substr(8).c_str());
    if (worker_index < 0 || worker_index >= MAX_WORKERS) {
        std::cerr << "Error: worker_index must be between 0 and " << (MAX_WORKERS - 1) << std::endl;
        return 1;
    }
    
    std::cout << "Starting Worker #" << worker_index << std::endl;
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
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
        // Try to dequeue a request
        if (!ipc_manager.dequeue_request(request)) {
            if (ipc_manager.is_shutdown_requested()) {
                std::cout << "Shutdown requested, exiting..." << std::endl;
                break;
            }
            // Error occurred, but continue trying
            std::cerr << "Failed to dequeue request, retrying..." << std::endl;
            usleep(100000);  // Sleep 100ms before retrying
            continue;
        }
        
        std::cout << "Worker #" << worker_index << " processing task " << request.task_id 
                  << " (message: \"" << std::string(request.data, request.len) << "\")" << std::endl;
        
        // Process the message
        std::string input(request.data, request.len);
        std::string result = WorkerProcess::process_message(input);
        
        // Send response back
        if (!ipc_manager.send_response(worker_index, request.task_id, result)) {
            std::cerr << "Failed to send response for task " << request.task_id << std::endl;
            continue;
        }
        
        processed_count++;
        std::cout << "Worker #" << worker_index << " completed task " << request.task_id 
                  << " (result: \"" << result << "\")" << std::endl;
    }
    
    std::cout << "Worker #" << worker_index << " processed " << processed_count 
              << " tasks. Shutting down..." << std::endl;
    
    g_ipc_manager = nullptr;
    return 0;
}
