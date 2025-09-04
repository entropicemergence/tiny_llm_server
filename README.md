# Mock Inference Server with Shared Memory Worker System

A high-performance HTTP server that processes requests using a pool of worker processes communicating via shared memory IPC. Built with C++17, POSIX shared memory, and semaphores.

## Architecture

```
          ┌───────────────────────────┐
          │      HTTP Server          │
          │  POST /process {msg}      │
          └────────────┬──────────────┘
                       │
                       ▼
            ┌───────────────────────┐     POSIX shm + sems
            │   Task Dispatcher     │◄━━━━━━━━━━━━━━━━━━━━━━━━━━┐
            │  (Round-robin)        │                           │
            └────────┬──────────────┘                           │
       enqueue req   │  assign worker i                         │
    (req ring, sems) │  await resp_i                            │
                     ▼                                          │
   ┌─────────────────────────────────────────┐                  │
   │         SHARED MEMORY REGION            │                  │
   │ ┌───────────────┐  ┌─────────────────┐  │                  │
   │ │ Request Ring  │  │ Response Slots  │  │                  │
   │ │  (256 slots)  │  │ [0..3] (4 max)  │  │                  │
   │ └───────────────┘  └─────────────────┘  │                  │
   │ head/tail + semaphores + task_ids       │                  │
   └─────────────────────────────────────────┘                  │
                     ▲                                          │
                     │  claim task i                            │
                     │  write resp_i                            │
      ┌──────────────┴──────────────┐         ┌─────────────────┴────────────┐
      │         Worker #0           │         │          Worker #3           │
      │  loop: pop→process→publish  │  ...    │   loop: pop→process→publish  │
      └─────────────────────────────┘         └──────────────────────────────┘
```

## Features

- **HTTP API**: Simple REST endpoint for message processing
- **Automatic Worker Management**: Server spawns and manages worker processes
- **Dynamic Scaling**: Workers scale up/down based on load (2-4 workers)
- **Shared Memory IPC**: High-performance inter-process communication
- **Concurrency**: Supports multiple parallel HTTP requests
- **Round-robin Dispatch**: Automatic load balancing across workers
- **Health Monitoring**: Automatic restart of crashed workers
- **Graceful Shutdown**: Clean termination of all processes
- **Bounded Memory**: Fixed-size ring buffer prevents memory growth

## Requirements

- **OS**: Linux/Unix (POSIX shared memory and semaphores required)
- **Compiler**: GCC/Clang with C++17 support
- **Build System**: CMake 3.20+
- **Dependencies**: 
  - POSIX threads (pthread)
  - POSIX real-time extensions (librt)
  - Oat++ (included as submodule)

## Building

### 1. Clone and Initialize Submodules

```bash
git clone <repository-url>
cd server_mock_inferance
git submodule update --init --recursive
```

### 2. Build with CMake

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

This creates two executables:
- `./server` - HTTP server with task dispatcher
- `./worker` - Worker process

### 3. Alternative: Use Build Script

```bash
# For Ubuntu/Linux
./build_ubuntu.sh
```

## Running the System

### 1. Start the Server

The server automatically manages worker processes - no manual worker startup required!

```bash
./server
```

Server output:
```
Starting Mock Inference Server...
Initializing task dispatcher...
WorkerManager initialized: min=2, max=4, executable=./worker
Starting initial 2 worker processes...
Spawning worker 0...
Worker 0 spawned with PID 12345
Spawning worker 1...
Worker 1 spawned with PID 12346
Successfully started 2 workers
Task dispatcher initialized successfully
Started with 2 workers
Server running on http://0.0.0.0:8080
Available endpoints:
  POST /process - Process a message
Press Ctrl+C to stop the server
```

### 2. Test the API

```bash
# Basic test
curl -X POST http://localhost:8080/process \
     -H "Content-Type: application/json" \
     -d '{"message": "hello world"}'

# Expected response:
# {"result": "WORKER_PROCESSED: DLROW OLLEH"}

# Multiple requests (test concurrency)
for i in {1..10}; do
  curl -X POST http://localhost:8080/process \
       -H "Content-Type: application/json" \
       -d "{\"message\": \"test message $i\"}" &
done
wait
```

## API Reference

### POST /process

Process a message using the worker pool.

**Request:**
```json
{
  "message": "your text here"
}
```

**Response (Success):**
```json
{
  "result": "WORKER_PROCESSED: PROCESSED_TEXT"
}
```

**Response (Error):**
```json
{
  "error": "Error description"
}
```

**Status Codes:**
- `200 OK` - Successfully processed
- `400 Bad Request` - Invalid JSON or missing message field
- `404 Not Found` - Invalid endpoint
- `500 Internal Server Error` - Processing failed

## Processing Logic

Workers currently implement a simple transformation:
1. Reverse the input string
2. Convert to uppercase  
3. Add "WORKER_PROCESSED: " prefix

Example: `"hello"` → `"WORKER_PROCESSED: OLLEH"`

## System Configuration

