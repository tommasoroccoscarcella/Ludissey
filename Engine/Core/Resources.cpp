#include "Resources.hpp"

#include <cstdlib>
#include <mutex>

namespace ld {

namespace {

std::mutex lock;

std::filesystem::path& stored() {
    static std::filesystem::path value = [] {
        const char* root = std::getenv("LUDISSEY_ROOT");
        return root ? std::filesystem::path(root) / "Resources" : std::filesystem::path("Resources");
    }();
    return value;
}

}

std::filesystem::path resourceFolder() {
    std::lock_guard<std::mutex> guard(lock);
    return stored();
}

void setResourceFolder(std::filesystem::path folder) {
    std::lock_guard<std::mutex> guard(lock);
    stored() = std::move(folder);
}

}
