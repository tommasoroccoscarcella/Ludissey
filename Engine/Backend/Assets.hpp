#pragma once

#include "Engine/Avatar/Avatar.hpp"
#include "Engine/Backend/Backend.hpp"
#include "Engine/Experience/Experience.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ld {

namespace asset_cache {
bool valid(std::string_view asset);
std::string digest(std::string_view data);
std::string identifier(std::string_view data, std::string_view extension);
std::filesystem::path folder();
void setFolder(std::optional<std::filesystem::path> folder);
std::filesystem::path location(std::string_view asset);
std::optional<std::string> content(const PropertyValue& value);
std::string reference(std::string_view asset);
bool matches(std::string_view data, std::string_view asset);
std::optional<std::filesystem::path> verified(std::string_view asset);
std::filesystem::path store(std::string_view data, std::string_view asset);
std::string adopt(const std::filesystem::path& source);
std::string readFile(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, std::string_view data);
}

class AssetService {
public:
    AssetService(std::shared_ptr<Backend> backend, std::shared_ptr<HttpTransport> transport = nullptr);
    static std::vector<AssetId> references(const Experience& world);
    static std::vector<AssetId> meshes(const Experience& world);
    Async<void> upload(Experience world);
    Async<std::map<AssetId, std::filesystem::path>> resolve(Experience world);

private:
    std::shared_ptr<Backend> backend;
    std::shared_ptr<HttpTransport> transport;
};

struct AvatarLoad {
    std::optional<AvatarConfiguration> avatar;
    AvatarCollection collection;
};

class AvatarRepository {
public:
    virtual ~AvatarRepository() = default;
    virtual Async<AvatarLoad> load() = 0;
    virtual Async<AvatarConfiguration> save(AvatarConfiguration avatar) = 0;
    virtual Async<AvatarCollection> acquire(std::string item) = 0;
};

class BackendAvatarRepository final : public AvatarRepository {
public:
    explicit BackendAvatarRepository(std::shared_ptr<Backend> backend) : backend(std::move(backend)) {}
    Async<AvatarLoad> load() override;
    Async<AvatarConfiguration> save(AvatarConfiguration avatar) override;
    Async<AvatarCollection> acquire(std::string item) override;

private:
    std::shared_ptr<Backend> backend;
};

class AvatarCache {
public:
    explicit AvatarCache(std::filesystem::path folder);
    static AvatarCache standard();
    std::filesystem::path file(const std::string& account) const;
    std::optional<AvatarConfiguration> load(const std::string& account) const;
    void save(const AvatarConfiguration& avatar, const std::string& account) const;

private:
    std::filesystem::path folder;
};

}
