#include "ipc_utils.hpp"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <sstream>

IPCManager::IPCManager(bool server, int worker_idx) 
    : shm_ptr(nullptr), shm_fd(-1), is_server(server), worker_index(worker_idx) {
    std::cout << "Building IPCManager with: server=" << server << ", worker_idx=" << worker_idx << std::endl;
    // Initialize semaphore arrays to null
    for (int i = 0; i < MAX_WORKERS; ++i) {
        sem_req_items[i] = nullptr;
        sem_req_space[i] = nullptr;
        sem_resp[i] = nullptr;
    }
}

IPCManager::~IPCManager() {
    cleanup();
}

bool IPCManager::initialize() {
    // On server startup, clean up any orphaned IPC objects from a previous run.
    if (is_server) {
        shm_unlink(SHM_NAME); // Unlink shared memory
        for (int i = 0; i < MAX_WORKERS; ++i) {
            std::ostringstream sem_req_items_name, sem_req_space_name, sem_resp_name;
            sem_req_items_name << SEM_REQ_ITEMS_PREFIX << i;
            sem_req_space_name << SEM_REQ_SPACE_PREFIX << i;
            sem_resp_name << SEM_RESP_PREFIX << i;
            sem_unlink(sem_req_items_name.str().c_str());
            sem_unlink(sem_req_space_name.str().c_str());
            sem_unlink(sem_resp_name.str().c_str());
        }
    }

    // Create or open shared memory
    if (is_server) {
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
            return false;
        }
        if (ftruncate(shm_fd, SHARED_MEM_SIZE) == -1) {
            std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
            return false;
        }
    } else {
        shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (shm_fd == -1) {
            std::cerr << "Failed to open shared memory: " << strerror(errno) << std::endl;
            return false;
        }
    }
    
    // Map shared memory
    shm_ptr = static_cast<SharedMem*>(mmap(nullptr, SHARED_MEM_SIZE, 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (shm_ptr == MAP_FAILED) {
        std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (is_server) {
        new (shm_ptr) SharedMem();
    }
    
    // Open semaphores
    for (int i = 0; i < MAX_WORKERS; ++i) {
        std::ostringstream sem_req_items_name, sem_req_space_name, sem_resp_name;
        sem_req_items_name << SEM_REQ_ITEMS_PREFIX << i;
        sem_req_space_name << SEM_REQ_SPACE_PREFIX << i;
        sem_resp_name << SEM_RESP_PREFIX << i;

        if (is_server) {
            sem_req_items[i] = sem_open(sem_req_items_name.str().c_str(), O_CREAT, 0666, 0);
            sem_req_space[i] = sem_open(sem_req_space_name.str().c_str(), O_CREAT, 0666, RING_CAP_PER_WORKER);
            sem_resp[i] = sem_open(sem_resp_name.str().c_str(), O_CREAT, 0666, 0);
        } else {
            sem_req_items[i] = sem_open(sem_req_items_name.str().c_str(), 0);
            sem_req_space[i] = sem_open(sem_req_space_name.str().c_str(), 0);
            sem_resp[i] = sem_open(sem_resp_name.str().c_str(), 0);
        }

        if (sem_req_items[i] == SEM_FAILED || sem_req_space[i] == SEM_FAILED || sem_resp[i] == SEM_FAILED) {
            std::cerr << "Failed to open semaphore for worker " << i << ": " << strerror(errno) << std::endl;
            return false;
        }
    }
    std::cout << "IPCManager initialized successfully" << std::endl;
    return true;
}

void IPCManager::cleanup() {
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (sem_req_items[i] != nullptr && sem_req_items[i] != SEM_FAILED) {
            sem_close(sem_req_items[i]);
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
    }
    
    if (shm_ptr != nullptr && shm_ptr != MAP_FAILED) {
        munmap(shm_ptr, SHARED_MEM_SIZE);
    }
    
    if (shm_fd != -1) {
        close(shm_fd);
        if (is_server) {
            shm_unlink(SHM_NAME);
        }
    }
    std::cout << "IPCManager cleaned up successfully" << std::endl;
}

bool IPCManager::enqueue_request(int worker_idx, const std::string& message, uint64_t& task_id) {
    if (!shm_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (message.length() >= CHUNK_SIZE) {
        std::cerr << "Message too large: " << message.length() << " >= " << CHUNK_SIZE << std::endl;
        return false;
    }
    
    if (sem_wait(sem_req_space[worker_idx]) == -1) {
        std::cerr << "Failed to wait for space on worker " << worker_idx << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    task_id = get_next_task_id();
    
    RequestQueue& queue = shm_ptr->worker_queues[worker_idx];
    size_t head_val = queue.head.load();
    ReqSlot& slot = queue.req[head_val % RING_CAP_PER_WORKER];
    
    slot.task_id = task_id;
    slot.len = static_cast<uint32_t>(message.length());
    std::memcpy(slot.data, message.c_str(), message.length());
    slot.data[message.length()] = '\0';
    
    queue.head.store(head_val + 1);
    
    sem_post(sem_req_items[worker_idx]);
    
    return true;
}

bool IPCManager::wait_for_response(int worker_idx, uint64_t task_id, std::string& result) {
    if (!shm_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (sem_wait(sem_resp[worker_idx]) == -1) {
        std::cerr << "Failed to wait for response from worker " << worker_idx << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    RespSlot& slot = shm_ptr->resp_slots[worker_idx];
    
    // Load the task_id from shared memory once to ensure consistency
    uint64_t received_task_id = slot.task_id.load();
    
    if (received_task_id != task_id) {
        std::cerr << "Task ID mismatch: expected " << task_id << ", got " << received_task_id << std::endl;
        // This can happen if a previous request timed out but the worker still processed it.
        // For robust implementation, a more complex handling mechanism is needed.
        // For this project, we'll treat it as a fatal error for the request.
        return false;
    }
    
    result.assign(slot.data, slot.len);
    
    return true;
}

bool IPCManager::dequeue_request(int worker_idx, ReqSlot& slot) {
    if (!shm_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (is_shutdown_requested()) {
        return false;
    }
    
    if (sem_wait(sem_req_items[worker_idx]) == -1) {
        // EINTR is ok, it means the wait was interrupted by a signal (e.g., SIGTERM)
        if (errno != EINTR) {
             std::cerr << "Worker " << worker_idx << " failed to wait for items: " << strerror(errno) << std::endl;
        }
        return false;
    }
    
    if (is_shutdown_requested()) {
        return false;
    }
    
    RequestQueue& queue = shm_ptr->worker_queues[worker_idx];
    size_t tail_val = queue.tail.fetch_add(1);
    ReqSlot& req_slot = queue.req[tail_val % RING_CAP_PER_WORKER];
    
    slot = req_slot;
    
    return true;
}

bool IPCManager::send_response(int worker_idx, uint64_t task_id, const std::string& result) {
    if (!shm_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    if (result.length() >= CHUNK_SIZE) {
        std::cerr << "Result too large: " << result.length() << " >= " << CHUNK_SIZE << std::endl;
        return false;
    }
    
    RespSlot& slot = shm_ptr->resp_slots[worker_idx];
    slot.task_id.store(task_id);
    slot.len = static_cast<uint32_t>(result.length());
    std::memcpy(slot.data, result.c_str(), result.length());
    slot.data[result.length()] = '\0';
    
    sem_post(sem_resp[worker_idx]);
    
    sem_post(sem_req_space[worker_idx]);
    
    return true;
}

bool IPCManager::is_shutdown_requested() const {
    return shm_ptr && shm_ptr->shutdown_flag.load();
}

void IPCManager::request_shutdown() {
    if (shm_ptr) {
        shm_ptr->shutdown_flag.store(true);
        
        // Wake up all waiting workers
        for (int i = 0; i < MAX_WORKERS; ++i) {
            sem_post(sem_req_items[i]);
        }
    }
}

uint64_t IPCManager::get_next_task_id() {
    if (!shm_ptr) return 0;
    return shm_ptr->next_task_id.fetch_add(1);
}
