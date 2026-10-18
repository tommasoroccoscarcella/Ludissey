#pragma once

#include "Engine/Avatar/Avatar.hpp"
#include "Engine/Core/Ids.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Result.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

struct AvatarSnapshot {
    double x = 0, y = 0, z = 0, yaw = 0;
    bool moving = false;
    double time = 0;
    int sequence = 0;
    bool operator==(const AvatarSnapshot&) const = default;
    Json json() const;
    static Result<AvatarSnapshot> fromJson(const Json& value);
};

struct ReplicationFrame {
    std::optional<std::string> player;
    std::string name;
    AvatarSnapshot snapshot;
    std::optional<AvatarConfiguration> avatar;
    std::optional<std::uint16_t> endpoint;
    int version = 1;
    bool operator==(const ReplicationFrame&) const = default;
    Json json() const;
    static Result<ReplicationFrame> fromJson(const Json& value);
};

namespace replication_protocol {
inline constexpr int version = 1;
inline constexpr char separator = '\n';
inline constexpr std::size_t maximumPacket = 16 * 1024;
std::string encode(const ReplicationFrame& frame);
std::optional<ReplicationFrame> decode(std::string_view raw);
std::string packet(const SessionId& session, const ReplicationFrame& frame);
std::optional<std::pair<SessionId, ReplicationFrame>> unpack(std::string_view data);
}

struct RemoteAvatar {
    static constexpr double minimumDelay = 0.1;
    SessionId session;
    std::string name;
    std::optional<AvatarConfiguration> avatar;
    std::vector<AvatarSnapshot> snapshots;
    std::optional<double> offset;
    double received = 0;
    int discarded = 0;
    std::optional<double> spacing;

    double delay() const;
    void receive(const AvatarSnapshot& snapshot, double local);
    std::optional<AvatarSnapshot> sample(double local) const;
};

class ReplicationTransport {
public:
    virtual ~ReplicationTransport() = default;
    virtual int interval() const = 0;
    virtual Result<void> send(const ReplicationFrame& frame) = 0;
    virtual Result<std::map<SessionId, ReplicationFrame>> receive() = 0;
};

class DirectTransport final : public ReplicationTransport {
public:
    static constexpr int rate = 20;
    static Result<std::unique_ptr<DirectTransport>> open(SessionId session);
    ~DirectTransport() override;
    int interval() const override { return 1000 / rate; }
    Result<void> send(const ReplicationFrame& frame) override;
    Result<std::map<SessionId, ReplicationFrame>> receive() override;
    std::optional<double> arrival(const SessionId& session);
    void connect(const SessionId& session, std::uint16_t port);
    void forget(const std::set<SessionId>& keeping);
    std::uint16_t port() const { return bound; }
    void close();

private:
    DirectTransport() = default;
    void drain();
    SessionId self;
    int socketHandle = -1;
    std::uint16_t bound = 0;
    std::mutex lock;
    std::map<SessionId, std::uint16_t> peers;
    std::map<SessionId, ReplicationFrame> inbox;
    std::map<SessionId, double> arrivals;
};

double wallClock();

}
