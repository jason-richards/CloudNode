#pragma once

#include <unordered_map>
#include <variant>
#include <string>

class PropertyBag {
public:
    using Value = std::variant<std::string, int, bool>;

    /**
     * @brief Stores or replaces a typed value under a key.
     * @param key Name used to retrieve the value.
     * @param value Value to store; supported types are string, int, and bool.
     */
    template <typename T>
    void Set(const std::string& key, T value) {
        m_values[key] = std::move(value);
    }

    /**
     * @brief Retrieves a value by key and type.
    * @param key Name of the stored value.
    * @tparam T Expected value type.
    * @return Const reference to the stored value.
     * @throws std::out_of_range if the key is not present.
     * @throws std::bad_variant_access if the stored type differs from T.
     */
    template <typename T>
    const T& Get(const std::string& key) const {
        return std::get<T>(m_values.at(key));
    }

    /**
     * @brief Tests whether a key is present.
     * @param key Name to check.
     * @return true if the key exists; otherwise false.
     */
    bool Contains(const std::string& key) const {
        return m_values.contains(key);
    }

private:
    std::unordered_map<std::string, Value> m_values;
};

