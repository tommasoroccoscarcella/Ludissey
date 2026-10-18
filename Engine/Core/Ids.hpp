#pragma once

#include <functional>
#include <string>

namespace ld {

template <typename Tag>
class StrongId {
public:
    StrongId() = default;
    explicit StrongId(std::string text) : raw(std::move(text)) {}
    const std::string& text() const { return raw; }
    bool empty() const { return raw.empty(); }
    bool operator<(const StrongId& other) const { return raw < other.raw; }
    bool operator==(const StrongId&) const = default;

private:
    std::string raw;
};

struct PlayerTag {};
struct ExperienceTag {};
struct ServerTag {};
struct SessionTag {};
struct AssetTag {};

using PlayerId = StrongId<PlayerTag>;
using ExperienceId = StrongId<ExperienceTag>;
using ServerId = StrongId<ServerTag>;
using SessionId = StrongId<SessionTag>;
using AssetId = StrongId<AssetTag>;

}

template <typename Tag>
struct std::hash<ld::StrongId<Tag>> {
    std::size_t operator()(const ld::StrongId<Tag>& id) const noexcept { return std::hash<std::string>{}(id.text()); }
};
