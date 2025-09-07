#include "ipc_utils.hpp"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <sstream>
#include <thread>
#include <chrono>
#include <time.h>


#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#define DEBUG_CERR(x) std::cerr << x << std::endl
#else
#define DEBUG_COUT(x)
#define DEBUG_CERR(x)
#endif


/* -----------------------------------------------------------------Constructor and Destructor section----------------------------------------------------------------------------------*/

IPCManager::IPCManager(bool server, int worker_idx) 
    : shared_mem_ptr(nullptr), shared_mem_file_descriptor(-1), is_server(server), worker_index(worker_idx) {
    std::cout << "Building IPCManager with: server=" << server << ", worker_idx=" << worker_idx << std::endl;
    // Initialize semaphore arrays to null
    for (int i = 0; i < MAX_WORKERS; ++i) {
        sem_request_items[i] = nullptr;
        sem_req_space[i] = nullptr;
        sem_resp[i] = nullptr;
        sem_resp_consumed[i] = nullptr;
    }
}

IPCManager::~IPCManager() {
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (sem_request_items[i] != nullptr && sem_request_items[i] != SEM_FAILED) {
            sem_close(sem_request_items[i]);
            if (is_server) {
                std::ostringstream ss;
                ss << SEM_REQ_ITEMS_PREFIX << i;
                sem_unlink(ss.str().c_str());
            }
        }
        if (sem_req_space[i] != nullptr && sem_req_space[i] != SEM_FAILED) {
            sem_close(sem_req_space[i]);
            if (is_server) {
                std::ostringstream ss;
                ss << SEM_REQ_SPACE_PREFIX << i;
                sem_unlink(ss.str().c_str());
            }
        }
        if (sem_resp[i] != nullptr && sem_resp[i] != SEM_FAILED) {
            sem_close(sem_resp[i]);
            if (is_server) {
                std::ostringstream ss;
                ss << SEM_RESP_PREFIX << i;
                sem_unlink(ss.str().c_str());
            }
        }
        if (sem_resp_consumed[i] != nullptr && sem_resp_consumed[i] != SEM_FAILED) {
            sem_close(sem_resp_consumed[i]);
            if (is_server) {
                std::ostringstream ss;
                ss << SEM_RESP_CONSUMED_PREFIX << i;
                sem_unlink(ss.str().c_str());
            }
        }
    }
    
    if (shared_mem_ptr != nullptr && shared_mem_ptr != MAP_FAILED) {
        munmap(shared_mem_ptr, SHARED_MEM_SIZE);
    }
    
    if (shared_mem_file_descriptor != -1) {
        close(shared_mem_file_descriptor);
        if (is_server) {
            shm_unlink(SHM_NAME);
        }
    }
    std::cout << "IPCManager cleaned up successfully" << std::endl;
}

