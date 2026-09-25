#pragma once

#include <unordered_map>
#include <variant>
#include <string>

class PropertyBag {
public:
    using Value = std::variant<std::string, int, bool>;

    template <typename T>
    void Set(const std::string& key, T value) {
        values_[key] = std::move(value);
    }

    template <typename T>
    const T& Get(const std::string& key) const {
        return std::get<T>(values_.at(key));
    }

private:
    std::unordered_map<std::string, Value> values_;
};

