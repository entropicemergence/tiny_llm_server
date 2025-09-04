#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>

// Configuration constants
constexpr size_t RING_CAP = 256;           // Request ring buffer capacity
constexpr size_t CHUNK_SIZE = 4096;        // Maximum message size
constexpr size_t MAX_WORKERS = 4;          // Maximum number of worker processes

// Shared memory object name
constexpr const char* SHM_NAME = "/mock_inference_shm";

// Semaphore names
constexpr const char* SEM_REQ_ITEMS = "/sem_req_items";  // Available requests
constexpr const char* SEM_REQ_SPACE = "/sem_req_space";  // Available space in ring
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
    uint64_t task_id;           // Matching task identifier
    uint32_t len;               // Result length
    char data[CHUNK_SIZE];      // Result data
    
    RespSlot() : task_id(0), len(0) {
        data[0] = '\0';
    }
};

// Main shared memory structure
struct SharedMem {
    std::atomic<size_t> head;           // Written by server (enqueue)
    std::atomic<size_t> tail;           // Claimed atomically by workers (dequeue)
    std::atomic<bool> shutdown_flag;    // Graceful shutdown signal
    std::atomic<uint64_t> next_task_id; // Task ID counter
    
    ReqSlot req[RING_CAP];              // Request ring buffer
    RespSlot resp_slots[MAX_WORKERS];   // Response slots (one per worker)
    
    SharedMem() : head(0), tail(0), shutdown_flag(false), next_task_id(1) {}
};

// Calculate total shared memory size
constexpr size_t SHARED_MEM_SIZE = sizeof(SharedMem);