bool IPCManager::initialize() {
    // On server startup, clean up any orphaned IPC objects from a previous run.
    if (is_server) {
        shm_unlink(SHM_NAME); // Unlink shared memory
        for (int i = 0; i < MAX_WORKERS; ++i) {
            std::ostringstream sem_req_items_name, sem_req_space_name, sem_resp_name, sem_resp_consumed_name;
            sem_req_items_name << SEM_REQ_ITEMS_PREFIX << i;
            sem_req_space_name << SEM_REQ_SPACE_PREFIX << i;
            sem_resp_name << SEM_RESP_PREFIX << i;
            sem_resp_consumed_name << SEM_RESP_CONSUMED_PREFIX << i;
            sem_unlink(sem_req_items_name.str().c_str());
            sem_unlink(sem_req_space_name.str().c_str());
            sem_unlink(sem_resp_name.str().c_str());
            sem_unlink(sem_resp_consumed_name.str().c_str());
        }
    }

    // Create or open shared memory
    if (is_server) {
        shared_mem_file_descriptor = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shared_mem_file_descriptor == -1) {
            std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
            return false;
        }
        if (ftruncate(shared_mem_file_descriptor, SHARED_MEM_SIZE) == -1) {
            std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
            return false;
        }
    } else {
        shared_mem_file_descriptor = shm_open(SHM_NAME, O_RDWR, 0666);
        if (shared_mem_file_descriptor == -1) {
            std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
            return false;
        }
    }
    
    // Map shared memory
    shared_mem_ptr = static_cast<SharedMem*>(mmap(nullptr, SHARED_MEM_SIZE,PROT_READ | PROT_WRITE, MAP_SHARED, shared_mem_file_descriptor, 0));
    if (shared_mem_ptr == MAP_FAILED) {
        std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (is_server) {
        new (shared_mem_ptr) SharedMem();
    }
    
    // Open semaphores
    for (int i = 0; i < MAX_WORKERS; ++i) {
        std::ostringstream sem_req_items_name, sem_req_space_name, sem_resp_name, sem_resp_consumed_name;
        sem_req_items_name << SEM_REQ_ITEMS_PREFIX << i;
        sem_req_space_name << SEM_REQ_SPACE_PREFIX << i;
        sem_resp_name << SEM_RESP_PREFIX << i;
        sem_resp_consumed_name << SEM_RESP_CONSUMED_PREFIX << i;

        if (is_server) {
            auto create_semaphore = [&](const char* name, int value) -> sem_t* {
                sem_t* sem = sem_open(name, O_CREAT | O_EXCL, 0666, value);
                if (sem == SEM_FAILED && errno == EEXIST) {
                    DEBUG_COUT("Semaphore " << name << " already exists, unlinking and recreating.");
                    sem_unlink(name);
                    sem = sem_open(name, O_CREAT, 0666, value);
                }
                return sem;
            };

            sem_request_items[i] = create_semaphore(sem_req_items_name.str().c_str(), 0);
            sem_req_space[i] = create_semaphore(sem_req_space_name.str().c_str(), RING_CAP_PER_WORKER);
            sem_resp[i] = create_semaphore(sem_resp_name.str().c_str(), 0);
            sem_resp_consumed[i] = create_semaphore(sem_resp_consumed_name.str().c_str(), 1);
        } else {
            sem_request_items[i] = sem_open(sem_req_items_name.str().c_str(), 0);
            sem_req_space[i] = sem_open(sem_req_space_name.str().c_str(), 0);
            sem_resp[i] = sem_open(sem_resp_name.str().c_str(), 0);
            sem_resp_consumed[i] = sem_open(sem_resp_consumed_name.str().c_str(), 0);
        }

        if (sem_request_items[i] == SEM_FAILED || sem_req_space[i] == SEM_FAILED || sem_resp[i] == SEM_FAILED || sem_resp_consumed[i] == SEM_FAILED) {
            std::cerr << "Failed to open semaphore for worker " << i << ": " << strerror(errno) << std::endl;
            return false;
        }
    }
    std::cout << "IPCManager initialized successfully" << std::endl;
    return true;
}

/* ---------------------------------------------------------------Main Methood section-----------------------------------------------------------*/

// Putting task into worker's request queue. max total task in the queue is RING_CAP_PER_WORKER * MAX_WORKERS
bool IPCManager::enqueue_request(int worker_idx, const std::string& message, uint64_t& task_id) {
    if (message.length() >= CHUNK_SIZE) {   // Keep this check as sometimes client send long prompt, next is implement multi chunk enqueue.
        DEBUG_CERR("Message too large: " << message.length() << " >= " << CHUNK_SIZE);
        return false;
    }
    // sem_req_space is RING_CAP_PER_WORKER,
    if (sem_wait(sem_req_space[worker_idx]) == -1) {DEBUG_CERR("Failed to wait for space on worker " << worker_idx << ": " << strerror(errno));return false;}
    
    task_id = get_next_task_id();       // get unique task id.
    
    RequestQueue& queue = shared_mem_ptr->worker_queues[worker_idx];
    size_t head_val = queue.head.load();
    ReqSlot& slot = queue.req[head_val % RING_CAP_PER_WORKER];  // Get refference for the nect ReqSlot in the ring buffer.
    
    slot.task_id = task_id;
    slot.len = static_cast<uint32_t>(message.length());
    std::memcpy(slot.data, message.c_str(), message.length());
    slot.data[message.length()] = '\0';
    
    queue.head.store(head_val + 1);  // Increment the ring buffer head tracker
    
    sem_post(sem_request_items[worker_idx]);
    
    return true;
}

