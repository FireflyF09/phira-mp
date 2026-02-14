#include "l10n.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

static const std::string LANG_FILES[] = {"en-US", "zh-CN", "zh-TW"};
static constexpr int NUM_LANGS = 3;

L10n& L10n::instance() {
    static L10n inst;
    return inst;
}

void L10n::load_from_directory(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mtx_);
    bundles_.resize(NUM_LANGS);

    for (int i = 0; i < NUM_LANGS; i++) {
        std::string path = dir + "/" + LANG_FILES[i] + ".ftl";
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "[l10n] warning: could not open " << path << std::endl;
            continue;
        }
        std::string line;
        while (std::getline(f, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') continue;
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            // Parse "key = value"
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // Trim key and value
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            size_t vs = val.find_first_not_of(" \t");
            if (vs != std::string::npos) val = val.substr(vs);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
            bundles_[i][key] = val;
        }
    }
}

std::string L10n::get(int lang_index, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mtx_);
    if (lang_index >= 0 && lang_index < (int)bundles_.size()) {
        auto it = bundles_[lang_index].find(key);
        if (it != bundles_[lang_index].end()) return it->second;
    }
    // Fallback to en-US (index 0)
    if (!bundles_.empty()) {
        auto it = bundles_[0].find(key);
        if (it != bundles_[0].end()) return it->second;
    }
    return key; // Return key as fallback
}

int L10n::parse_language(const std::string& lang_str) {
    if (lang_str.find("zh-CN") != std::string::npos || lang_str == "zh-Hans" || lang_str == "zh") return 1;
    if (lang_str.find("zh-TW") != std::string::npos || lang_str == "zh-Hant") return 2;
    return 0; // default en-US
}
