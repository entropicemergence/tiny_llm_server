#!/usr/bin/env python3
import requests
import json
import sys, time
import threading
import random
import argparse

NUM_THREADS = 10
MAX_REQUESTS = 300


# list_of_of_prompt = ["Once upon a time", "They were", "Every day", "They like to", "One day,", "They want to be",
# "We can", "He was", "She is", "She wanted"]

list_of_of_prompt = ["Lily and Tom", "Lily and Tom","Lily and Tom"]

def test_ping():
    try:
        url = "http://127.0.0.1:8080/ping"
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        print(f"Ping successful: {response.json()}")
    except requests.exceptions.RequestException as e:
        print(f"Ping failed: {e}")

def test_process(message, thread_id, request_id):
    try:
        url = "http://127.0.0.1:8080/process"
        headers = {"Content-Type": "application/json"}
        payload = {"message": message, "max_tokens": 25 + random.randint(0, 25)}

        start_time = time.time()
        
        with requests.post(url, headers=headers, json=payload, stream=True, timeout=10) as response:
            response.raise_for_status()  # Raise an exception for bad status codes
            
            print(f"T {thread_id} | {request_id}: Result: ", end='', flush=True)
            
            buffer = ""
            decoder = json.JSONDecoder()
            for chunk in response.iter_content(chunk_size=None): # Process chunks as they arrive
                if chunk:
                    buffer += chunk.decode('utf-8', errors='ignore')
                    while buffer:
                        try:
                            result, index = decoder.raw_decode(buffer)
                            print(result.get('chunk', ''), end='', flush=True)
                            buffer = buffer[index:]
                        except json.JSONDecodeError:
                            # Not a full JSON object yet
                            break

        latency = time.time() - start_time
        print() # Newline after the streamed response
        print(f"T {thread_id} | {request_id}: Latency: {latency*1000:.2f} ms")

    except requests.exceptions.RequestException as e:
        print(f"T {thread_id} | {request_id}: Process failed: {e}")
    except Exception as e:
        print(f"T {thread_id} | {request_id}: An unexpected error occurred: {e}")

# def test_completion (message, thread_id, request_id):
#     try:
#         max_tokens = 25 + random.randint(0, 25)
#         data = json.dumps({"message": message, "max_tokens": max_tokens}).encode()

#         req = urllib.request.Request("http://127.0.0.1:8080/completion", data, 
#                                    {"Content-Type": "application/json"})
#         proxy_handler = urllib.request.ProxyHandler({})
#         opener = urllib.request.build_opener(proxy_handler)

#         start_time = time.time()
#         response = opener.open(req, timeout=10)
#         latency = time.time() - start_time
        
#         result = json.loads(response.read().decode())
#         print(f"T {thread_id} | {request_id}: Result: {result['result']} Latency: {latency*1000:.2f} ms")
        
#     except Exception as e:
#         print(f"T {thread_id} | {request_id}: Process failed: {e}")


def worker_thread(thread_id, num_requests_per_thread):
    """Worker function that each thread will execute"""

    for i in range(num_requests_per_thread):
        prompt = list_of_of_prompt[random.randint(0, len(list_of_of_prompt)-1)]
        # for j in range(10):
        #     prompt += list_of_of_prompt[random.randint(0, len(list_of_of_prompt)-1)]
        print(f"T {thread_id} | {i}: testing with message '{prompt}'")
        test_process(prompt, thread_id, i)
        # test_completion(prompt, thread_id, i)

def main():
    test_ping()
    # parser = argparse.ArgumentParser(description="Run concurrent requests to the server.")
    # # Get number of requests per thread, default to 250 (1000 total)
    # num_requests_per_thread = MAX_REQUESTS // NUM_THREADS

    # print(f"Testing C++ Server with {NUM_THREADS} threads")
    # print(f"Each thread will make {num_requests_per_thread} requests")
    # print(f"Total requests: {NUM_THREADS * num_requests_per_thread}")
    # # Create and start threads
    # threads = []
    # for i in range(NUM_THREADS):
    #     thread = threading.Thread(target=worker_thread, args=(i+1, num_requests_per_thread))
    #     threads.append(thread)
    #     thread.start()

    # # Wait for all threads to complete
    # for thread in threads:
    #     thread.join()

    print("All threads completed!")

if __name__ == "__main__":
    main()
