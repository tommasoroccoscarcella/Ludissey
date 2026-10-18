#include "Http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace ld {

namespace {

std::size_t collect(char* data, std::size_t size, std::size_t count, void* target) {
    static_cast<std::string*>(target)->append(data, size * count);
    return size * count;
}

std::size_t header(char* data, std::size_t size, std::size_t count, void* target) {
    std::string line(data, size * count);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string value = line.substr(colon + 1);
        auto begin = value.find_first_not_of(" \t");
        auto end = value.find_last_not_of(" \t\r\n");
        (*static_cast<std::map<std::string, std::string>*>(target))[key] = begin == std::string::npos ? "" : value.substr(begin, end - begin + 1);
    }
    return size * count;
}

}

CurlTransport::CurlTransport() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse CurlTransport::perform(const HttpRequest& request) {
    CURL* handle = curl_easy_init();
    if (!handle) throw NetworkError("Connessione non disponibile", false);
    HttpResponse response;
    curl_slist* headers = nullptr;
    for (const auto& [key, value] : request.headers) headers = curl_slist_append(headers, (key + ": " + value).c_str());
    curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout * 1000));
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min(request.timeout, 15.0) * 1000));
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS | CURLPROTO_HTTP));
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &response.headers);
    if (request.method != "GET" && request.method != "HEAD") {
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size()));
    }
    CURLcode code = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    if (code != CURLE_OK) throw NetworkError(std::string("Connessione non riuscita: ") + curl_easy_strerror(code), code == CURLE_OPERATION_TIMEDOUT);
    response.status = static_cast<int>(status);
    return response;
}

std::shared_ptr<HttpTransport> standardTransport() {
    static auto transport = std::make_shared<CurlTransport>();
    return transport;
}

}
