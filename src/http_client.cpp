#include "http_client.h"
#include <cstdio>
#include <cstring>
#include <sstream>
#include <array>
#include <iostream>

// Shell-escape a string for use in commands
static std::string shell_escape(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

HttpResponse HttpClient::get(const std::string& url, const std::string& bearer_token) {
    HttpResponse resp;

    // Build curl command
    // -s: silent, -w: write out HTTP code at end, -o -: output body to stdout
    std::string cmd = "curl -s -w '\\n%{http_code}' ";
    if (!bearer_token.empty()) {
        cmd += "-H " + shell_escape("Authorization: Bearer " + bearer_token) + " ";
    }
    cmd += shell_escape(url);
    cmd += " 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        resp.status_code = 0;
        resp.body = "failed to execute curl";
        return resp;
    }

    std::string output;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        output += buffer.data();
    }
    int exit_code = pclose(pipe);

    if (exit_code != 0 && output.empty()) {
        resp.status_code = 0;
        resp.body = "curl failed";
        return resp;
    }

    // The last line is the HTTP status code (from -w)
    auto last_nl = output.rfind('\n');
    if (last_nl == std::string::npos) {
        resp.status_code = 0;
        resp.body = output;
    } else {
        std::string code_str = output.substr(last_nl + 1);
        // Trim trailing whitespace
        while (!code_str.empty() && (code_str.back() == '\r' || code_str.back() == '\n' || code_str.back() == ' '))
            code_str.pop_back();
        resp.body = output.substr(0, last_nl);
        // Remove trailing \r\n from body
        while (!resp.body.empty() && (resp.body.back() == '\r' || resp.body.back() == '\n'))
            resp.body.pop_back();
        try {
            resp.status_code = std::stoi(code_str);
        } catch (...) {
            resp.status_code = 0;
        }
    }

    return resp;
}

// ── Simple JSON parser ───────────────────────────────────────────────
// Handles flat JSON like {"id":123,"name":"foo","language":"en-US","full_combo":true}

namespace SimpleJson {

static std::string find_value(const std::string& json, const std::string& key) {
    // Look for "key": or "key" :
    std::string search = "\"" + key + "\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    // Skip whitespace and colon
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == ':'))
        pos++;
    if (pos >= json.size()) return "";

    if (json[pos] == '"') {
        // String value
        pos++; // skip opening quote
        std::string result;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                pos++; // skip escape char
                switch (json[pos]) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case 'n': result += '\n'; break;
                    case 't': result += '\t'; break;
                    default: result += json[pos]; break;
                }
            } else {
                result += json[pos];
            }
            pos++;
        }
        return result;
    } else {
        // Number, bool, or null
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ')
            end++;
        return json.substr(pos, end - pos);
    }
}

std::string get_string(const std::string& json, const std::string& key) {
    return find_value(json, key);
}

int get_int(const std::string& json, const std::string& key) {
    std::string v = find_value(json, key);
    if (v.empty()) return 0;
    try { return std::stoi(v); } catch (...) { return 0; }
}

float get_float(const std::string& json, const std::string& key) {
    std::string v = find_value(json, key);
    if (v.empty()) return 0;
    try { return std::stof(v); } catch (...) { return 0; }
}

bool get_bool(const std::string& json, const std::string& key) {
    std::string v = find_value(json, key);
    return v == "true";
}

} // namespace SimpleJson
