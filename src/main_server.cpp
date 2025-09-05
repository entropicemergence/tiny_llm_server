// #include "oatpp/core/base/Environment.hpp"  // for initialization only

#include <string>
#include <memory>
#include <iostream>
#include <thread>
#include <sstream>
#include <map>
#include <atomic>
#include <csignal>
#include <cerrno>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netinet/tcp.h> // For TCP_NODELAY
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif


#include "server/task_dispatcher.hpp"


static std::atomic<bool> global_shutdown_flag(false);
void ctrl_c_signal_handler(int signal) {
    global_shutdown_flag.store(true);
}

// Add section to setup global environment, containing model, worker binary path, and other dynamic variables. must adapt to where the base path where the program is called from

// structs for request/response data
struct ProcessRequest {
    std::string message;
    int max_tokens;
};

struct ProcessResponse {
    std::string result;
    int status_code;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

class HttpInferenceServer {
private:
    SOCKET serverSocket;
    int port;
    std::unique_ptr<TaskDispatcher> task_dispatcher;

public:
    HttpInferenceServer(int serverPort) : port(serverPort), serverSocket(INVALID_SOCKET) {task_dispatcher = std::make_unique<TaskDispatcher>();}
    ~HttpInferenceServer() {
        if (task_dispatcher) {task_dispatcher->shutdown();}                           // Cleanup dispatcher first to shutdown workers gracefully
        // if (dispatcher) {
        //     dispatcher.reset(); // This triggers full, blocking cleanup of workers
        // }
        if (serverSocket != INVALID_SOCKET) {closesocket(serverSocket);}    // Close socket
#ifdef _WIN32
        WSACleanup();                       // Clean up Winsock2
#endif
    }

    bool initializeSocket() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {                                        // Initialize Winsock2
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
#endif

        serverSocket = socket(AF_INET, SOCK_STREAM, 0); // AF_INET is the Internet address family for IPv4, SOCK_STREAM specify the type as TCP, 0 is the protocol (let the os choose default for tcp)
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }

