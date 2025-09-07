# Server Test Clients

This directory contains various clients to test, debug, and benchmark the C++ LLM server.

## Python Clients

### Prerequisites

Ensure you have Python 3 and the `requests` library installed:

```bash
pip install requests
```

### 1. Simple Client (`simple_client.py`)

This script sends a single prompt to the server and streams the response. It's useful for basic functional testing.

**Usage:**

```bash
python3 client/simple_client.py "Your prompt here" --max-tokens 100
```

### 2. Multi-Inference Load Tester (`multi_inference.py`)

This is the original multi-threaded load testing script. It's good for generating concurrent load on the server and visualizing the responses in real-time.

**Usage:**

```bash
python3 client/multi_inference.py
```
You can configure the number of threads and requests by editing the constants at the top of the file.

### 3. Error Handling Tester (`error_handling_client.py`)

This script runs a suite of tests to check the server's resilience and error handling capabilities. It tests for:
- Requests to non-existent endpoints.
- Using the wrong HTTP method.
- Sending malformed JSON.
- Sending requests with missing required fields.
- Abruptly closing a connection to see if the server remains responsive.

**Usage:**

```bash
python3 client/error_handling_client.py
```
After running this script, it is recommended to run `simple_client.py` to ensure the server is still operational, especially after the abrupt connection closure test.

## C++ Benchmarking Client

For more accurate and high-performance benchmarking, a C++ client is provided. This avoids potential bottlenecks from Python's GIL and can generate a much higher load.

### Prerequisites

You need a C++ compiler (like g++) and CMake.

### Building the client

1.  Navigate to the `benchmarking_cpp` directory:
    ```bash
    cd client/benchmarking_cpp
    ```
2.  Create a build directory and run CMake:
    ```bash
    mkdir build && cd build
    cmake ..
    ```
3.  Compile the client:
    ```bash
    make
    ```
This will create an executable named `bench_client` in the `build` directory.

### Running the benchmark

Run the executable from within the `build` directory.

**Usage:**

```bash
./bench_client [options]
```

**Options:**

-   `-t, --threads <num>`: Number of concurrent threads (default: 10).
-   `-n, --requests <num>`: Number of requests each thread should send (default: 10).
-   `-s, --server <ip>`: Server IP address (default: 127.0.0.1).
-   `-p, --port <port>`: Server port (default: 8080).
-   `-h, --help`: Show help message.

**Example:**

To run a benchmark with 50 threads, each sending 100 requests:

```bash
./bench_client -t 50 -n 100
```
The client will output detailed statistics, including RPS (Requests Per Second) and latency percentiles.