Key constants (in `src/ipc/shared_mem.hpp`):

```cpp
constexpr size_t RING_CAP = 256;        // Request ring buffer capacity
constexpr size_t CHUNK_SIZE = 4096;     // Maximum message size (4KB)
constexpr size_t MAX_WORKERS = 4;       // Maximum number of worker processes
```

Worker scaling configuration (in `src/server/worker_manager.cpp`):

```cpp
static constexpr int SCALE_UP_THRESHOLD = 5;    // Scale up if >5 pending requests
static constexpr int SCALE_DOWN_THRESHOLD = 2;  // Scale down if <2 avg requests per worker
static constexpr std::chrono::seconds SCALE_CHECK_INTERVAL{10}; // Check every 10 seconds
static constexpr std::chrono::seconds WORKER_IDLE_TIMEOUT{60};  // Kill idle workers after 60s
```

Default worker configuration: **2 minimum workers**, **4 maximum workers**.

## Shared Memory Layout

```cpp
struct SharedMem {
    std::atomic<size_t> head;           // Server enqueue position
    std::atomic<size_t> tail;           // Worker dequeue position  
    std::atomic<bool> shutdown_flag;    // Graceful shutdown signal
    std::atomic<uint64_t> next_task_id; // Task ID counter
    
    ReqSlot req[256];                   // Request ring buffer
    RespSlot resp_slots[4];             // Response slots (per worker)
};
```

## Synchronization

The system uses named POSIX semaphores:

- `/sem_req_items` - Signals available requests (0→N)
- `/sem_req_space` - Signals available space (N→0) 
- `/sem_resp_0` to `/sem_resp_3` - Per-worker response signals

## Error Handling

Common issues and solutions:

### "Failed to create shared memory: Permission denied"
```bash
# Check permissions
ls -la /dev/shm/
# Clean up stale shared memory
sudo rm -f /dev/shm/mock_inference_shm
sudo rm -f /dev/shm/sem.*
```

### "Failed to open shared memory: No such file or directory"
- This shouldn't happen with automatic worker management
- If it does, restart the server (it creates shared memory)

### "Failed to enqueue request - server may be overloaded"
- All 256 ring buffer slots are full
- Workers may be too slow or crashed  
- Server will automatically restart crashed workers
- Check server logs for worker spawn/termination messages

## Graceful Shutdown

1. **Ctrl+C on server**: Automatically terminates all managed workers and cleans up resources
2. **Automatic cleanup**: Shared memory and semaphores are unlinked
3. **Worker monitoring**: Background thread stops and all workers are gracefully terminated

## Performance Considerations

- **Ring Buffer**: Prevents memory allocation during processing
- **Atomic Operations**: Lock-free head/tail advancement
- **TCP_NODELAY**: Disabled Nagle's algorithm for lower latency
- **Thread Pool**: HTTP requests handled in separate threads
- **Bounded Queues**: Prevents unbounded memory growth

## Development

### Adding New Processing Logic

Modify `src/worker/worker_process.cpp`:

```cpp
std::string WorkerProcess::process_message(const std::string& input) {
    // Add your custom processing here
    return "PROCESSED: " + input;
}
```

### Extending the API

Add new endpoints in `src/main_server.cpp`:

```cpp
if (request.method == "POST" && request.path == "/new_endpoint") {
    // Handle new endpoint
}
```

### Monitoring

Server logs all operations:
- Worker spawn/termination events
- Task dispatch and assignment  
- Processing start/completion
- Scaling decisions (up/down)
- Health monitoring and restarts
- Error conditions and shutdown sequences

## Troubleshooting

### Build Issues

```bash
# Missing dependencies
sudo apt-get install build-essential cmake libpthread-stubs0-dev

# Clean build
rm -rf build && mkdir build && cd build && cmake .. && make
```

### Runtime Issues

```bash
# Check shared memory
ls -la /dev/shm/mock_*

# Check semaphores  
ls -la /dev/shm/sem.*

# Monitor processes
ps aux | grep -E "(server|worker)"

# Check system limits
cat /proc/sys/fs/mqueue/msg_max
cat /proc/sys/kernel/sem
```

## Design Decisions

### Why Shared Memory?
- **Performance**: Zero-copy message passing
- **Scalability**: Supports high-throughput scenarios
- **Isolation**: Workers run in separate processes

### Why Ring Buffer?
- **Bounded Memory**: Prevents unbounded growth
- **Lock-free**: Atomic head/tail operations
- **Cache Friendly**: Sequential memory access

### Why POSIX Semaphores?
- **Cross-platform**: Available on all Unix systems
- **Kernel-level**: Reliable synchronization
- **Named**: Survives process crashes

## Future Enhancements

- **Streaming**: Partial result transmission
- **Load Balancing**: Dynamic worker assignment
- **Health Checks**: Worker process monitoring
- **Metrics**: Performance and throughput monitoring
- **Windows Support**: Named pipes and memory-mapped files

## License

This project is part of a take-home assignment demonstrating shared memory IPC and worker process architecture.
