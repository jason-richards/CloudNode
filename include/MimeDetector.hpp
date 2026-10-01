#ifndef MIMEDETECTOR_H
#define MIMEDETECTOR_H

#include <magic.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

/**
 * @file MimeDetector.hpp
 * @brief Detects MIME types and file metadata for files transferred through the app.
 *
 * This utility wraps libmagic and exposes a small interface for identifying the
 * MIME type of a file and returning a compact JSON object containing the file
 * name, size, and MIME type. The class is used when constructing file offer
 * payloads for the WebRTC client.
 */

/**
 * @class MimeDetector
 * @brief Detects MIME types for local files using libmagic.
 *
 * The detector initializes a libmagic handle in the constructor and releases it
 * in the destructor. It provides convenience methods to read file contents,
 * classify the content type, and serialize the result as JSON metadata.
 */
class MimeDetector {
public:
    /**
     * @brief Creates a MIME detector and initializes the libmagic handle.
     *
     * The object opens a libmagic cookie using `MAGIC_MIME_TYPE`, which returns
     * a MIME string such as `text/plain` or `image/jpeg` without extra charset
     * data. If initialization fails, the cookie remains null and the later
     * methods will throw runtime errors when they attempt to use it.
     */
    MimeDetector() : m_cookie(magic_open(MAGIC_MIME_TYPE)) {
        if (m_cookie) {
            magic_load(m_cookie, NULL);
        }
    }

    /**
     * @brief Releases the underlying libmagic resource.
     */
    ~MimeDetector() {
        if (m_cookie) {
            magic_close(m_cookie);
        }
    }

    /**
     * @brief Detects the MIME type of the given file.
     *
     * The file is opened in binary mode, read into memory, and passed to
     * `magic_buffer()`, which inspects its contents and returns the associated
     * MIME type. This implementation creates a fresh libmagic session for each
     * call so the detector remains independent from any external library state.
     *
     * @param filename Path to the file whose MIME type will be identified.
     * @return std::string MIME type reported by libmagic, such as `application/pdf`
     *         or `image/png`.
     * @throws std::runtime_error if libmagic cannot be initialized, if the magic
     *         database cannot be loaded, or if the file cannot be opened.
     */
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

    /**
     * @brief Returns a JSON object with the file name, size, and MIME type.
     *
     * This helper is used to build file-offer payloads sent to the signalling
     * server. It calls `get_mime_type()` to detect the file content type,
     * obtains the file size via the filesystem API, and serializes all values as
     * a JSON object. If any step fails, the method returns a JSON error payload
     * instead of propagating an exception.
     *
     * @param filename Path to the file to inspect.
     * @return std::string JSON payload such as:
     *         {"name": "example.pdf", "size": 12345, "mimeType": "application/pdf"}
     *         or an error object on failure.
     */
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
    /**
     * @brief libmagic cookie used to maintain a persistent MIME inspection handle.
     *
     * The member is initialized in the constructor and closed in the destructor,
     * even though the current methods create ephemeral cookie instances for each
     * call. Keeping this handle avoids accidental lifetime issues and documents
     * the dependency on libmagic.
     */
    magic_t m_cookie;
};


#endif // MIMEDETECTOR_H

