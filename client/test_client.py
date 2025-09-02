#!/usr/bin/env python3
import urllib.request
import urllib.parse
import json
import sys

def test_health():
    try:
        response = urllib.request.urlopen("http://localhost:8080/health", timeout=5)
        data = json.loads(response.read().decode())
        print(f"Health: {data['result']}")
        return True
    except Exception as e:
        print(f"Health check failed: {e}")
        return False

def test_process(message):
    try:
        data = json.dumps({"message": message}).encode()
        req = urllib.request.Request("http://localhost:8080/process", data, 
                                   {"Content-Type": "application/json"})
        response = urllib.request.urlopen(req, timeout=10)
        result = json.loads(response.read().decode())
        print(f"Result: {result['result']}")
    except Exception as e:
        print(f"Process failed: {e}")

def main():
    print("Testing C++ Server")
    
    # if not test_health():
    #     print("Server not running on port 8080")
    #     return
    
    messages = ["Hello", "Test message", ""]
    for msg in messages:
        print(f"\nTesting: '{msg}'")
        test_process(msg)

if __name__ == "__main__":
    main()
