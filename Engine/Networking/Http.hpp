#pragma once

#include "Engine/Core/Async.hpp"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ld {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    double timeout = 25;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success() const { return status >= 200 && status < 300; }
};

class NetworkError : public ServiceError {
public:
    NetworkError(const std::string& message, bool timeout) : ServiceError(message), timedOut(timeout) {}
    bool timedOut;
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponse perform(const HttpRequest& request) = 0;
};

class CurlTransport final : public HttpTransport {
public:
    CurlTransport();
    HttpResponse perform(const HttpRequest& request) override;
};

std::shared_ptr<HttpTransport> standardTransport();

}
