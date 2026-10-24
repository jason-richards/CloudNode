#pragma once

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <utility>

/** @brief RAII advisory flock(2) on a file; holds a shared or exclusive lock for its lifetime. */
class FileLock {
public:
    enum class Mode { Shared, Exclusive };

    FileLock() = default;
    FileLock(FileLock&& other) noexcept : m_fd(std::exchange(other.m_fd, -1)) {}
    FileLock& operator=(FileLock&& other) noexcept {
        if (this != &other) {
            Release();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    ~FileLock() { Release(); }

    /**
     * @brief Locks the file, creating it if Exclusive and missing.
     * @param wait When false, fails instead of blocking if another holder conflicts.
     */
    bool Acquire(const std::filesystem::path& path, Mode mode, bool wait) {
        Release();
        const int flags = mode == Mode::Exclusive ? (O_RDWR | O_CREAT) : O_RDONLY;
        const int fd = open(path.c_str(), flags | O_CLOEXEC, 0644);
        if (fd < 0) {
            return false;
        }
        const int op = (mode == Mode::Exclusive ? LOCK_EX : LOCK_SH) | (wait ? 0 : LOCK_NB);
        if (flock(fd, op) != 0) {
            close(fd);
            return false;
        }
        m_fd = fd;
        return true;
    }

    void Release() {
        if (m_fd >= 0) {
            close(m_fd);
            m_fd = -1;
        }
    }

private:
    int m_fd{-1};
};

/** @brief Binary ifstream that holds a shared lock on its file from open until close. */
class LockedIfstream : public std::ifstream {
public:
    explicit LockedIfstream(const std::filesystem::path& path) {
        if (!m_lock.Acquire(path, FileLock::Mode::Shared, true)) {
            setstate(std::ios::failbit);
            return;
        }
        open(path, std::ios::binary);
    }

    ~LockedIfstream() {
        close();
        m_lock.Release();
    }

private:
    FileLock m_lock;
};
