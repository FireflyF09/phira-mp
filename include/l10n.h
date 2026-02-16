#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

// Simple localization system
// Language IDs: 0 = en-US, 1 = zh-CN, 2 = zh-TW

class L10n {
public:
    static L10n& instance();

    void load_from_directory(const std::string& dir);
    std::string get(int lang_index, const std::string& key) const;

    static int parse_language(const std::string& lang_str);

private:
    L10n() = default;
    // lang_index -> (key -> value)
    std::vector<std::unordered_map<std::string, std::string>> bundles_;
    mutable std::mutex mtx_;
};

// Thread-local current language index
struct Language {
    int index = 0; // default en-US
    Language() = default;
    explicit Language(int idx) : index(idx) {}
};

// Get translated string for current thread's language
// Usage: tl(lang, "key")
inline std::string tl(const Language& lang, const std::string& key) {
    return L10n::instance().get(lang.index, key);
}
