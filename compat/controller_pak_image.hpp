#pragma once
#include "pak_storage.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

// Linux experiment: existing raw 32 KiB Controller Pak images only.
// The guest owns the filesystem. This class never formats or repairs an image.
namespace rush_pak {
class Image : public Storage {
    std::array<uint8_t, 32768> bytes{};
    std::string path;
    int lock_fd = -1;
    static void fail(const char* operation) { throw std::system_error(errno, std::generic_category(), operation); }
    static bool write_failed(const char* operation, bool replacement_visible) {
        // Report only the failed operation, never a save path or payload.
        std::fprintf(stderr, "CONTROLLER_PAK_STORAGE_ERROR: %s; %s.\n", operation,
            replacement_visible ? "replacement visible, durability not confirmed" : "replacement not acknowledged");
        return false;
    }
public:
    explicit Image(const std::string& filename) : path(std::filesystem::weakly_canonical(filename).string()) {
        lock_fd = open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (lock_fd < 0) fail("Controller Pak lock open");
        try {
            if (flock(lock_fd, LOCK_EX | LOCK_NB)) fail("Controller Pak already in use");
            // Reject a FIFO without waiting indefinitely for its writer.
            int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
            if (fd < 0) fail("Controller Pak image open");
            struct stat info{};
            if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size != off_t(bytes.size())) {
                close(fd); throw std::runtime_error("Controller Pak image must be a regular raw 32768-byte file");
            }
            size_t done = 0;
            while (done < bytes.size()) {
                ssize_t count = read(fd, bytes.data() + done, bytes.size() - done);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) { close(fd); throw std::runtime_error("Controller Pak image read failed"); }
                done += size_t(count);
            }
            close(fd);
        } catch (...) { close(lock_fd); lock_fd = -1; throw; }
    }
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    ~Image() { if (lock_fd >= 0) close(lock_fd); }
    bool read_block(uint16_t address, uint8_t* data) const override {
        if ((address & 31) || address > bytes.size() - 32) return false;
        std::memcpy(data, bytes.data() + address, 32); return true;
    }
    bool write_block(uint16_t address, const uint8_t* data) override {
        if ((address & 31) || address > bytes.size() - 32) return false;
        auto next = bytes;
        std::memcpy(next.data() + address, data, 32);
        // Same-directory atomic replacement protects the old image if writing
        // the new contents fails. The separate lock survives image replacement.
        std::string temporary = path + ".tmp.XXXXXX";
        int directory = open(std::filesystem::path(path).parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory < 0) return write_failed("directory_open", false);
        // Set close-on-exec atomically: another thread may launch an audio or
        // diagnostic helper while this save is being written.
        int fd = mkostemp(temporary.data(), O_CLOEXEC);
        if (fd < 0) { close(directory); return write_failed("temporary_create", false); }
        size_t done = 0;
        bool ok = true;
        const char* failed_operation = nullptr;
        while (done < next.size()) {
            ssize_t count = write(fd, next.data() + done, next.size() - done);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) { ok = false; failed_operation = "write"; break; }
            done += size_t(count);
        }
        if (ok && fsync(fd)) { ok = false; failed_operation = "file_sync"; }
        if (close(fd)) { ok = false; if (!failed_operation) failed_operation = "file_close"; }
        if (ok && rename(temporary.c_str(), path.c_str())) { ok = false; failed_operation = "replace"; }
        if (!ok) { unlink(temporary.c_str()); close(directory); return write_failed(failed_operation, false); }
        bytes = next;
        // Replacement has committed at this point. A directory flush failure
        // reports uncertain durability; cache and visible disk contents still
        // agree, and retrying the same block remains safe.
        ok = fsync(directory) == 0;
        if (!ok) failed_operation = "directory_sync";
        if (close(directory)) { ok = false; if (!failed_operation) failed_operation = "directory_close"; }
        return ok ? true : write_failed(failed_operation, true);
    }
};
}
