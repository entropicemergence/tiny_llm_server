# Simple Python Client

Minimal Python client to test the C++ server. Uses only built-in libraries.

## Usage

1. Start the C++ server:
   ```bash
   cd build
   ./server.exe
   ```

2. Run the test client:
   ```bash
   cd client
   python test_client.py
   ```

## What it tests

- `GET /health` - Server health check
- `POST /process` - Message processing with different inputs

No dependencies required - uses only Python standard library.
