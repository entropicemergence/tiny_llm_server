#include "oatpp/core/base/Environment.hpp"  // Keep for initialization only
#include <string>
#include <memory>
#include <iostream>
#include <thread>
#include <sstream>
#include <map>

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
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif



// Simple C-style structs for request/response data
struct ProcessRequest {
    std::string message;
};

struct ProcessResponse {
    std::string result;
    int status_code;
};

// Simple HTTP parser and handler
class SimpleHttpServer {
private:
    SOCKET serverSocket;
    int port;

public:
    SimpleHttpServer(int serverPort = 8080) : port(serverPort), serverSocket(INVALID_SOCKET) {}
    
    ~SimpleHttpServer() {
        if (serverSocket != INVALID_SOCKET) {
            closesocket(serverSocket);
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool initializeSocket() {
#ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed" << std::endl;
            return false;
        }
#endif

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }

        // Allow socket reuse
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(port);

        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed on port " << port << std::endl;
            return false;
        }

        if (listen(serverSocket, 10) == SOCKET_ERROR) {
            std::cerr << "Listen failed" << std::endl;
            return false;
        }

        return true;
    }

    // Simple JSON parsing - extract message field
    bool parseJsonMessage(const std::string& jsonBody, std::string& message) {
        size_t messagePos = jsonBody.find("\"message\"");
        if (messagePos == std::string::npos) return false;
        
        size_t colonPos = jsonBody.find(":", messagePos);
        if (colonPos == std::string::npos) return false;
        
        size_t quoteStart = jsonBody.find("\"", colonPos);
        if (quoteStart == std::string::npos) return false;
        
        size_t quoteEnd = jsonBody.find("\"", quoteStart + 1);
        if (quoteEnd == std::string::npos) return false;
        
        message = jsonBody.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        return true;
    }

    // Parse HTTP request manually
    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    bool parseHttpRequest(const std::string& requestData, HttpRequest& request) {
        std::istringstream stream(requestData);
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
        
        // Read body if Content-Length is specified
        auto contentLengthIt = request.headers.find("Content-Length");
        if (contentLengthIt != request.headers.end()) {
            int contentLength = std::stoi(contentLengthIt->second);
            request.body.resize(contentLength);
            stream.read(&request.body[0], contentLength);
        }
        
        return true;
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
    std::string processMessage(const std::string& message) {
        if (message.empty()) {
            return "{\"result\": \"Error: No message provided\"}";
        }
        
        // TODO: Replace with worker process communication
        // For now, simple processing
        return "{\"result\": \"Processed: " + message + "\"}";
    }

    // Handle client connection
    void handleClient(SOCKET clientSocket) {
        char buffer[4096];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        
        if (bytesReceived <= 0) {
            closesocket(clientSocket);
            return;
        }
        
        buffer[bytesReceived] = '\0';
        std::string requestData(buffer);
        
        HttpRequest request;
        if (!parseHttpRequest(requestData, request)) {
            std::string response = buildHttpResponse(400, "Bad Request", "{\"error\": \"Invalid HTTP request\"}");
            send(clientSocket, response.c_str(), response.length(), 0);
            closesocket(clientSocket);
            return;
        }

        std::string response;
        
        if (request.method == "POST" && request.path == "/process") {
            std::string message;
            if (parseJsonMessage(request.body, message)) {
                std::string result = processMessage(message);
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
        if (!initializeSocket()) {
            return;
        }

        std::cout << "Server running on http://0.0.0.0:" << port << std::endl;
        std::cout << "Available endpoints:" << std::endl;
        std::cout << "  POST /process - Process a message" << std::endl;
        std::cout << "Press Ctrl+C to stop the server" << std::endl;

        while (true) {
            sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            
            SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSocket == INVALID_SOCKET) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }

            // Handle client in a separate thread for basic concurrency
            std::thread clientThread(&SimpleHttpServer::handleClient, this, clientSocket);
            clientThread.detach();
        }
    }
};

void run() {
    // Keep minimal Oatpp usage - just for environment initialization
    oatpp::base::Environment::init();
    
    try {
        // Create and run our simple HTTP server
        SimpleHttpServer server(8080);
        server.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }
    
    oatpp::base::Environment::destroy();
}

int main() {
    std::cout << "Starting Mock Inference Server..." << std::endl;
    run();
    return 0;
}