// Dequeues a request for a specific worker. Worker use this to get the prompt data from server. the data is in ReqSlot. This is blocking call.
bool IPCManager::dequeue_request(int worker_idx, ReqSlot& slot) {
    if (sem_wait(sem_request_items[worker_idx]) == -1) {    // Block until there is an item available
        // EINTR is ok, it means the wait was interrupted by a signal (e.g., SIGTERM)
        if (errno != EINTR) {std::cerr << "Worker " << worker_idx << " failed to wait for items: " << strerror(errno) << std::endl;}
        return false;
    }
    
    RequestQueue& queue = shared_mem_ptr->worker_queues[worker_idx];
    size_t tail_val = queue.tail.fetch_add(1);
    ReqSlot& req_slot = queue.req[tail_val % RING_CAP_PER_WORKER];
    
    // Manually copy data since std::atomic makes ReqSlot non-copyable
    slot.task_id = req_slot.task_id;
    slot.len = req_slot.len;
    std::memcpy(slot.data, req_slot.data, slot.len);
    slot.data[slot.len] = '\0';
    slot.is_canceled.store(req_slot.is_canceled.load());

    return true;
}

// Used by worker to send response chunk to server. Wait server to post sem_resp_consumed, then Load shared memory response slot, then fill up RespSlot
bool IPCManager::send_response_chunk(int worker_idx, uint64_t task_id, const std::string& chunk, bool is_last) {
    // wait until sem_resp_consumed gets posted by the server, then we send another chunk
    if (sem_wait(sem_resp_consumed[worker_idx]) == -1) {DEBUG_CERR("Failed to wait for response consumption signal from worker " << worker_idx << ": " << strerror(errno)); return false;}
    RespSlot& slot = shared_mem_ptr->resp_slots[worker_idx];
    slot.task_id.store(task_id);
    slot.len = static_cast<uint32_t>(chunk.length());
    slot.is_last_piece = is_last;
    std::memcpy(slot.data, chunk.c_str(), chunk.length());
    slot.data[chunk.length()] = '\0';
    
    sem_post(sem_resp[worker_idx]);
    return true;
}


// Used by client to wait get the token chunk from worker. This is blocking call
// It works by loading worker response slot, then match the task_id. if not match, wake up another thread, hopefully the owner of correct task_id, then sleep this thread.
// bool IPCManager::wait_for_response_chunk(int worker_idx, uint64_t task_id, std::string& chunk, bool& is_last, const std::function<bool(const std::string&)>& on_timeout_callback, bool& client_disconnected) {
//     int multiplier = 0;
//     while (true) {
//         struct timespec ts;
//         if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
//             DEBUG_CERR("clock_gettime failed: " << strerror(errno));
//             return false;
//         }
//         ts.tv_sec += 1; // 1-second timeout from now

//         if (sem_timedwait(sem_resp[worker_idx], &ts) == -1) {
//             if (errno == ETIMEDOUT) {
//                 if (on_timeout_callback) {
//                     std::cout << "On timeout callback" <<task_id<< std::endl;
//                     if (!on_timeout_callback("{\"chunk\": \"" + std::string("+") + "\", \"is_last\": " + std::string("false") + "}")) { // Callback returns false on client disconnect
//                         client_disconnected = true;
//                         return false;
//                     }
//                 }
//                 continue; // Continue waiting, jump to the top of the loop
//             }
//             DEBUG_CERR("Failed to wait for response from worker " << worker_idx << ": " << strerror(errno));
//             return false;
//         }
        
//         RespSlot& slot = shared_mem_ptr->resp_slots[worker_idx];
//         uint64_t received_task_id = slot.task_id.load();
        
