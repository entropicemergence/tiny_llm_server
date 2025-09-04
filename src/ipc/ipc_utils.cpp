#include "ipc_utils.hpp"
#include <iostream>
#include <cstring>
#include <errno.h>
#include <sstream>

IPCManager::IPCManager(bool server, int worker_idx) 
    : shm_ptr(nullptr), shm_fd(-1), sem_req_items(nullptr), 
      sem_req_space(nullptr), is_server(server), worker_index(worker_idx) {
    
    // Initialize response semaphore array
    for (int i = 0; i < MAX_WORKERS; ++i) {
        sem_resp[i] = nullptr;
    }
}

IPCManager::~IPCManager() {
    cleanup();
}

bool IPCManager::initialize() {
    // Create or open shared memory
    if (is_server) {
        // Server creates the shared memory
        shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
            return false;
        }
        
        // Set the size
        if (ftruncate(shm_fd, SHARED_MEM_SIZE) == -1) {
            std::cerr << "Failed to set shared memory size: " << strerror(errno) << std::endl;
            return false;
        }
    } else {
        // Worker opens existing shared memory
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
    
    // Initialize shared memory structure if server
    if (is_server) {
        new (shm_ptr) SharedMem();  // Placement new to initialize atomics
    }
    
    // Open semaphores
    if (is_server) {
        // Server creates semaphores
        sem_req_items = sem_open(SEM_REQ_ITEMS, O_CREAT, 0666, 0);  // Initially 0 items
        sem_req_space = sem_open(SEM_REQ_SPACE, O_CREAT, 0666, RING_CAP);  // Initially full space
        
        for (int i = 0; i < MAX_WORKERS; ++i) {
            std::ostringstream oss;
            oss << SEM_RESP_PREFIX << i;
            sem_resp[i] = sem_open(oss.str().c_str(), O_CREAT, 0666, 0);  // Initially no responses
        }
    } else {
        // Worker opens existing semaphores
        sem_req_items = sem_open(SEM_REQ_ITEMS, 0);
        sem_req_space = sem_open(SEM_REQ_SPACE, 0);
        
        for (int i = 0; i < MAX_WORKERS; ++i) {
            std::ostringstream oss;
            oss << SEM_RESP_PREFIX << i;
            sem_resp[i] = sem_open(oss.str().c_str(), 0);
        }
    }
    
    // Check if all semaphores opened successfully
    if (sem_req_items == SEM_FAILED || sem_req_space == SEM_FAILED) {
        std::cerr << "Failed to open request semaphores: " << strerror(errno) << std::endl;
        return false;
    }
    
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (sem_resp[i] == SEM_FAILED) {
            std::cerr << "Failed to open response semaphore " << i << ": " << strerror(errno) << std::endl;
            return false;
        }
    }
    
    return true;
}

void IPCManager::cleanup() {
    // Close semaphores
    if (sem_req_items != nullptr && sem_req_items != SEM_FAILED) {
        sem_close(sem_req_items);
        if (is_server) {
            sem_unlink(SEM_REQ_ITEMS);
        }
    }
    
    if (sem_req_space != nullptr && sem_req_space != SEM_FAILED) {
        sem_close(sem_req_space);
        if (is_server) {
            sem_unlink(SEM_REQ_SPACE);
        }
    }
    
    for (int i = 0; i < MAX_WORKERS; ++i) {
        if (sem_resp[i] != nullptr && sem_resp[i] != SEM_FAILED) {
            sem_close(sem_resp[i]);
            if (is_server) {
                std::ostringstream oss;
                oss << SEM_RESP_PREFIX << i;
                sem_unlink(oss.str().c_str());
            }
        }
    }
    
    // Unmap shared memory
    if (shm_ptr != nullptr && shm_ptr != MAP_FAILED) {
        munmap(shm_ptr, SHARED_MEM_SIZE);
    }
    
    // Close shared memory file descriptor
    if (shm_fd != -1) {
        close(shm_fd);
        if (is_server) {
            shm_unlink(SHM_NAME);
        }
    }
}

bool IPCManager::enqueue_request(const std::string& message, uint64_t& task_id, int& assigned_worker) {
    if (!shm_ptr) return false;
    
    // Check message size
    if (message.length() >= CHUNK_SIZE) {
        std::cerr << "Message too large: " << message.length() << " >= " << CHUNK_SIZE << std::endl;
        return false;
    }
    
    // Wait for space in ring
    if (sem_wait(sem_req_space) == -1) {
        std::cerr << "Failed to wait for space: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Generate task ID and assign worker (round-robin)
    task_id = get_next_task_id();
    assigned_worker = static_cast<int>((task_id - 1) % MAX_WORKERS);
    
    // Get slot and fill it
    size_t head_val = shm_ptr->head.load();
    ReqSlot& slot = shm_ptr->req[head_val % RING_CAP];
    
    slot.task_id = task_id;
    slot.len = static_cast<uint32_t>(message.length());
    std::memcpy(slot.data, message.c_str(), message.length());
    slot.data[message.length()] = '\0';
    
    // Advance head
    shm_ptr->head.store(head_val + 1);
    
    // Signal that item is available
    sem_post(sem_req_items);
    
    return true;
}

bool IPCManager::wait_for_response(int worker_idx, uint64_t task_id, std::string& result) {
    if (!shm_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    // Wait for response from specific worker
    if (sem_wait(sem_resp[worker_idx]) == -1) {
        std::cerr << "Failed to wait for response: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Read response
    RespSlot& slot = shm_ptr->resp_slots[worker_idx];
    
    // Verify task ID matches
    if (slot.task_id != task_id) {
        std::cerr << "Task ID mismatch: expected " << task_id << ", got " << slot.task_id << std::endl;
        return false;
    }
    
    // Extract result
    result.assign(slot.data, slot.len);
    
    return true;
}

bool IPCManager::dequeue_request(ReqSlot& slot) {
    if (!shm_ptr) return false;
    
    // Check for shutdown
    if (is_shutdown_requested()) {
        return false;
    }
    
    // Wait for available item
    if (sem_wait(sem_req_items) == -1) {
        std::cerr << "Failed to wait for items: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Check shutdown again after waking up
    if (is_shutdown_requested()) {
        return false;
    }
    
    // Atomically claim tail index
    size_t tail_val = shm_ptr->tail.fetch_add(1);
    ReqSlot& req_slot = shm_ptr->req[tail_val % RING_CAP];
    
    // Copy request data
    slot = req_slot;
    
    return true;
}

bool IPCManager::send_response(int worker_idx, uint64_t task_id, const std::string& result) {
    if (!shm_ptr || worker_idx < 0 || worker_idx >= MAX_WORKERS) return false;
    
    // Check result size
    if (result.length() >= CHUNK_SIZE) {
        std::cerr << "Result too large: " << result.length() << " >= " << CHUNK_SIZE << std::endl;
        return false;
    }
    
    // Write to response slot
    RespSlot& slot = shm_ptr->resp_slots[worker_idx];
    slot.task_id = task_id;
    slot.len = static_cast<uint32_t>(result.length());
    std::memcpy(slot.data, result.c_str(), result.length());
    slot.data[result.length()] = '\0';
    
    // Signal response ready
    sem_post(sem_resp[worker_idx]);
    
    // Signal space available
    sem_post(sem_req_space);
    
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
            sem_post(sem_req_items);
        }
    }
}

uint64_t IPCManager::get_next_task_id() {
    if (!shm_ptr) return 0;
    return shm_ptr->next_task_id.fetch_add(1);
}
