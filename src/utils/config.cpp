#include "config.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

bool AppConfig::load(const std::string& filename) {
    config_map.clear();
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open config file: " << filename << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Remove comments
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        // Trim leading/trailing whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if(line.empty()) {
            continue;
        }

        std::istringstream iss(line);
        std::string key, value;
        if (std::getline(iss, key, '=') && std::getline(iss, value)) {
            // Trim whitespace from key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            if (!key.empty()) {
                config_map[key] = value;
            }
        }
    }
    return true;
}

std::string AppConfig::get_string(const std::string& key, const std::string& default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        return it->second;
    }
    return default_value;
}

int AppConfig::get_int(const std::string& key, int default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        try {
            return std::stoi(it->second);
        } catch (const std::exception& e) {
            std::cerr << "Config Error: Could not parse int for key '" << key << "'. Using default." << std::endl;
        }
    }
    return default_value;
}

size_t AppConfig::get_size_t(const std::string& key, size_t default_value) const {
    auto it = config_map.find(key);
    if (it != config_map.end()) {
        try {
            return std::stoul(it->second);
        } catch (const std::exception& e) {
             std::cerr << "Config Error: Could not parse size_t for key '" << key << "'. Using default." << std::endl;
        }
    }
    return default_value;
}
