// #include "oatpp/core/base/Environment.hpp"  // for initialization only

#include <string>
#include <memory>
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h> // For TCP_NODELAY
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#include <string.h>

#include "server/task_dispatcher.hpp"
#include "utils/http_utils.hpp"

// #define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define DEBUG_COUT(x) std::cout << x << std::endl
#define DEBUG_CERR(x) std::cerr << x << std::endl
#else
#define DEBUG_COUT(x)
#define DEBUG_CERR(x)
#endif


static std::atomic<bool> global_shutdown_flag(false);
void ctrl_c_signal_handler(int signal) {
    global_shutdown_flag.store(true);
}

// Add section to setup global environment, containing model, worker binary path, and other dynamic variables. must adapt to where the base path where the program is called from
// There is bug in wsl where the client cant connect at all with the server, solved by restarting wsl. 
// currently when the client close connection abruptly the worker beceome stuck and unable to proccess subsequent requests.
// enabling DEBUG_PRINT will ensure server and client communicate okay, this is very subtle bug

class HttpInferenceServer {
private:
    SOCKET serverSocket;
    int port;
    std::unique_ptr<TaskDispatcher> task_dispatcher;

public:
    HttpInferenceServer(int serverPort) : port(serverPort), serverSocket(INVALID_SOCKET) {
        task_dispatcher = std::make_unique<TaskDispatcher>();
    }
    ~HttpInferenceServer() {
        if (serverSocket != INVALID_SOCKET) {closesocket(serverSocket);}    // Close socket
    }

    bool initializeSocket() {

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
            DEBUG_CERR("Bind failed on port " << port);
            perror("bind failed");
            return false;
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {                                         // Listen for connections with a queue of #define SOMAXCONN	4096. It means burst of request larger than this will overwhelms os.
            DEBUG_CERR("Listen failed");
            return false;
        }

        return true;
    }


    // Handle client connection
    void handleClient(SOCKET clientSocket) {
        DEBUG_COUT("Handling new client on socket " << clientSocket);
        // Set TCP_NODELAY to disable Nagle's algorithm for lower latency
        int opt = 1;
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));

        HttpRequest request;
        DEBUG_COUT("Reading and parsing HTTP request from client " << clientSocket);
        if (!HttpUtils::readAndParseHttpRequest(clientSocket, request)) {
            DEBUG_CERR("Failed to read/parse HTTP request from client " << clientSocket);
            std::string response = HttpUtils::buildHttpResponse(400, "Bad Request", "{\"error\": \"Invalid HTTP request\"}");    // send bad request response to client. Close connection.
            send(clientSocket, response.c_str(), response.length(), 0);
            closesocket(clientSocket);
            return;
        }
        DEBUG_COUT("Successfully parsed request from client " << clientSocket << ": " << request.method << " " << request.path);

        if (request.method == "POST" && request.path == "/process") {
            ProcessRequest request_parsed;
            if (HttpUtils::parseJsonMessage(request.body, request_parsed)) {
                std::string header = HttpUtils::buildHttpChunkedResponseHeader(200, "OK");
                if(send(clientSocket, header.c_str(), header.length(), 0) == SOCKET_ERROR) {
                    DEBUG_CERR("Failed to send header");
                }


                std::atomic<bool> client_connected(true);
                auto chunk_callback = [clientSocket, &client_connected](const std::string& chunk_data) -> bool {
                    if (!client_connected) return false; // Stop if already disconnected
                    std::string chunk = HttpUtils::buildHttpChunk(chunk_data);
                    if(send(clientSocket, chunk.c_str(), chunk.length(), 0) == SOCKET_ERROR) {
                        DEBUG_CERR("Failed to send chunk");
                        client_connected.store(false);
                        return false;
                    }
                    return true;
                };

                task_dispatcher->process_message(chunk_callback, request_parsed.message, request_parsed.max_tokens);

                // Send final zero-length chunk
                if (client_connected) {
                    std::string final_chunk = "0\r\n\r\n";
                    if(send(clientSocket, final_chunk.c_str(), final_chunk.length(), 0) == SOCKET_ERROR) {
                        DEBUG_CERR("Failed to send final chunk");
                    }
                }
                closesocket(clientSocket);

            } else {
                std::string response = HttpUtils::buildHttpResponse(400, "Bad Request", "{\"error\": \"Invalid JSON or missing message field\"}");
                send(clientSocket, response.c_str(), response.length(), 0);
                closesocket(clientSocket);
            }
        } else if (request.method == "GET" && request.path == "/ping") {
            DEBUG_COUT("Handling /ping request from client " << clientSocket);
            std::string response = HttpUtils::buildHttpResponse(200, "OK", "{\"status\": \"ok\"}");
            send(clientSocket, response.c_str(), response.length(), 0);
            closesocket(clientSocket);
            DEBUG_COUT("Finished /ping request for client " << clientSocket);
        } else {
            std::string response = HttpUtils::buildHttpResponse(404, "Not Found", "{\"error\": \"Endpoint not found\"}");
            send(clientSocket, response.c_str(), response.length(), 0);
            closesocket(clientSocket);
        }
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
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;   // This is the damn problem, timeval struct has to be initialized both the sec and usec, otherwise the will hold garbage !

 
            DEBUG_COUT("Waiting for connection on select()...");
            // Wait for activity on server socket or timeout
            int activity = select(serverSocket + 1, &read_fds, nullptr, nullptr, &timeout);  // system call that monitors multiple file descriptors (like sockets). 0 = timeout, > 0 = activity, < 0 = error
            if ((activity < 0) && (errno!=EINTR)) { DEBUG_CERR("Select error: " << strerror(errno));}
            else if (activity > 0) { DEBUG_COUT("select() returned activity. Accepting connection..."); }


            if (activity > 0 && FD_ISSET(serverSocket, &read_fds)) { // FD_ISSET checks if server socket is in the set
                sockaddr_in clientAddr;
                socklen_t clientAddrLen = sizeof(clientAddr);
                
                SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen); // accept the connection after select syscall confirm there is activity on server socket
                if (clientSocket == INVALID_SOCKET) {
                    if (global_shutdown_flag.load()) break;
                    DEBUG_COUT("Accept failed: Invalid socket");
                    continue;
                }

                // Handle client in a separate thread. Fixed, the tradeoff to implement asyncronous loop is not worth it.
                std::thread clientThread(&HttpInferenceServer::handleClient, this, clientSocket);
                clientThread.detach();
            }
        }
        task_dispatcher->stop_monitor_thread();
        DEBUG_COUT("\nShutdown signal received, closing server...");
    }
};


int main() {
    signal(SIGINT, ctrl_c_signal_handler);   // SIGINT is the signal for ctrl+c
    signal(SIGPIPE, SIG_IGN);                // Ignore SIGPIPE to prevent crashes on broken client connections
    std::cout << "Starting Mock Inference Server..." << std::endl;
    // Minimal Oatpp usage - just for environment initialization
    // oatpp::base::Environment::init();

    try {
        HttpInferenceServer server(8080);
        server.run();
        
    } catch (const std::exception& e) {
        DEBUG_CERR("Server error: " << e.what());
    }
    // oatpp::base::Environment::destroy();
    return 0;
}


// sudo lsof -i :8080
// kill 202232 202281 kill previous proccess if one mistakenly press ctrl+c twice
// killall -9 -u $USER worker
// ps -u $USER