        // Allow socket reuse
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));           // Allow socket reuse (in case short restart of the server, os sometime set port in use flog for s short time after the server is killed)

        sockaddr_in serverAddr;     // Struct to hold the server address
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;   // INADDR_ANY is the wildcard address for all IPv4 addresses
        serverAddr.sin_port = htons(port);         // Convert port to network byte order (big endian)

        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {    // Bind socket to port
            std::cerr << "Bind failed on port " << port << std::endl;
#ifdef _WIN32
            std::cerr << "WSA Error: " << WSAGetLastError() << std::endl;
#else
            perror("bind failed");
#endif
            return false;
        }

        if (listen(serverSocket, 10) == SOCKET_ERROR) {                                         // Listen for connections with a queue of 10
            std::cerr << "Listen failed" << std::endl;
            return false;
        }

        return true;
    }

    // Simple JSON parsing - extract message field
    bool parseJsonMessage(const std::string& jsonBody, struct ProcessRequest& request) {
        size_t maxTokensPos = jsonBody.find("\"max_tokens\"");
        size_t colonPos = jsonBody.find(":", maxTokensPos);
         try {
            request.max_tokens = std::stoi(jsonBody.substr(colonPos + 1));
        } catch (const std::exception&) {
            request.max_tokens = 0;
        }

        size_t messagePos = jsonBody.find("\"message\"");
        if (messagePos == std::string::npos) return false;
        
        colonPos = jsonBody.find(":", messagePos);
        if (colonPos == std::string::npos) return false;
        
        size_t quoteStart = jsonBody.find("\"", colonPos);
        if (quoteStart == std::string::npos) return false;
        
        size_t quoteEnd = jsonBody.find("\"", quoteStart + 1);
        if (quoteEnd == std::string::npos) return false;
        
        request.message = jsonBody.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        return true;
    }

    // Read and parse HTTP request directly from socket
    bool readAndParseHttpRequest(SOCKET clientSocket, HttpRequest& request) {
        std::string headerBuffer;
        char buffer[1024];
        bool headersParsed = false;
        int contentLength = 0;

        while (true) {
            int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) {
                return false; // Connection closed or error
            }

            headerBuffer.append(buffer, bytesReceived);

            // Parse headers when we have them
            if (!headersParsed && headerBuffer.find("\r\n\r\n") != std::string::npos) {
                headersParsed = true;

                // Parse request line and headers
                std::istringstream stream(headerBuffer);
                std::string line;

                // Parse request line
                if (!std::getline(stream, line)) return false;
                std::istringstream requestLine(line);
                requestLine >> request.method >> request.path;

                // Parse headers
                while (std::getline(stream, line) && line != "\r" && !line.empty()) {
                    size_t colonPos = line.find(':');
                    if (colonPos != std::string::npos) {
                        std::string key = line.substr(0, colonPos);
                        std::string value = line.substr(colonPos + 1);
                        // Trim whitespace
                        while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
                        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
                        request.headers[key] = value;
                    }
                }

                // Check Content-Length
                auto contentLengthIt = request.headers.find("Content-Length");
                if (contentLengthIt != request.headers.end()) {
                    contentLength = std::stoi(contentLengthIt->second);
                } else {
                    return false; // Require Content-Length for now
                }

                // Read body if we have it already in buffer
                size_t headerEnd = headerBuffer.find("\r\n\r\n");
                size_t bodyStart = headerEnd + 4;
                size_t availableBody = headerBuffer.size() - bodyStart;

                if (availableBody >= (size_t)contentLength) {
                    // Body is already in buffer
                    request.body = headerBuffer.substr(bodyStart, contentLength);
                    return true;
                } else {
                    // Need to read more for body
                    request.body.reserve(contentLength);
                    request.body = headerBuffer.substr(bodyStart);
                    int remaining = contentLength - request.body.size();
                    while (remaining > 0) {
                        int toRead = remaining < (int)sizeof(buffer) ? remaining : (int)sizeof(buffer);
                        bytesReceived = recv(clientSocket, buffer, toRead, 0);
                        if (bytesReceived <= 0) return false;
                        request.body.append(buffer, bytesReceived);
                        remaining -= bytesReceived;
                    }
                    return true;
                }
            }
        }
    }

    // Build HTTP response
    std::string buildHttpResponse(int statusCode, const std::string& statusText, 
                                 const std::string& body, const std::string& contentType = "application/json") {
        std::ostringstream response;
        response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
        response << "Content-Type: " << contentType << "\r\n";
        response << "Content-Length: " << body.length() << "\r\n";
        response << "Connection: close\r\n";
        response << "\r\n";
        response << body;
        return response.str();
    }

    // Process the message (main business logic)
    std::string processMessage(const ProcessRequest& request_parsed) {
        if (request_parsed.message.empty()) {
            return "{\"error\": \"No message provided\"}";
        }
        
        // Use task dispatcher to send to worker processes
        if (!task_dispatcher || !task_dispatcher->is_ready()) {
            return "{\"error\": \"Task dispatcher not ready\"}";
        }
        
        return task_dispatcher->process_message(request_parsed.message, request_parsed.max_tokens);
    }

    // Handle client connection
    void handleClient(SOCKET clientSocket) {
        // Set TCP_NODELAY to disable Nagle's algorithm for lower latency
        int opt = 1;
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));

        HttpRequest request;
        if (!readAndParseHttpRequest(clientSocket, request)) {
            std::string response = buildHttpResponse(400, "Bad Request", "{\"error\": \"Invalid HTTP request\"}");    // send bad request response to client. Close connection.
            send(clientSocket, response.c_str(), response.length(), 0);
            closesocket(clientSocket);
            return;
        }

        std::string response;
        
        if (request.method == "POST" && request.path == "/process") {
            ProcessRequest request_parsed;
            if (parseJsonMessage(request.body, request_parsed)) {
                std::string result = processMessage(request_parsed);
                response = buildHttpResponse(200, "OK", result);
            } else {
                response = buildHttpResponse(400, "Bad Request", "{\"error\": \"Invalid JSON or missing message field\"}");
            }
        } else {
            response = buildHttpResponse(404, "Not Found", "{\"error\": \"Endpoint not found\"}");
        }
        
        send(clientSocket, response.c_str(), response.length(), 0);
        closesocket(clientSocket);
    }

    void run() {
        // Initialize task dispatcher first
        if (!task_dispatcher->initialize()) {
            std::cerr << "Failed to initialize worker task dispatcher, quitting..." << std::endl;
            return;
        }else{
            std::cout << "Task dispatcher initialized successfully, worker processes are ready to serve requests" << std::endl;
        }
        
        if (!initializeSocket()) {
            std::cerr << "Failed to initialize socket, quitting..." << std::endl;
            return;
        }

        std::cout << "Server running on http://0.0.0.0:" << port << std::endl;
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "  POST /process - Process a message" << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;

        // Main server loop: Listen for incoming connections until shutdown (ctrl+c) is signaled. This is crucial for graceful shutdown, when the class is deconstructed, it will bring down all the child processes.
        while (!global_shutdown_flag.load()) {
            // Prepare file descriptor set for select
            fd_set read_fds;                    // struct that contains a list of file descriptors to monitor
            FD_ZERO(&read_fds);                 // initialize the set to empty
            FD_SET(serverSocket, &read_fds);    // add the server socket to the set, this way file descriptor for server socket is monitored for read events

            // Set timeout to periodically check shutdown flag.
            struct timeval timeout;
            timeout.tv_sec = 1;  // 1 second timeout
 
            // Wait for activity on server socket or timeout
            int activity = select(serverSocket + 1, &read_fds, nullptr, nullptr, &timeout);  // system call that monitors multiple file descriptors (like sockets). 0 = timeout, > 0 = activity, < 0 = error

            if ((activity < 0) && (errno!=EINTR)) {
                std::cerr << "Select error" << std::endl;
            }

            if (activity > 0 && FD_ISSET(serverSocket, &read_fds)) { // FD_ISSET checks if server socket is in the set
                // Accept new client connection
                sockaddr_in clientAddr;
                socklen_t clientAddrLen = sizeof(clientAddr);
                
                SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen); // accept the connection after select syscall confirm there is activity on server socket
                if (clientSocket == INVALID_SOCKET) {
                    if (global_shutdown_flag.load()) break;
                    std::cerr << "Accept failed: Invalid socket" << std::endl;
                    continue;
                }

                // Handle client in a separate thread
                std::thread clientThread(&HttpInferenceServer::handleClient, this, clientSocket);
                clientThread.detach();
            }
        }
        std::cout << "\nShutdown signal received, closing server..." << std::endl;
    }
};


int main() {
    signal(SIGINT, ctrl_c_signal_handler);   // SIGINT is the signal for ctrl+c

    std::cout << "Starting Mock Inference Server..." << std::endl;
    // Minimal Oatpp usage - just for environment initialization
    // oatpp::base::Environment::init();

    try {
        HttpInferenceServer server(8080);
        server.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }
    // oatpp::base::Environment::destroy();
    return 0;
}


// sudo lsof -i :8080
// kill 202232 202281 kill previous proccess if one mistakenly press ctrl+c twice