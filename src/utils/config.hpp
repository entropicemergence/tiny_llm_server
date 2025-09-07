#pragma once

#include <string>
#include <unordered_map>
#include <cstddef>

class AppConfig {
public:
    static AppConfig& get_instance() {
        static AppConfig instance;
        return instance;
    }

    bool load(const std::string& filename);

    std::string get_string(const std::string& key, const std::string& default_value) const;
    int get_int(const std::string& key, int default_value) const;
    size_t get_size_t(const std::string& key, size_t default_value) const;

private:
    AppConfig() = default;
    ~AppConfig() = default;
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    std::unordered_map<std::string, std::string> config_map;
};
