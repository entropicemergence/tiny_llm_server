#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>

// Configuration constants
constexpr size_t CHUNK_SIZE = 4096;        // Maximum message size
constexpr size_t MAX_WORKERS = 8;          // Maximum number of worker processes.
constexpr size_t RING_CAP_PER_WORKER = 32; // Capacity of each worker's request queue, MUST be a power of 2

// Shared memory object names
constexpr const char* SHM_NAME = "/inference_shm";

// Semaphore names
constexpr const char* SEM_REQ_ITEMS_PREFIX = "/sem_req_items_";
constexpr const char* SEM_REQ_SPACE_PREFIX = "/sem_req_space_";
constexpr const char* SEM_RESP_PREFIX = "/sem_resp_";    // Response semaphores (per worker)

// Request slot structure
struct ReqSlot {
    uint64_t task_id;           // Unique task identifier
    uint32_t len;               // Message length
    char data[CHUNK_SIZE];      // Message data
    
    ReqSlot() : task_id(0), len(0) {
        data[0] = '\0';
    }
};

// Response slot structure
struct RespSlot {
    std::atomic<uint64_t> task_id;
    uint32_t len;               // Result length
    char data[CHUNK_SIZE];      // Result data
    
    RespSlot() : task_id(0), len(0) {
        data[0] = '\0';
    }
};

// A request queue for a single worker
struct RequestQueue {
    ReqSlot req[RING_CAP_PER_WORKER];
    std::atomic<size_t> head; // written by server
    std::atomic<size_t> tail; // written by worker
};

// Main shared memory structure
struct SharedMem {
    // Per-worker request queues
    RequestQueue worker_queues[MAX_WORKERS];

    // Response slots - one for each worker
    RespSlot resp_slots[MAX_WORKERS];
    
    // Global state
    std::atomic<uint64_t> next_task_id;
    std::atomic<bool> shutdown_flag;

    SharedMem() : next_task_id(1), shutdown_flag(false) {
        for (int i = 0; i < MAX_WORKERS; ++i) {
            worker_queues[i].head.store(0);
            worker_queues[i].tail.store(0);
        }
    }
};

// Calculate total shared memory size
constexpr size_t SHARED_MEM_SIZE = sizeof(SharedMem);
