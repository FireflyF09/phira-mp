#pragma once
#include <string>
#include <optional>
#include <stdexcept>

struct HttpResponse {
    int status_code = 0;
    std::string body;
    bool ok() const { return status_code >= 200 && status_code < 300; }
};

class HttpClient {
public:
    // GET with optional Authorization header
    static HttpResponse get(const std::string& url, const std::string& bearer_token = "");
};

// Simple JSON value extraction (no library needed)
// Only handles flat JSON objects with string/number/bool values
namespace SimpleJson {
    std::string get_string(const std::string& json, const std::string& key);
    int get_int(const std::string& json, const std::string& key);
    float get_float(const std::string& json, const std::string& key);
    bool get_bool(const std::string& json, const std::string& key);
}
