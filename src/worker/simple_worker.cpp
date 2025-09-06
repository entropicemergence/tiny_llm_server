#include "simple_worker.hpp"
#include <algorithm>
#include <cctype>
#include <random>

std::string WorkerProcess::process_message(const std::string& input) {
    std::string reversed = reverse_string(input);
    std::string result = to_uppercase(reversed);
    return result;
}

std::string WorkerProcess::reverse_string(const std::string& str) {
    std::string shuffled = str;
    std::string random_string = "";
    for (int i = 0; i < 50000; i++) {
        std::random_device rd;
        std::mt19937 gen(rd());
        // std::shuffle(shuffled.begin(), shuffled.end(), gen);
    }

    // Generate a 5-character random string (alphanumeric)
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    static const size_t max_index = (sizeof(charset) - 2);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, max_index);

    for (int i = 0; i < 5; ++i) {
        random_string += charset[distrib(gen)];
    }

    return shuffled + " "+random_string;
}

std::string WorkerProcess::to_uppercase(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), 
                   [](unsigned char c) { return std::toupper(c); });
    return upper;
}
