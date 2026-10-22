#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

/** @brief Describes how a watched file changed. */
enum class FileChange {
    Added,
    Modified,
    Removed
};

/**
 * @brief Recursively monitors a directory tree (inotify) and reports file changes.
 *
 * The callback runs on the watcher's background thread.
 */
class FileWatcher {
public:
    using Callback = std::function<void(const std::filesystem::path&, FileChange)>;

    explicit FileWatcher(std::filesystem::path root);
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    /** @brief Registers the callback invoked for each file change; replaces any previous one. */
    void OnFile(Callback callback);

    /** @brief Begins watching. Returns false if already running or the watch cannot be set up. */
    bool Start();

    /** @brief Stops watching and joins the background thread. */
    void Stop();

private:
    void Run();
    void AddWatchTree(const std::filesystem::path& dir, bool reportExisting);
    void HandleEvents(const char* buffer, long length);
    void Notify(const std::filesystem::path& path, FileChange change);

    std::filesystem::path m_root;
    int m_inotifyFd{-1};
    int m_wakeFd{-1};
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    std::mutex m_callbackMutex;
    Callback m_callback;

    std::map<int, std::filesystem::path> m_watches;  // Run thread only after Start()
    std::set<std::filesystem::path> m_created;       // Files created but not yet closed
};
