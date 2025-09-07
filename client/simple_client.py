#!/usr/bin/env python3
import requests
import json
import argparse

def simple_process(message, max_tokens):
    """Sends a single request to the server and streams the response."""
    try:
        url = "http://127.0.0.1:8080/process"
        headers = {"Content-Type": "application/json"}
        payload = {"message": message, "max_tokens": max_tokens}

        print(f"Sending prompt: '{message}'")
        
        with requests.post(url, headers=headers, json=payload, stream=True, timeout=10) as response:
            response.raise_for_status()
            
            print("Response: ", end="", flush=True)
            full_response = ""
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
                                print(chunk_text, end="", flush=True)
                            buffer = buffer[index:]
                        except json.JSONDecodeError:
                            # Not enough data to decode a full JSON object, wait for more
                            break
            print("\n--- End of stream ---")

    except requests.exceptions.RequestException as e:
        print(f"\nAn error occurred: {e}")
    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}")

def main():
    parser = argparse.ArgumentParser(description="A simple client to test the C++ inference server.")
    parser.add_argument("prompt", type=str, help="The prompt to send to the model.")
    parser.add_argument("--max-tokens", type=int, default=50, help="The maximum number of tokens to generate.")
    args = parser.parse_args()

    simple_process(args.prompt, args.max_tokens)

if __name__ == "__main__":
    main()