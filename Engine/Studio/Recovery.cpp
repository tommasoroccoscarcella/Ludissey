#include "Recovery.hpp"

#include "Engine/Backend/Backend.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>

namespace ld::studio_recovery {

namespace {

constexpr std::uintmax_t maximumBytes = 32u * 1024 * 1024;

std::mutex& guard() {
    static std::mutex value;
    return value;
}

std::optional<std::filesystem::path>& chosen() {
    static std::optional<std::filesystem::path> value;
    return value;
}

std::optional<std::filesystem::path> fileFor(const std::string& id) {
    if (!isUuid(id)) return std::nullopt;
    return folder() / (id + ".json");
}

std::optional<std::string> read(const std::filesystem::path& file) {
    std::error_code problem;
    auto size = std::filesystem::file_size(file, problem);
    if (problem || size > maximumBytes) return std::nullopt;
    std::ifstream stream(file, std::ios::binary);
    if (!stream) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

Result<std::pair<Experience, std::string>> decode(const std::string& text) {
    auto parsed = Json::parse(text);
    if (!parsed || !(*parsed)["experience"].isObject() || !(*parsed)["savedAt"].isString()) return Error::make(ErrorCode::invalidArgument, tr("Copia di recupero non valida"));
    auto world = Experience::parse((*parsed)["experience"].dump());
    if (!world) return world.error();
    if (auto problem = world->compatibility()) return Error::make(ErrorCode::conflict, *problem);
    return std::make_pair(*world, (*parsed)["savedAt"].string());
}

}

std::filesystem::path folder() { return chosen() ? *chosen() : supportFolder() / "Recupero"; }

void setFolder(std::optional<std::filesystem::path> value) { chosen() = std::move(value); }

Result<void> write(const Experience& world, const std::string& savedAt) {
    auto file = fileFor(world.id);
    if (!file) return Error::make(ErrorCode::invalidArgument, tr("Progetto senza identificatore valido"));
    std::lock_guard<std::mutex> lock(guard());
    std::error_code problem;
    std::filesystem::create_directories(folder(), problem);
    if (problem) return Error::make(ErrorCode::unavailable, tr("Cartella di recupero non disponibile"));
    std::string data = Json::object({{"savedAt", savedAt}, {"experience", world.json()}}).dump();
    auto pending = *file;
    pending += ".nuova";
    int descriptor = ::open(pending.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (descriptor < 0) return Error::make(ErrorCode::unavailable, tr("Impossibile scrivere la copia di recupero"));
    bool written = ::fchmod(descriptor, S_IRUSR | S_IWUSR) == 0 && ::write(descriptor, data.data(), data.size()) == static_cast<ssize_t>(data.size()) && ::fsync(descriptor) == 0;
    ::close(descriptor);
    if (written) std::filesystem::rename(pending, *file, problem);
    if (!written || problem) {
        std::filesystem::remove(pending, problem);
        return Error::make(ErrorCode::unavailable, tr("Impossibile scrivere la copia di recupero"));
    }
    return {};
}

std::vector<Entry> list() {
    std::vector<Entry> found;
    std::lock_guard<std::mutex> lock(guard());
    std::error_code problem;
    std::vector<std::filesystem::path> rejected;
    for (auto it = std::filesystem::directory_iterator(folder(), problem); !problem && it != std::filesystem::directory_iterator(); it.increment(problem)) {
        const auto& path = it->path();
        if (path.extension() == ".nuova") {
            rejected.push_back(path);
            continue;
        }
        if (path.extension() != ".json" || !isUuid(path.stem().string())) continue;
        auto text = read(path);
        auto decoded = text ? decode(*text) : Result<std::pair<Experience, std::string>>(Error::make(ErrorCode::invalidArgument, "illeggibile"));
        if (!decoded || decoded->first.id != path.stem().string()) {
            Log::warning("Studio", "copia di recupero non valida scartata");
            rejected.push_back(path);
            continue;
        }
        found.push_back({decoded->first.id, decoded->first.title, decoded->second, decoded->first.blocks.size()});
    }
    for (const auto& path : rejected) std::filesystem::remove(path, problem);
    std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) { return a.savedAt > b.savedAt; });
    return found;
}

Result<Experience> load(const std::string& id) {
    auto file = fileFor(id);
    if (!file) return Error::make(ErrorCode::invalidArgument, tr("Copia di recupero non valida"));
    std::lock_guard<std::mutex> lock(guard());
    auto text = read(*file);
    if (!text) return Error::make(ErrorCode::notFound, tr("Copia di recupero non trovata"));
    auto decoded = decode(*text);
    if (!decoded) return decoded.error();
    if (decoded->first.id != id) return Error::make(ErrorCode::invalidArgument, tr("Copia di recupero non valida"));
    return decoded->first;
}

bool exists(const std::string& id) {
    auto file = fileFor(id);
    std::error_code problem;
    return file && std::filesystem::exists(*file, problem);
}

void discard(const std::string& id) {
    auto file = fileFor(id);
    if (!file) return;
    std::lock_guard<std::mutex> lock(guard());
    std::error_code problem;
    std::filesystem::remove(*file, problem);
}

}
