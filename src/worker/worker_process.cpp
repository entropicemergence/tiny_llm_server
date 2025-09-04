#include "worker_process.hpp"
#include <algorithm>
#include <cctype>

std::string WorkerProcess::process_message(const std::string& input) {
    if (input.empty()) {
        return "ERROR: Empty input";
    }
    
    // Process: reverse and uppercase
    std::string reversed = reverse_string(input);
    std::string result = to_uppercase(reversed);
    
    // Add processing prefix to show it was handled by worker
    return "WORKER_PROCESSED: " + result;
}

std::string WorkerProcess::reverse_string(const std::string& str) {
    std::string reversed = str;
    std::reverse(reversed.begin(), reversed.end());
    return reversed;
}

std::string WorkerProcess::to_uppercase(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), 
                   [](unsigned char c) { return std::toupper(c); });
    return upper;
}
