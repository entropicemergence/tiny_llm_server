#pragma once

#include <string>

// Worker processing functions
class WorkerProcess {
public:
    // Process a message (main business logic)
    // For now: reverse string and convert to uppercase
    static std::string process_message(const std::string& input);
    
    // Additional processing functions can be added here
    // e.g., static std::string process_streaming(const std::string& input);
    
private:
    // Helper functions
    static std::string reverse_string(const std::string& str);
    static std::string to_uppercase(const std::string& str);
};
