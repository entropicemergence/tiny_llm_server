#!/usr/bin/env python3
import requests
import json
import socket
import time

BASE_URL = "http://127.0.0.1:8080"

def print_test_case(name):
    print(f"\n----- {name} -----")

def test_ping():
    print_test_case("Test /ping Endpoint")
    try:
        response = requests.get(f"{BASE_URL}/ping", timeout=2)
        response.raise_for_status()
        print(f"Ping successful: {response.json()} (Status: {response.status_code})")
        return True
    except requests.exceptions.RequestException as e:
        print(f"Ping failed: {e}")
        return False

def test_nonexistent_endpoint():
    print_test_case("Test Non-Existent Endpoint")
    try:
        response = requests.get(f"{BASE_URL}/nonexistent")
        print(f"Status Code: {response.status_code} (Expected 404)")
        print(f"Response: {response.json()}")
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def test_wrong_method():
    print_test_case("Test Wrong Method on /process")
    try:
        response = requests.get(f"{BASE_URL}/process")
        print(f"Status Code: {response.status_code} (Expected 404)")
        print(f"Response: {response.json()}")
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def test_invalid_json():
    print_test_case("Test Invalid JSON in POST Body")
    try:
        headers = {"Content-Type": "application/json"}
        # Invalid JSON (missing closing brace)
        invalid_json = '{"message": "hello", "max_tokens": 20'
        response = requests.post(f"{BASE_URL}/process", headers=headers, data=invalid_json, timeout=5)
        print(f"Status Code: {response.status_code} (Expected 400)")
        print(f"Response: {response.json()}")
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")
    except json.JSONDecodeError as e:
        print(f"Could not decode JSON response: {e}")


def test_missing_field():
    print_test_case("Test Missing 'message' Field in JSON")
    try:
        headers = {"Content-Type": "application/json"}
        payload = {"max_tokens": 20}
        response = requests.post(f"{BASE_URL}/process", headers=headers, json=payload, timeout=5)
        print(f"Status Code: {response.status_code} (Expected 400)")
        print(f"Response: {response.json()}")
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def test_abrupt_close():
    print_test_case("Test Abrupt Connection Close")
    print("This test mimics a client that disconnects early.")
    try:
        host = '127.0.0.1'
        port = 8080
        
        payload = json.dumps({"message": "This is a long request that will be cut short", "max_tokens": 200})
        request = (
            f"POST /process HTTP/1.1\r\n"
            f"Host: {host}:{port}\r\n"
            f"Content-Type: application/json\r\n"
            f"Content-Length: {len(payload)}\r\n"
            f"Connection: close\r\n\r\n"
            f"{payload}"
        )

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(5)
            s.connect((host, port))
            s.sendall(request.encode('utf-8'))
            
            # Receive a small part of the response
            response_part = s.recv(128)
            print(f"Received initial part of response:\n{response_part.decode('utf-8', errors='ignore')}")
            
            # Abruptly close the socket
            print("Closing socket abruptly...")
        
        print("Socket closed. The server should handle this gracefully.")
        print("Check server logs and try sending another request with a different client to see if it's stuck.")

    except Exception as e:
        print(f"An error occurred: {e}")

def main():
    print("Starting server error handling tests...")
    
    if not test_ping():
        print("\nServer not reachable. Please ensure the server is running before starting tests.")
        return

    test_nonexistent_endpoint()
    test_wrong_method()
    test_invalid_json()
    test_missing_field()
    test_abrupt_close()
    
    print("\n----- Tests Finished -----")
    print("Verify server behavior, especially after the abrupt close test.")
    print("After the 'abrupt close' test, try running simple_client.py to ensure the server is still responsive.")


if __name__ == "__main__":
    main()
