#pragma once
#include <iostream>
#include <sstream>
#include <string_view>
#include <mutex>
#include <chrono>
#include <iomanip>

class Logger {
public:
    enum class Level { Info, Warning, Error, Success };

    class LogStream {
    public:
        LogStream(Level level, std::mutex& mtx) : mutex_(mtx) {
            // 1. Generate and attach the timestamp
            stream_ << "[" << get_timestamp() << "] ";

            // 2. Attach the colored log level prefix
            switch (level) {
                case Level::Info:
                    stream_ << "\033[36m[INFO] \033[0m"; // Cyan
                    break;
                case Level::Warning:
                    stream_ << "\033[33m[WARNING] \033[0m"; // Yellow
                    break;
                case Level::Error:
                    stream_ << "\033[31m[ERROR] \033[0m"; // Red
                    out_target_ = &std::cerr;
                    break;
                case Level::Success:
                    stream_ << "\033[32m[SUCCESS] \033[0m"; // Green
                    break;
            }
        }

        // Lock the shared mutex during destruction to safely print the full line
        ~LogStream() {
            stream_ << "\n";
            std::lock_guard<std::mutex> lock(mutex_);
            *out_target_ << stream_.str();
        }

        template <typename T>
        LogStream& operator<<(const T& value) {
            stream_ << value;
            return *this;
        }

        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            stream_ << manip;
            return *this;
        }

    private:
        // Helper function to fetch modern time formatting thread-safely
        std::string get_timestamp() {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm_buf;
            
#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tm_buf, &time);
#else
            localtime_r(&time, &tm_buf);
#endif

            std::ostringstream time_stream;
            time_stream << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
            return time_stream.str();
        }

        std::ostringstream stream_;
        std::ostream* out_target_ = &std::cout;
        std::mutex& mutex_;
    };

    // Static hooks referencing a shared global mutex
    static LogStream info()    { return LogStream(Level::Info, get_mutex()); }
    static LogStream warn()    { return LogStream(Level::Warning, get_mutex()); }
    static LogStream error()   { return LogStream(Level::Error, get_mutex()); }
    static LogStream success() { return LogStream(Level::Success, get_mutex()); }

private:
    // Meyers Singleton pattern ensures a single thread-safe instance of the mutex
    static std::mutex& get_mutex() {
        static std::mutex mtx;
        return mtx;
    }
};
