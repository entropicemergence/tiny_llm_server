#!/usr/bin/env python3
import urllib.request
import urllib.parse
import json
import sys, time
import threading
import random

NUM_THREADS = 10
MAX_REQUESTS = 1000


list_of_of_prompt = ["Once upon a time", "They were", "Every day", "They like to", "One day,", "They want to be",
"We can", "He was", "She is", "She wanted"]

def test_process(message, thread_id, request_id):
    try:
        max_tokens = 25 + random.randint(0, 25)
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
        print(f"T {thread_id} | {request_id}: Result: {result['result']} Latency: {latency*1000:.2f} ms")
    except Exception as e:
        print(f"T {thread_id} | {request_id}: Process failed: {e}")

def test_completion (message, thread_id, request_id):
    try:
        max_tokens = 25 + random.randint(0, 25)
        data = json.dumps({"message": message, "max_tokens": max_tokens}).encode()

        req = urllib.request.Request("http://127.0.0.1:8080/completion", data, 
                                   {"Content-Type": "application/json"})
        proxy_handler = urllib.request.ProxyHandler({})
        opener = urllib.request.build_opener(proxy_handler)

        start_time = time.time()
        response = opener.open(req, timeout=10)
        latency = time.time() - start_time
        
        result = json.loads(response.read().decode())
        print(f"T {thread_id} | {request_id}: Result: {result['result']} Latency: {latency*1000:.2f} ms")
        
    except Exception as e:
        print(f"T {thread_id} | {request_id}: Process failed: {e}")


def worker_thread(thread_id, num_requests_per_thread):
    """Worker function that each thread will execute"""

    for i in range(num_requests_per_thread):
        prompt = list_of_of_prompt[random.randint(0, len(list_of_of_prompt)-1)]
        print(f"T {thread_id} | {i}: testing with message '{prompt}'")
        test_process(prompt, thread_id, i)
        # test_completion(prompt, thread_id, i)

def main():
    # Get number of requests per thread, default to 250 (1000 total)
    num_requests_per_thread = MAX_REQUESTS // NUM_THREADS

    print(f"Testing C++ Server with {NUM_THREADS} threads")
    print(f"Each thread will make {num_requests_per_thread} requests")
    print(f"Total requests: {NUM_THREADS * num_requests_per_thread}")
    # Create and start threads
    threads = []
    for i in range(NUM_THREADS):
        thread = threading.Thread(target=worker_thread, args=(i+1, num_requests_per_thread))
        threads.append(thread)
        thread.start()

    # Wait for all threads to complete
    for thread in threads:
        thread.join()

    print("All threads completed!")

if __name__ == "__main__":
    main()
