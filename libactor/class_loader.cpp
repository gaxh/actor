#include "class_loader.h"

#include <sstream>
#include <vector>

#include <dlfcn.h>

static std::map<std::string, void *> s_binary_handers; // path -> handler

bool class_loader_load_library(const std::string &path) {
    void *handler = dlopen(path.c_str(), RTLD_LAZY | RTLD_GLOBAL);

    if(handler == NULL) {
        CLASS_LOADER_ERROR("load library %s failed: %s", path.c_str(), dlerror());
        return false;
    }

    {
        WLockGuard g(class_loader_internal_lock());
        s_binary_handers[path] = handler;
    }
    return true;
}

static std::map<std::string, void *> s_sub_loaders; // base_class_name -> sub_loader

void *class_loader_get_sub_loader(const std::string &base_class_name) {
    auto iter = s_sub_loaders.find(base_class_name);

    return iter != s_sub_loaders.end() ? iter->second : NULL;
}

void class_loader_set_sub_loader(const std::string &base_class_name, void *sub_loader) {
    if(sub_loader != NULL) {
        s_sub_loaders[base_class_name] = sub_loader;
    } else {
        s_sub_loaders.erase(base_class_name);
    }
}

struct LoadedClassInfo {
    std::string class_name;
    std::string base_class_name;
};

static std::vector<LoadedClassInfo> s_loaded_class_info;

void class_loader_add_class_info(const std::string &class_name, const std::string &base_class_name) {
    LoadedClassInfo info;
    info.class_name = class_name;
    info.base_class_name = base_class_name;
    s_loaded_class_info.emplace_back(std::move(info));
}

std::string class_loader_dump_meta() {
    RLockGuard g(class_loader_internal_lock());

    std::ostringstream ss;

    ss << "binary_loaded: ";
    for(auto iter = s_binary_handers.begin(); iter != s_binary_handers.end(); ++iter) {
        ss << iter->first << ", ";
    }
    ss << "\nsub_loaders: ";
    for(auto iter = s_sub_loaders.begin(); iter != s_sub_loaders.end(); ++iter) {
        ss << iter->first << ", ";
    }
    ss << "\nclass_loaded: ";
    for(auto iter = s_loaded_class_info.begin(); iter != s_loaded_class_info.end(); ++iter) {
        const LoadedClassInfo &info = *iter;
        ss << info.class_name << ":" << info.base_class_name << ", ";
    }
    return ss.str();
}

static RWLock s_internal_lock;
RWLock &class_loader_internal_lock() {
    return s_internal_lock;
}
