#include "../ipc/ipc_utils.hpp"
#include "simple_worker.hpp"
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



void llm_process_and_send_chunked_response(IPCManager& ipc_manager, int worker_index, ReqSlot& request){
    std::string current_input(request.data, request.len);
    TinyLLM llm;
    llm.init(current_input);
    int max_tokens = 50;
    const int eos_token_id = 3;
    int generated_tokens = 0;
    int next_token = -1; // Start with -1 to indicate first inference

    while (generated_tokens < max_tokens) {
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
        generated_tokens++;
    }
    ipc_manager.signal_request_handled(worker_index);
}

void simple_process_and_send_chunked_response(IPCManager& ipc_manager, int worker_index, ReqSlot& request){
    std::string current_input(request.data, request.len);
    for (int i=0; i<10; ++i) {
        // Process the message, feeding back the result of the previous iteration
        std::string result_piece = WorkerProcess::process_message(current_input);
        
        bool is_last_iteration = (i == 9);

        // Split the single `result_piece` into chunks and send them
        size_t offset = 0;
        if (result_piece.empty() && is_last_iteration) {
            if (!ipc_manager.send_response_chunk(worker_index, request.task_id, "", true)) {
                std::cerr << "Worker " << worker_index << " failed to send final empty response chunk for task " << request.task_id << std::endl;
            }
        } else {
            while(offset < result_piece.length()){
                size_t chunk_size = std::min((size_t)CHUNK_SIZE - 1, result_piece.length() - offset);
                std::string chunk = result_piece.substr(offset, chunk_size);
                offset += chunk_size;

                bool is_last_chunk_of_piece = (offset >= result_piece.length());
                bool is_last_overall = is_last_iteration && is_last_chunk_of_piece;
                
                if (!ipc_manager.send_response_chunk(worker_index, request.task_id, chunk, is_last_overall)) {
                    std::cerr << "Worker " << worker_index << " failed to send response chunk for task " << request.task_id << std::endl;
                    ipc_manager.signal_request_handled(worker_index);
                    return;
                }
            }
        }
        
        current_input = result_piece; // Feedback for the next iteration
    }

    ipc_manager.signal_request_handled(worker_index);
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
        
        DEBUG_COUT("Worker #" << worker_index << " processing task " << request.task_id << " (message: \"" << std::string(request.data, request.len) << "\")" << std::endl);
        
        llm_process_and_send_chunked_response(ipc_manager, worker_index, request);
        // simple_process_and_send_chunked_response(ipc_manager, worker_index, request);
        processed_count++;
        DEBUG_COUT("Worker #" << worker_index << " completed task " << request.task_id << std::endl);
    }
    
    DEBUG_COUT("Worker #" << worker_index << " processed " << processed_count << " tasks. Shutting down..." << std::endl);
    
    g_ipc_manager = nullptr;
    return 0;
}
