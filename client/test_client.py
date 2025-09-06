#!/usr/bin/env python3
import requests
import json
import sys, time
import threading
import random
import argparse
import queue



# it seems python thread is limited to max ~ 12, need to write dedicated c client for better benchmarking
NUM_THREADS = 12
MAX_REQUESTS = 300

print_lock = threading.Lock()
display_queue = queue.Queue()


list_of_of_prompt = ["Once upon a time", "They were", "Every day", "They like to", "One day,", "They want to be",
"We can", "He was", "She is", "She wanted"]

# list_of_of_prompt = ["Lily and Tom", "Lily and Tom","Lily and Tom"]

def screen_manager(num_threads):
    """Manages the terminal screen updates."""
    thread_lines = {i: "" for i in range(1, num_threads + 1)}
    sys.stdout.write("\033[2J\033[H")  # Clear screen
    sys.stdout.flush()

    # Initial header print
    header = (
        f"Testing C++ Server with {NUM_THREADS} threads\n"
        f"Each thread will make {MAX_REQUESTS // NUM_THREADS} requests\n"
        f"Total requests: {NUM_THREADS * (MAX_REQUESTS // NUM_THREADS)}\n"
        f"{'-' * 30}\n"
    )
    sys.stdout.write(header)
    sys.stdout.flush()
    
    header_line_count = header.count('\n')

    while True:
        try:
            item = display_queue.get()
            if item is None:  # Sentinel for stopping
                break
            
            thread_id, content_type, data = item
            
            if content_type == "update":
                thread_lines[thread_id] = data
            elif content_type == "append":
                thread_lines[thread_id] += data
            elif content_type == "final":
                thread_lines[thread_id] = data
            
            # Redraw screen
            for tid, line in thread_lines.items():
                sys.stdout.write(f"\033[{tid + header_line_count};1H\033[K{line}")
            sys.stdout.flush()

        except Exception:
            # Handle exceptions to prevent the screen manager from crashing
            break
    
    # Final cursor position
    sys.stdout.write(f"\033[{num_threads + header_line_count + 1};1H")
    sys.stdout.flush()

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
            response.raise_for_status()
            
            full_response = ""
            base_text = f"T {thread_id} | {request_id}: "
            display_queue.put((thread_id, "update", base_text))

            buffer = ""
            decoder = json.JSONDecoder()
            for chunk in response.iter_content(chunk_size=None):
                if chunk:
                    buffer += chunk.decode('utf-8', errors='ignore')
                    while buffer:
                        try:
                            result, index = decoder.raw_decode(buffer)
                            chunk_text = result.get('chunk', '')
                            if chunk_text:
                                full_response += chunk_text
                                display_queue.put((thread_id, "update", base_text + full_response))

                            buffer = buffer[index:]
                        except json.JSONDecodeError:
                            break

        latency = time.time() - start_time
        final_text = f"T {thread_id} | {request_id}: Latency: {latency*1000:.2f} ms: Result: {full_response}"
        display_queue.put((thread_id, "final", final_text))

    except requests.exceptions.RequestException as e:
        error_text = f"T {thread_id} | {request_id}: Process failed: {e}"
        display_queue.put((thread_id, "final", error_text))
    except Exception as e:
        error_text = f"T {thread_id} | {request_id}: An unexpected error occurred: {e}"
        display_queue.put((thread_id, "final", error_text))

def worker_thread(thread_id, num_requests_per_thread):
    """Worker function that each thread will execute"""
    time.sleep(0.01 * thread_id)
    for i in range(num_requests_per_thread):
        prompt = list_of_of_prompt[random.randint(0, len(list_of_of_prompt)-1)]
        loading_text = f"T {thread_id} | {i}: testing with message '{prompt}'"
        display_queue.put((thread_id, "update", loading_text))
        test_process(prompt, thread_id, i)

def main():
    test_ping()
    parser = argparse.ArgumentParser(description="Run concurrent requests to the server.")
    num_requests_per_thread = MAX_REQUESTS // NUM_THREADS

    # Start the screen manager
    screen_manager_thread = threading.Thread(target=screen_manager, args=(NUM_THREADS,))
    screen_manager_thread.start()

    # Create and start worker threads
    threads = []
    for i in range(NUM_THREADS):
        thread = threading.Thread(target=worker_thread, args=(i + 1, num_requests_per_thread))
        threads.append(thread)
        thread.start()

    # Wait for all worker threads to complete
    for thread in threads:
        thread.join()

    # Stop the screen manager
    display_queue.put(None)
    screen_manager_thread.join()
    
    print("All threads completed!")

if __name__ == "__main__":
    main()