//         if (received_task_id == task_id) {
//             chunk.assign(slot.data, slot.len);
//             is_last = slot.is_last_piece;
//             sem_post(sem_resp_consumed[worker_idx]); // Signal worker: chunk consumed, you can now write the next one. worker wait for this to be posted before sending another chunk
//             return true;
//         } else {
//             // This is not the chunk we are looking for. Post back to the response semaphore to wake up another waiting thread.
//             sem_post(sem_resp[worker_idx]);
//             // Yield to give other threads a chance to run and check the chunk.
//             std::this_thread::sleep_for(std::chrono::milliseconds(25));
//             multiplier++;
//             if (multiplier%5 == 0) {
//                 if (!on_timeout_callback("{\"chunk\": \"" + std::string("+") + "\", \"is_last\": " + std::string("false") + "}")) { // Callback returns false on client disconnect
//                     client_disconnected = true;
//                     return false;
//                 }
//             }
//         }
//     }
// }


bool IPCManager::wait_for_response_chunk(int worker_idx, uint64_t task_id, std::string& chunk, bool& is_last, const std::function<bool(const std::string&)>& on_timeout_callback, bool& client_disconnected) {
    while (true) {
        if (sem_wait(sem_resp[worker_idx]) == -1) {DEBUG_CERR("Failed to wait for response from worker " << worker_idx << ": " << strerror(errno));return false;}
        
        RespSlot& slot = shared_mem_ptr->resp_slots[worker_idx];
        uint64_t received_task_id = slot.task_id.load();
        
        if (received_task_id == task_id) {
            chunk.assign(slot.data, slot.len);
            is_last = slot.is_last_piece;
            sem_post(sem_resp_consumed[worker_idx]); // Signal worker: chunk consumed, you can now write the next one. worker wait for this to be posted before sending another chunk
            return true;
        } else {
            // This is not the chunk we are looking for. Post back to the response semaphore to wake up another waiting thread.
            sem_post(sem_resp[worker_idx]);
            // Yield to give other threads a chance to run and check the chunk.
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
}
/* -----------------------------------------------------------------Utility section----------------------------------------------------------------------------------*/

// Get the number of requests in the worker's request queue. Mainly for load balancing, looking for worker with the least requests in queue.
bool IPCManager::get_request_queue_size(int worker_idx, int& size) const {
    if (sem_getvalue(sem_request_items[worker_idx], &size) == -1) {return false;}// sem_getvalue returns -1 on error
    return true;
}

void IPCManager::cancel_request(int worker_idx, uint64_t task_id) {
    if (!shared_mem_ptr) return;

    RequestQueue& queue = shared_mem_ptr->worker_queues[worker_idx];
    
    // This is a best-effort cancellation. We search for the task in the queue.
    // There's a chance the worker has already dequeued it.
    size_t head = queue.head.load();
    size_t tail = queue.tail.load();

    for (size_t i = tail; i < head; ++i) {
        ReqSlot& slot = queue.req[i % RING_CAP_PER_WORKER];
        if (slot.task_id == task_id) {
            slot.is_canceled.store(true);
            break;
        }
    }
}

// Signaling that a request has been handled. Worker use this to signal the server that it has finished processing the request. increment sem_req_space.
void IPCManager::signal_request_handled(int worker_idx) {
    sem_post(sem_req_space[worker_idx]);
}

// Check if the server has requested a shutdown. Used by worker to check if it should shutdown.
bool IPCManager::is_shutdown_requested() const {
    return shared_mem_ptr && shared_mem_ptr->shutdown_flag.load();
}

// Request a shutdown. Used by server to request a shutdown.
void IPCManager::request_shutdown() {
    if (shared_mem_ptr) {
        shared_mem_ptr->shutdown_flag.store(true);   // command the workers to shutdown
        // Wake up all waiting workers process, 
        for (int i = 0; i < MAX_WORKERS; ++i) {
            sem_post(sem_request_items[i]);
        }
    }
}

uint64_t IPCManager::get_next_task_id() {
    if (!shared_mem_ptr) return 0;
    return shared_mem_ptr->next_task_id.fetch_add(1);
}
