#ifndef MIMEDETECTOR_H
#define MIMEDETECTOR_H

#include <magic.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

class MimeDetector {
public:
    MimeDetector() : m_cookie(magic_open(MAGIC_MIME_TYPE)) {
        if (m_cookie) {
            magic_load(m_cookie, NULL);
        }
    }

    ~MimeDetector() {
        if (m_cookie) {
            magic_close(m_cookie);
        }
    }

    inline std::string get_mime_type(const std::string& filename) {
        magic_t cookie = magic_open(MAGIC_MIME_TYPE);
        if (!cookie) {
            throw std::runtime_error("Failed to initialize libmagic");
        }

        if (magic_load(cookie, nullptr) != 0) {
            magic_close(cookie);
            throw std::runtime_error("Failed to load libmagic database");
        }

        std::ifstream file(filename, std::ios::binary);
        if (!file) {
            magic_close(cookie);
            throw std::runtime_error("Failed to open file: " + filename);
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        std::string mime_type = magic_buffer(cookie, content.data(), content.size());
        magic_close(cookie);

        return mime_type;
    }

    inline std::string get_file_info(const std::string& filename) {
        try {
            std::string mimeType = get_mime_type(filename);

            // Get file size
            std::uintmax_t size = fs::file_size(filename);

            // Construct JSON string
            return 
                "{\"name\": \""
                + filename 
                + "\", \"size\": "
                + std::to_string(size)
                + ", \"mimeType\": \""
                + mimeType
                + "\"}";
        } catch (const std::exception& e) {
            // Return a JSON error response if any exception occurs
            return "{\"error\": \"" + std::string(e.what()) + "\"}";
        } catch (...) {
            return "{\"error\": \"Unknown error occurred\"}";
        }
    }

private:
    magic_t m_cookie;
};


#endif // MIMEDETECTOR_H

