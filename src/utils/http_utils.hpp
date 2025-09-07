#ifndef HTTP_UTILS_HPP
#define HTTP_UTILS_HPP

#include <string>
#include <map>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/tcp.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close

// Structs for request/response data
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

class HttpUtils {
public:
    // Simple JSON parsing - extract message field
    static bool parseJsonMessage(const std::string& jsonBody, ProcessRequest& request);
    
    // Read and parse HTTP request directly from socket
    static bool readAndParseHttpRequest(SOCKET clientSocket, HttpRequest& request);
    
    // Build HTTP response
    static std::string buildHttpResponse(int statusCode, const std::string& statusText, 
                                       const std::string& body, const std::string& contentType = "application/json");

    // Build HTTP chunked response header
    static std::string buildHttpChunkedResponseHeader(int statusCode, const std::string& statusText, const std::string& contentType = "application/json");

    // Build an HTTP chunk
    static std::string buildHttpChunk(const std::string& data);

    // Build JSON response chunk with proper escaping
    static std::string build_json_response_chunk(const std::string &s, bool is_last);
};

#endif // HTTP_UTILS_HPP
