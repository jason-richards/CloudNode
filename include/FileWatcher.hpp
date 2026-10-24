#pragma once

#include <atomic>
#include <chrono>
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

    /**
     * @brief Registers the callback invoked for file changes; replaces any previous one.
     *
     * Changes are debounced: each path is queued once (changes to it are coalesced) and the
     * queue is delivered only after no file event has occurred for @p timeout. Any new event
     * restarts the wait. A zero timeout delivers immediately.
     */
    void OnFile(Callback callback, std::chrono::milliseconds timeout = std::chrono::seconds(10));

    /** @brief Begins watching. Returns false if already running or the watch cannot be set up. */
    bool Start();

    /** @brief Stops watching and joins the background thread. */
    void Stop();

private:
    void Run();
    void AddWatchTree(const std::filesystem::path& dir, bool reportExisting);
    void HandleEvents(const char* buffer, long length);
    void Notify(const std::filesystem::path& path, FileChange change);
    void FlushIfQuiet();

    std::filesystem::path m_root;
    int m_inotifyFd{-1};
    int m_wakeFd{-1};
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    std::mutex m_callbackMutex;
    Callback m_callback;
    std::chrono::milliseconds m_timeout{std::chrono::seconds(10)};  // Guarded by m_callbackMutex

    // Run thread only: changes awaiting a quiet period, and when that period ends.
    std::map<std::filesystem::path, FileChange> m_pending;
    std::chrono::steady_clock::time_point m_deadline;

    std::map<int, std::filesystem::path> m_watches;  // Run thread only after Start()
    std::set<std::filesystem::path> m_created;       // Files created but not yet closed
};
