#include "FileWatcher.hpp"
#include "Logger.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kWatchMask = IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;

}  // namespace

FileWatcher::FileWatcher(fs::path root)
    : m_root(std::move(root)) {}

FileWatcher::~FileWatcher() {
    Stop();
}

void
FileWatcher::OnFile(Callback callback) {
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_callback = std::move(callback);
}

bool
FileWatcher::Start() {
    if (m_running.exchange(true)) {
        return false;
    }

    std::error_code ec;
    if (!fs::is_directory(m_root, ec)) {
        Logger::error() << "FileWatcher: not a directory: " << m_root.string();
        m_running = false;
        return false;
    }

    m_inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    m_wakeFd = eventfd(0, EFD_CLOEXEC);
    if (m_inotifyFd < 0 || m_wakeFd < 0) {
        Logger::error() << "FileWatcher: unable to initialize inotify.";
        if (m_inotifyFd >= 0) close(m_inotifyFd);
        if (m_wakeFd >= 0) close(m_wakeFd);
        m_inotifyFd = m_wakeFd = -1;
        m_running = false;
        return false;
    }

    m_watches.clear();
    m_created.clear();
    AddWatchTree(m_root, false);
    m_thread = std::thread(&FileWatcher::Run, this);
    return true;
}

void
FileWatcher::Stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    const uint64_t one = 1;
    if (write(m_wakeFd, &one, sizeof(one)) < 0) {
        Logger::warn() << "FileWatcher: unable to wake worker.";
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    close(m_inotifyFd);
    close(m_wakeFd);
    m_inotifyFd = m_wakeFd = -1;
}

void
FileWatcher::Run() {
    std::vector<char> buffer(64 * 1024);
    pollfd fds[2] = {{m_inotifyFd, POLLIN, 0}, {m_wakeFd, POLLIN, 0}};

    while (m_running) {
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            Logger::error() << "FileWatcher: poll failed.";
            return;
        }
        if (fds[1].revents & POLLIN) {
            return;
        }
        if (fds[0].revents & POLLIN) {
            const auto length = read(m_inotifyFd, buffer.data(), buffer.size());
            if (length > 0) {
                HandleEvents(buffer.data(), length);
            }
        }
    }
}

void
FileWatcher::AddWatchTree(const fs::path& dir, bool reportExisting) {
    const int wd = inotify_add_watch(m_inotifyFd, dir.c_str(), kWatchMask);
    if (wd < 0) {
        Logger::warn() << "FileWatcher: unable to watch " << dir.string();
        return;
    }
    m_watches[wd] = dir;

    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code typeEc;
        if (it->is_symlink(typeEc)) {
            continue;
        }
        if (it->is_directory(typeEc)) {
            AddWatchTree(it->path(), reportExisting);
        } else if (reportExisting && it->is_regular_file(typeEc)) {
            Notify(it->path(), FileChange::Added);
        }
    }
}

void
FileWatcher::HandleEvents(const char* buffer, long length) {
    for (const char* ptr = buffer; ptr < buffer + length;) {
        const auto* event = reinterpret_cast<const inotify_event*>(ptr);
        ptr += sizeof(inotify_event) + event->len;

        if (event->mask & IN_IGNORED) {
            m_watches.erase(event->wd);
            continue;
        }
        auto dirIt = m_watches.find(event->wd);
        if (dirIt == m_watches.end() || event->len == 0) {
            continue;
        }
        const fs::path path = dirIt->second / event->name;
        const bool isDir = event->mask & IN_ISDIR;

        if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
            if (isDir) {
                AddWatchTree(path, true);
            } else if (event->mask & IN_MOVED_TO) {
                Notify(path, FileChange::Added);
            } else {
                m_created.insert(path);
            }
        } else if (event->mask & IN_CLOSE_WRITE) {
            if (!isDir) {
                Notify(path, m_created.erase(path) ? FileChange::Added : FileChange::Modified);
            }
        } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
            if (!isDir) {
                m_created.erase(path);
                Notify(path, FileChange::Removed);
            }
        }
    }
}

void
FileWatcher::Notify(const fs::path& path, FileChange change) {
    Callback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_callback;
    }
    if (callback) {
        callback(path, change);
    }
}
