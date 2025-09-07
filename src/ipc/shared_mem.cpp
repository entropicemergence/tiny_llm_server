#include "shared_mem.hpp"
#include "../utils/config.hpp"

const char* get_shm_name() {
    static const std::string name = AppConfig::get_instance().get_string("SHM_NAME", "/inference_shm");
    return name.c_str();
}

const char* get_sem_req_items_prefix() {
    static const std::string name = AppConfig::get_instance().get_string("SEM_REQ_ITEMS_PREFIX", "/sem_req_items_");
    return name.c_str();
}

const char* get_sem_req_space_prefix() {
    static const std::string name = AppConfig::get_instance().get_string("SEM_REQ_SPACE_PREFIX", "/sem_req_space_");
    return name.c_str();
}

const char* get_sem_resp_prefix() {
    static const std::string name = AppConfig::get_instance().get_string("SEM_RESP_PREFIX", "/sem_resp_");
    return name.c_str();
}

const char* get_sem_resp_consumed_prefix() {
    static const std::string name = AppConfig::get_instance().get_string("SEM_RESP_CONSUMED_PREFIX", "/sem_resp_consumed_");
    return name.c_str();
}
