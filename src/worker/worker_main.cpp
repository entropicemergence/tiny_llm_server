#include "../ipc/ipc_utils.hpp"
#include "../llm/tiny_llm_inference.hpp"
#include <iostream>
#include <string>
#include <cstdlib>
#include <signal.h>



#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#define DEBUG_CERR(x) std::cerr << x << std::endl
#else
#define DEBUG_COUT(x)
#define DEBUG_CERR(x)
#endif

// Global variables for signal handling
static volatile bool keep_running = true;
static IPCManager* g_ipc_manager = nullptr;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
    keep_running = false;
}



void llm_process_and_send_chunked_response(IPCManager& ipc_manager, int worker_index, ReqSlot& request, TinyLLM& llm){
    std::string payload(request.data, request.len);
    size_t separator_pos = payload.find('\x01');
    if (separator_pos == std::string::npos) {
        DEBUG_CERR("Worker " << worker_index << " could not find separator in payload for task " << request.task_id << std::endl);
        ipc_manager.signal_request_handled(worker_index);
        return;
    }

    int max_tokens;
    std::string current_input;
    try {
        max_tokens = std::stoi(payload.substr(0, separator_pos));
        current_input = payload.substr(separator_pos + 1);
    } catch (const std::exception& e) {
        DEBUG_CERR("Worker " << worker_index << " failed to parse payload for task " << request.task_id << ": " << e.what() << std::endl);
        ipc_manager.signal_request_handled(worker_index);
        return;
    }

    ipc_manager.send_response_chunk(worker_index, request.task_id, current_input, false); // optional, send back the promt
    
    if (max_tokens > 50) {
        max_tokens = 50;
    }

    
    llm.init(current_input);
    const int eos_token_id = 3;
    int generated_tokens = 0;
    int next_token = -1; // Start with -1 to indicate first inference

    while (generated_tokens < max_tokens) { // for now the model only supports max 50 tokens
        next_token = llm.inference(next_token);
        if (next_token == eos_token_id) {
            if (!ipc_manager.send_response_chunk(worker_index, request.task_id, "", true)) {
                DEBUG_CERR("Worker " << worker_index << " failed to send final EOS response chunk for task " << request.task_id << std::endl);
            }
            break;
        }

        bool is_last_iteration = (generated_tokens == max_tokens - 1);
        std::string result_piece = llm.decode(next_token);

        if (!ipc_manager.send_response_chunk(worker_index, request.task_id, result_piece, is_last_iteration)) {
            DEBUG_CERR("Worker " << worker_index << " failed to send response chunk for task " << request.task_id << std::endl);
            ipc_manager.signal_request_handled(worker_index);
            return;
        }
        if (ipc_manager.is_shutdown_requested()){
            return;
        }
        generated_tokens++;
    }
    ipc_manager.signal_request_handled(worker_index);
}



int main(int argc, char* argv[]) {  // argc is argument count(including program name). argv is argument vector. format : executable --index=process_index
    TinyLLM llm;

    std::string arg = argv[1];
    int worker_index = std::atoi(arg.substr(8).c_str());   // extract process index from argument vector

    // // Set up signal handlers
    // signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize IPC
    IPCManager ipc_manager(false, worker_index);  // false = not server
    g_ipc_manager = &ipc_manager;
    
    if (!ipc_manager.initialize()) {
        DEBUG_CERR("Failed to initialize IPC" << std::endl);
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
                DEBUG_COUT("Shutdown requested, worker " << worker_index << " exiting..." << std::endl);
                break;
            }
            // An error or signal interruption occurred, loop and retry
            continue;
        }
        
        // Check if the task has been canceled by the server
        if (request.is_canceled.load()) {
            DEBUG_COUT("Worker #" << worker_index << " skipping canceled task " << request.task_id);
            ipc_manager.signal_request_handled(worker_index); // Still need to signal that we are done with this slot
            continue;
        }

        DEBUG_COUT("Worker #" << worker_index << " processing task " << request.task_id << " (message: \"" << std::string(request.data, request.len) << "\")" << std::endl);
        
        llm_process_and_send_chunked_response(ipc_manager, worker_index, request, llm);
        processed_count++;
        DEBUG_COUT("Worker #" << worker_index << " completed task " << request.task_id << std::endl);
    }
    
    DEBUG_COUT("Worker #" << worker_index << " processed " << processed_count << " tasks. Shutting down..." << std::endl);
    
    g_ipc_manager = nullptr;
    return 0;
}
