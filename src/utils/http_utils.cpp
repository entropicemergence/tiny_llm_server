#include "http_utils.hpp"

bool HttpUtils::parseJsonMessage(const std::string& jsonBody, ProcessRequest& request) {
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

bool HttpUtils::readAndParseHttpRequest(SOCKET clientSocket, HttpRequest& request) {
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

std::string HttpUtils::buildHttpResponse(int statusCode, const std::string& statusText, 
                             const std::string& body, const std::string& contentType) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;
    return response.str();
}

std::string HttpUtils::buildHttpChunkedResponseHeader(int statusCode, const std::string& statusText, const std::string& contentType) {
    std::ostringstream response;
    response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    response << "Content-Type: " << contentType << "\r\n";
    response << "Transfer-Encoding: chunked\r\n";
    response << "Connection: keep-alive\r\n";
    response << "\r\n";
    return response.str();
}

std::string HttpUtils::buildHttpChunk(const std::string& data) {
    std::ostringstream chunk;
    chunk << std::hex << data.length() << "\r\n";
    chunk << data << "\r\n";
    return chunk.str();
}
