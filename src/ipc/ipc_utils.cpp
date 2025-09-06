#include "ipc_utils.hpp"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <sstream>


#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#else
#define DEBUG_COUT(x)
#endif


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
            sem_request_items[i] = sem_open(sem_req_items_name.str().c_str(), O_CREAT, 0666, 0);
            sem_req_space[i] = sem_open(sem_req_space_name.str().c_str(), O_CREAT, 0666, RING_CAP_PER_WORKER);
            sem_resp[i] = sem_open(sem_resp_name.str().c_str(), O_CREAT, 0666, 0);
            sem_resp_consumed[i] = sem_open(sem_resp_consumed_name.str().c_str(), O_CREAT, 0666, 1);
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


bool IPCManager::enqueue_request(int worker_idx, const std::string& message, uint64_t& task_id) {
    if (!shared_mem_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (message.length() >= CHUNK_SIZE) {
        std::cerr << "Message too large: " << message.length() << " >= " << CHUNK_SIZE << std::endl;
        return false;
    }
    
    if (sem_wait(sem_req_space[worker_idx]) == -1) {
        std::cerr << "Failed to wait for space on worker " << worker_idx << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    task_id = get_next_task_id();
    
    RequestQueue& queue = shared_mem_ptr->worker_queues[worker_idx];
    size_t head_val = queue.head.load();
    ReqSlot& slot = queue.req[head_val % RING_CAP_PER_WORKER];
    
    slot.task_id = task_id;
    slot.len = static_cast<uint32_t>(message.length());
    std::memcpy(slot.data, message.c_str(), message.length());
    slot.data[message.length()] = '\0';
    
    queue.head.store(head_val + 1);
    
    sem_post(sem_request_items[worker_idx]);
    
    return true;
}

bool IPCManager::wait_for_response_chunk(int worker_idx, uint64_t task_id, std::string& chunk, bool& is_last) {
    if (!shared_mem_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (sem_wait(sem_resp[worker_idx]) == -1) {
        std::cerr << "Failed to wait for response from worker " << worker_idx << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    RespSlot& slot = shared_mem_ptr->resp_slots[worker_idx];
    
    uint64_t received_task_id = slot.task_id.load();
    
    if (received_task_id != task_id) {
        std::cerr << "Task ID mismatch: expected " << task_id << ", got " << received_task_id << std::endl;
        sem_post(sem_resp_consumed[worker_idx]); // Release the lock
        return false;
    }
    
    chunk.assign(slot.data, slot.len);
    is_last = slot.is_last_piece;
    
    sem_post(sem_resp_consumed[worker_idx]);
    
    return true;
}

bool IPCManager::dequeue_request(int worker_idx, ReqSlot& slot) {
    if (!shared_mem_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (is_shutdown_requested()) {
        return false;
    }
    
    if (sem_wait(sem_request_items[worker_idx]) == -1) {    // Block until there is an item available
        // EINTR is ok, it means the wait was interrupted by a signal (e.g., SIGTERM)
        if (errno != EINTR) {
             std::cerr << "Worker " << worker_idx << " failed to wait for items: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    if (is_shutdown_requested()) {
        return false;
    }
    
    RequestQueue& queue = shared_mem_ptr->worker_queues[worker_idx];
    size_t tail_val = queue.tail.fetch_add(1);
    ReqSlot& req_slot = queue.req[tail_val % RING_CAP_PER_WORKER];
    
    slot = req_slot;
    
    return true;
}

bool IPCManager::send_response_chunk(int worker_idx, uint64_t task_id, const std::string& chunk, bool is_last) {
    if (!shared_mem_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;    
    if (chunk.length() >= CHUNK_SIZE) {
        std::cerr << "Result chunk too large: " << chunk.length() << " >= " << CHUNK_SIZE << std::endl;
        return false;
    }

    if (sem_wait(sem_resp_consumed[worker_idx]) == -1) {
        std::cerr << "Failed to wait for response consumption signal from worker " << worker_idx << ": " << strerror(errno) << std::endl;
        return false;
    }

    RespSlot& slot = shared_mem_ptr->resp_slots[worker_idx];
    slot.task_id.store(task_id);
    slot.len = static_cast<uint32_t>(chunk.length());
    slot.is_last_piece = is_last;
    std::memcpy(slot.data, chunk.c_str(), chunk.length());
    slot.data[chunk.length()] = '\0';
    
    sem_post(sem_resp[worker_idx]);
    
    return true;
}

void IPCManager::signal_request_handled(int worker_idx) {
    if (worker_idx < 0 || worker_idx >= MAX_WORKERS) return;
    sem_post(sem_req_space[worker_idx]);
}


bool IPCManager::is_shutdown_requested() const {
    return shared_mem_ptr && shared_mem_ptr->shutdown_flag.load();
}

void IPCManager::request_shutdown() {
    if (shared_mem_ptr) {
        shared_mem_ptr->shutdown_flag.store(true);   // command the workers to shutdown
        
        // Wake up all waiting workers
        for (int i = 0; i < MAX_WORKERS; ++i) {
            sem_post(sem_request_items[i]);
        }
    }
}

uint64_t IPCManager::get_next_task_id() {
    if (!shared_mem_ptr) return 0;
    return shared_mem_ptr->next_task_id.fetch_add(1);
}
