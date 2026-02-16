#pragma once
#include <string>
#include <set>
#include <shared_mutex>
#include <fstream>
#include <iostream>
#include <sstream>

class BanManager {
public:
    static BanManager& instance() {
        static BanManager inst;
        return inst;
    }

    void load(const std::string& path = "banned.txt") {
        std::unique_lock lock(mtx_);
        path_ = path;
        banned_.clear();
        std::ifstream f(path);
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            // trim
            size_t s = line.find_first_not_of(" \t\r\n");
            if (s == std::string::npos) continue;
            line = line.substr(s);
            if (line.empty() || line[0] == '#') continue;
            try {
                int32_t id = std::stoi(line);
                banned_.insert(id);
            } catch (...) {}
        }
        std::cerr << "[ban] loaded " << banned_.size() << " banned users" << std::endl;
    }

    void save() const {
        std::shared_lock lock(mtx_);
        std::ofstream f(path_);
        if (!f.is_open()) {
            std::cerr << "[ban] failed to save ban list" << std::endl;
            return;
        }
        for (auto id : banned_) {
            f << id << "\n";
        }
    }

    bool is_banned(int32_t user_id) const {
        std::shared_lock lock(mtx_);
        return banned_.count(user_id) > 0;
    }

    bool ban(int32_t user_id) {
        {
            std::unique_lock lock(mtx_);
            if (!banned_.insert(user_id).second) return false;
        }
        save();
        return true;
    }

    bool unban(int32_t user_id) {
        {
            std::unique_lock lock(mtx_);
            if (banned_.erase(user_id) == 0) return false;
        }
        save();
        return true;
    }

    std::set<int32_t> get_banned() const {
        std::shared_lock lock(mtx_);
        return banned_;
    }

private:
    BanManager() = default;
    mutable std::shared_mutex mtx_;
    std::set<int32_t> banned_;
    std::string path_ = "banned.txt";
};
