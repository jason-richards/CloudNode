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
        /** @brief Starts a log line with timestamp and severity prefix. */
        LogStream(Level level, std::mutex& mtx) : m_mutex(mtx) {
            // 1. Generate and attach the timestamp
            m_stream << "[" << get_timestamp() << "] ";

            // 2. Attach the colored log level prefix
            switch (level) {
                case Level::Info:
                    m_stream << "\033[36m[INFO] \033[0m"; // Cyan
                    break;
                case Level::Warning:
                    m_stream << "\033[33m[WARNING] \033[0m"; // Yellow
                    break;
                case Level::Error:
                    m_stream << "\033[31m[ERROR] \033[0m"; // Red
                    m_out_target = &std::cerr;
                    break;
                case Level::Success:
                    m_stream << "\033[32m[SUCCESS] \033[0m"; // Green
                    break;
            }
        }

        // Lock the shared mutex during destruction to safely print the full line
        /** @brief Writes the completed line to the selected output stream. */
        ~LogStream() {
            m_stream << "\n";
            std::lock_guard<std::mutex> lock(m_mutex);
            *m_out_target << m_stream.str();
        }

        /** @brief Appends a value to the pending log line. */
        template <typename T>
        LogStream& operator<<(const T& value) {
            m_stream << value;
            return *this;
        }

        /** @brief Appends an ostream manipulator to the pending log line. */
        LogStream& operator<<(std::ostream& (*manip)(std::ostream&)) {
            m_stream << manip;
            return *this;
        }

    private:
        /** @brief Formats the current local time for a log prefix. */
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

        std::ostringstream m_stream;
        std::ostream* m_out_target = &std::cout;
        std::mutex& m_mutex;
    };

    // Static hooks referencing a shared global mutex
    /** @return A stream that writes an informational log line. */
    static LogStream info()    { return LogStream(Level::Info, get_mutex()); }
    /** @return A stream that writes a warning log line. */
    static LogStream warn()    { return LogStream(Level::Warning, get_mutex()); }
    /** @return A stream that writes an error log line. */
    static LogStream error()   { return LogStream(Level::Error, get_mutex()); }
    /** @return A stream that writes a success log line. */
    static LogStream success() { return LogStream(Level::Success, get_mutex()); }

private:
    // Meyers Singleton pattern ensures a single thread-safe instance of the mutex
    /** @brief Returns the mutex shared by all log streams. */
    static std::mutex& get_mutex() {
        static std::mutex mtx;
        return mtx;
    }
};
