#include "pak_storage.hpp"
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#if defined(__linux__) || defined(_WIN32)
#include "controller_pak_image.hpp"
#endif
namespace rush_pak {
Storage* controller_one() {
    static std::unique_ptr<Storage> image = []() -> std::unique_ptr<Storage> {
        const char* path = std::getenv("RUSH_CONTROLLER_PAK");
        if (!path || !*path) return nullptr;
#if defined(__linux__) || defined(_WIN32)
        return std::make_unique<Image>(path);
#else
        throw std::runtime_error("Controller Pak image storage is unavailable on this platform");
#endif
    }();
    return image.get();
}
}
extern "C" bool rush_controller_pak_attached() { return rush_pak::controller_one() != nullptr; }
