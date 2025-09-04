#!/usr/bin/env python3
import urllib.request
import urllib.parse
import json
import sys, time

def test_process(message):
    try:
        data = json.dumps({"message": message}).encode()
        
        # Use 127.0.0.1 to avoid localhost resolution delays
        req = urllib.request.Request("http://127.0.0.1:8080/process", data, 
                                   {"Content-Type": "application/json"})
        
        # Build a proxy handler that does nothing to bypass system proxies
        proxy_handler = urllib.request.ProxyHandler({})
        opener = urllib.request.build_opener(proxy_handler)

        start_time = time.time()
        # Use the opener to make the request
        response = opener.open(req, timeout=10)
        latency = time.time() - start_time
        
        result = json.loads(response.read().decode())
        print(f"Result: {result['result']}")
        print(f"Latency: {latency*1000:.2f} ms")
    except Exception as e:
        print(f"Process failed: {e}")

def main():
    print("Testing C++ Server")
    
    messages = ["Hello", "Test message", "sbdfsdbf","grgethtrwhjyr"]
    # for msg in messages:
    for i in range(100000):
        print(f"\nTesting: '{i}'")
        test_process(messages[i%len(messages)])

if __name__ == "__main__":
    main()
