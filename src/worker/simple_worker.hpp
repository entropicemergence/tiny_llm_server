#pragma once

#include <string>

class WorkerProcess {
public:
    static std::string process_message(const std::string& input);
private:
    static std::string reverse_string(const std::string& str);
    static std::string to_uppercase(const std::string& str);
};