#include "Engine/Core/Json.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <set>
#include <string>

namespace fs = std::filesystem;
using ld::Json;

namespace {

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("File mancante: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::size_t collect(char* data, std::size_t size, std::size_t count, void* target) {
    static_cast<std::string*>(target)->append(data, size * count);
    return size * count;
}

Json admin(const std::string& title, const std::string& method, const Json& body, const std::string& secret) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Rete non disponibile");
    std::string url = "https://" + title + ".playfabapi.com/Admin/" + method, payload = body.dump(), reply;
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("X-SecretKey: " + secret).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) throw std::runtime_error(std::string("Richiesta fallita: ") + curl_easy_strerror(code));
    auto parsed = Json::parse(reply);
    if (parsed && status == 200) return (*parsed)["data"];
    std::string detail = "HTTP " + std::to_string(status);
    if (parsed) {
        if ((*parsed)["error"].isString()) detail += ", " + (*parsed)["error"].string();
        if ((*parsed)["errorCode"].isNumber()) detail += " " + std::to_string((*parsed)["errorCode"].integer());
        if ((*parsed)["errorMessage"].isString()) detail += ": " + (*parsed)["errorMessage"].string();
    }
    if (status == 401 || status == 403) detail += ". Controlla che la chiave sia la developer secret key del titolo " + title + ".";
    throw std::runtime_error("PlayFab ha rifiutato " + method + " (" + detail + ")");
}

std::set<std::string> handlers(const std::string& source) {
    static const std::regex pattern(R"(handlers\.([A-Za-z0-9_]+)\s*=)");
    std::set<std::string> found;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), pattern); it != std::sregex_iterator(); ++it) found.insert((*it)[1].str());
    return found;
}

void list(const std::string& label, const std::set<std::string>& names) {
    if (names.empty()) return;
    std::cout << label << ":";
    for (const auto& name : names) std::cout << " " << name;
    std::cout << "\n";
}

void compare(const std::string& published, const std::string& source) {
    auto before = handlers(published), after = handlers(source);
    std::set<std::string> added, removed;
    for (const auto& name : after)
        if (!before.count(name)) added.insert(name);
    for (const auto& name : before)
        if (!after.count(name)) removed.insert(name);
    std::cout << "Funzioni nei sorgenti: " << after.size() << ", pubblicate: " << before.size() << "\n";
    list("Da aggiungere", added);
    list("Da togliere", removed);
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: LudisseyPublish <radice del repository> [--pubblica | --esporta <file>]\n";
        return 2;
    }
    fs::path root = argv[1];
    std::string mode = argc > 2 ? argv[2] : "";
    try {
        std::string source = read(root / "CloudScript/Rules.js") + read(root / "CloudScript/Avatar.js") + read(root / "CloudScript/Ludissey.js");
        if (mode == "--esporta") {
            if (argc < 4) throw std::runtime_error("Indica il file di destinazione");
            std::ofstream(argv[3], std::ios::binary) << source;
            std::cout << "CloudScript esportato: " << source.size() << " caratteri\n";
            return 0;
        }
        auto rules = Json::parse(read(root / "Resources/experience-rules.json"));
        if (!rules) throw std::runtime_error("Regole non leggibili");
        std::string title = (*rules)["account"]["titleId"].string();
        const char* secret = std::getenv("LUDISSEY_PLAYFAB_SECRET");
        if (!secret || !*secret) throw std::runtime_error("Imposta LUDISSEY_PLAYFAB_SECRET con la developer secret key del titolo " + title + ".");
        curl_global_init(CURL_GLOBAL_DEFAULT);
        Json live = admin(title, "GetCloudScriptRevision", Json::object(), secret);
        std::string current;
        for (const auto& file : live["Files"].items()) current += file["FileContents"].stringOr("");
        std::cout << "Revisione pubblicata: " << ld::formatNumber(live["Revision"].number()) << (current == source ? " (identica ai sorgenti)" : " (diversa dai sorgenti)") << "\n";
        if (current != source) compare(current, source);
        if (mode == "--pubblica" && current != source) {
            Json files = Json::array({Json::object({{"Filename", "Ludissey.js"}, {"FileContents", source}})});
            Json result = admin(title, "UpdateCloudScript", Json::object({{"Files", files}, {"Publish", true}}), secret);
            Json check = admin(title, "GetCloudScriptRevision", Json::object(), secret);
            std::string now;
            for (const auto& file : check["Files"].items()) now += file["FileContents"].stringOr("");
            if (now != source) throw std::runtime_error("La revisione " + ld::formatNumber(result["Revision"].number()) + " non corrisponde ai sorgenti dopo la pubblicazione.");
            std::cout << "Nuova revisione pubblicata e verificata: " << ld::formatNumber(result["Revision"].number()) << "\n";
        } else if (mode != "--pubblica" && current != source) {
            std::cout << "Per pubblicare aggiungi --pubblica.\n";
        }
        curl_global_cleanup();
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
