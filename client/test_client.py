#!/usr/bin/env python3
import requests
import json
import sys, time
import threading
import random
import argparse
import curses

NUM_THREADS = 10
MAX_REQUESTS = 300

print_lock = threading.Lock()


list_of_of_prompt = ["Once upon a time", "They were", "Every day", "They like to", "One day,", "They want to be",
"We can", "He was", "She is", "She wanted"]

# list_of_of_prompt = ["Lily and Tom", "Lily and Tom","Lily and Tom"]

def test_ping():
    try:
        url = "http://127.0.0.1:8080/ping"
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        # This will be printed before curses takes over the screen
        print(f"Ping successful: {response.json()}")
    except requests.exceptions.RequestException as e:
        print(f"Ping failed: {e}")

def test_process(stdscr, message, thread_id, request_id, line_num):
    full_response = ""
    try:
        url = "http://127.0.0.1:8080/process"
        headers = {"Content-Type": "application/json"}
        payload = {"message": message, "max_tokens": 25 + random.randint(0, 25)}

        start_time = time.time()
        
        with requests.post(url, headers=headers, json=payload, stream=True, timeout=10) as response:
            response.raise_for_status()  # Raise an exception for bad status codes
            
            buffer = ""
            decoder = json.JSONDecoder()
            for chunk in response.iter_content(chunk_size=None): # Process chunks as they arrive
                if chunk:
                    buffer += chunk.decode('utf-8', errors='ignore')
                    while buffer:
                        try:
                            result, index = decoder.raw_decode(buffer)
                            chunk_text = result.get('chunk', '')
                            full_response += chunk_text
                            buffer = buffer[index:]

                            with print_lock:
                                display_message = f"T {thread_id} | {request_id}: Result: {full_response}"
                                # Truncate message if it's too long for the screen
                                _, max_x = stdscr.getmaxyx()
                                if len(display_message) >= max_x:
                                    display_message = display_message[:max_x-1]
                                
                                stdscr.move(line_num, 0)
                                stdscr.clrtoeol()
                                stdscr.addstr(line_num, 0, display_message)
                                stdscr.refresh()

                        except json.JSONDecodeError:
                            # Not a full JSON object yet
                            break

        latency = time.time() - start_time
        
        with print_lock:
            final_message = f"T {thread_id} | {request_id}: Result: {full_response} | Latency: {latency*1000:.2f} ms"
            _, max_x = stdscr.getmaxyx()
            if len(final_message) >= max_x:
                final_message = final_message[:max_x-1]
            stdscr.move(line_num, 0)
            stdscr.clrtoeol()
            stdscr.addstr(line_num, 0, final_message)
            stdscr.refresh()

    except requests.exceptions.RequestException as e:
        with print_lock:
            error_message = f"T {thread_id} | {request_id}: Process failed: {e}"
            stdscr.move(line_num, 0)
            stdscr.clrtoeol()
            stdscr.addstr(line_num, 0, error_message)
            stdscr.refresh()
    except Exception as e:
        with print_lock:
            error_message = f"T {thread_id} | {request_id}: An unexpected error occurred: {e}"
            stdscr.move(line_num, 0)
            stdscr.clrtoeol()
            stdscr.addstr(line_num, 0, error_message)
            stdscr.refresh()

def worker_thread(stdscr, thread_id, num_requests_per_thread, line_num):
    """Worker function that each thread will execute"""

    for i in range(num_requests_per_thread):
        prompt = list_of_of_prompt[random.randint(0, len(list_of_of_prompt)-1)]
        # for j in range(10):
        #     prompt += list_of_of_prompt[random.randint(0, len(list_of_of_prompt)-1)]
        with print_lock:
            message = f"T {thread_id} | {i}: testing with message '{prompt}'"
            stdscr.move(line_num, 0)
            stdscr.clrtoeol()
            stdscr.addstr(line_num, 0, message)
            stdscr.refresh()
        test_process(stdscr, prompt, thread_id, i, line_num)
        # test_completion(prompt, thread_id, i)

def run_threaded_tests(stdscr):
    stdscr.clear()
    
    num_requests_per_thread = MAX_REQUESTS // NUM_THREADS
    
    stdscr.addstr(0, 0, f"Testing C++ Server with {NUM_THREADS} threads")
    stdscr.addstr(1, 0, f"Each thread will make {num_requests_per_thread} requests")
    stdscr.addstr(2, 0, f"Total requests: {NUM_THREADS * num_requests_per_thread}")
    stdscr.refresh()

    threads = []
    for i in range(NUM_THREADS):
        line_num = 4 + i
        thread = threading.Thread(target=worker_thread, args=(stdscr, i + 1, num_requests_per_thread, line_num))
        threads.append(thread)
        thread.start()

    for thread in threads:
        thread.join()

    stdscr.addstr(4 + NUM_THREADS + 2, 0, "All threads completed! Press any key to exit.")
    stdscr.getch()

def main():
    test_ping()
    parser = argparse.ArgumentParser(description="Run concurrent requests to the server.")
    # Get number of requests per thread, default to 250 (1000 total)
    num_requests_per_thread = MAX_REQUESTS // NUM_THREADS

    print(f"Testing C++ Server with {NUM_THREADS} threads")
    print(f"Each thread will make {num_requests_per_thread} requests")
    print(f"Total requests: {NUM_THREADS * num_requests_per_thread}")
    
    curses.wrapper(run_threaded_tests)

if __name__ == "__main__":
    main()
