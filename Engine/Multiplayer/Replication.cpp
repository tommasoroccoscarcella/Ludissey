#include "Replication.hpp"

#include "Engine/Character/Character.hpp"
#include "Localization/Localization.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ld {

double wallClock() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

Json AvatarSnapshot::json() const {
    return Json::object({{"x", x}, {"y", y}, {"z", z}, {"yaw", yaw}, {"moving", moving}, {"time", time}, {"sequence", sequence}});
}

Result<AvatarSnapshot> AvatarSnapshot::fromJson(const Json& value) {
    AvatarSnapshot s;
    for (auto [key, target] : {std::pair{"x", &s.x}, std::pair{"y", &s.y}, std::pair{"z", &s.z}, std::pair{"yaw", &s.yaw}, std::pair{"time", &s.time}}) {
        const Json& item = value[key];
        if (!item.isNumber() || !std::isfinite(item.number())) return fail(ErrorCode::corrupted, tr("Snapshot non valido"));
        *target = item.number();
    }
    if (!value["moving"].isBool()) return fail(ErrorCode::corrupted, tr("Snapshot non valido"));
    s.moving = value["moving"].boolean();
    if (const Json* sequence = value.find("sequence"); sequence && !sequence->isNull()) {
        if (!sequence->isNumber() || std::floor(sequence->number()) != sequence->number()) return fail(ErrorCode::corrupted, tr("Snapshot non valido"));
        s.sequence = static_cast<int>(sequence->number());
    }
    return s;
}

Json ReplicationFrame::json() const {
    Json out = Json::object();
    if (player) out.set("player", *player);
    out.set("name", name);
    out.set("snapshot", snapshot.json());
    if (avatar) out.set("avatar", avatar->json());
    if (endpoint) out.set("endpoint", static_cast<int>(*endpoint));
    out.set("version", version);
    return out;
}

Result<ReplicationFrame> ReplicationFrame::fromJson(const Json& value) {
    if (!value.isObject() || !value["name"].isString()) return fail(ErrorCode::corrupted, tr("Frame non valido"));
    ReplicationFrame frame;
    frame.player = value.optionalString("player");
    frame.name = value["name"].string();
    auto snapshot = AvatarSnapshot::fromJson(value["snapshot"]);
    if (!snapshot) return snapshot.error();
    frame.snapshot = *snapshot;
    if (const Json* avatar = value.find("avatar"); avatar && !avatar->isNull()) {
        auto parsed = AvatarConfiguration::fromJson(*avatar);
        if (!parsed) return parsed.error();
        frame.avatar = *parsed;
    }
    if (const Json* endpoint = value.find("endpoint"); endpoint && !endpoint->isNull()) {
        double port = endpoint->number(-1);
        if (port < 0 || port > 65535 || std::floor(port) != port) return fail(ErrorCode::corrupted, tr("Porta non valida"));
        frame.endpoint = static_cast<std::uint16_t>(port);
    }
    frame.version = static_cast<int>(value["version"].integer(replication_protocol::version));
    return frame;
}

namespace replication_protocol {

std::string encode(const ReplicationFrame& frame) { return frame.json().dump(); }

std::optional<ReplicationFrame> decode(std::string_view raw) {
    if (raw.size() > maximumPacket) return std::nullopt;
    auto json = Json::parse(raw, 32);
    if (!json) return std::nullopt;
    auto frame = ReplicationFrame::fromJson(*json);
    if (!frame || frame->version != version) return std::nullopt;
    return *frame;
}

std::string packet(const SessionId& session, const ReplicationFrame& frame) { return session.text() + separator + encode(frame); }

std::optional<std::pair<SessionId, ReplicationFrame>> unpack(std::string_view data) {
    auto split = data.find(separator);
    if (split == std::string_view::npos) return std::nullopt;
    auto frame = decode(data.substr(split + 1));
    if (!frame) return std::nullopt;
    return std::pair{SessionId(std::string(data.substr(0, split))), *frame};
}

}

double RemoteAvatar::delay() const {
    double fallback = rules::replication::delayMilliseconds / 1000.0;
    if (!spacing) return fallback;
    return std::min(fallback, std::max(minimumDelay, *spacing * 2.5));
}

void RemoteAvatar::receive(const AvatarSnapshot& snapshot, double local) {
    if (!snapshots.empty()) {
        const AvatarSnapshot& last = snapshots.back();
        if ((snapshot.sequence <= last.sequence && (snapshot.sequence > 0 || last.sequence > 0)) || snapshot.time <= last.time) {
            ++discarded;
            return;
        }
        double gap = snapshot.time - last.time;
        spacing = spacing ? *spacing * 0.8 + gap * 0.2 : gap;
    }
    double skew = local - snapshot.time;
    offset = std::min(offset.value_or(skew), skew);
    snapshots.push_back(snapshot);
    if (snapshots.size() > static_cast<std::size_t>(rules::replication::snapshots))
        snapshots.erase(snapshots.begin(), snapshots.begin() + static_cast<long>(snapshots.size() - rules::replication::snapshots));
    received = local;
}

std::optional<AvatarSnapshot> RemoteAvatar::sample(double local) const {
    if (snapshots.empty()) return std::nullopt;
    const AvatarSnapshot& first = snapshots.front();
    const AvatarSnapshot& last = snapshots.back();
    double time = local - offset.value_or(0) - delay();
    if (time <= first.time) return first;
    if (time >= last.time) return last;
    for (std::size_t index = 1; index < snapshots.size(); ++index) {
        if (snapshots[index].time < time) continue;
        const AvatarSnapshot& a = snapshots[index - 1];
        const AvatarSnapshot& b = snapshots[index];
        double t = (time - a.time) / std::max(1e-9, b.time - a.time);
        AvatarSnapshot s;
        s.x = a.x + (b.x - a.x) * t;
        s.y = a.y + (b.y - a.y) * t;
        s.z = a.z + (b.z - a.z) * t;
        s.yaw = a.yaw + motion::shortestAngle(a.yaw, b.yaw) * t;
        s.moving = t < 0.5 ? a.moving : b.moving;
        s.time = time;
        return s;
    }
    return last;
}

Result<std::unique_ptr<DirectTransport>> DirectTransport::open(SessionId session) {
    std::unique_ptr<DirectTransport> transport(new DirectTransport());
    transport->self = std::move(session);
    int handle = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (handle < 0) return fail(ErrorCode::unavailable, tr("Porta di replica locale non disponibile"));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(handle, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        ::close(handle);
        return fail(ErrorCode::unavailable, tr("Porta di replica locale non disponibile"));
    }
    socklen_t length = sizeof address;
    ::getsockname(handle, reinterpret_cast<sockaddr*>(&address), &length);
    ::fcntl(handle, F_SETFL, ::fcntl(handle, F_GETFL, 0) | O_NONBLOCK);
    transport->socketHandle = handle;
    transport->bound = ntohs(address.sin_port);
    if (transport->bound == 0) return fail(ErrorCode::unavailable, tr("Porta di replica locale non disponibile"));
    return transport;
}

DirectTransport::~DirectTransport() { close(); }

void DirectTransport::close() {
    std::lock_guard guard(lock);
    if (socketHandle >= 0) ::close(socketHandle);
    socketHandle = -1;
    peers.clear();
}

void DirectTransport::drain() {
    char buffer[replication_protocol::maximumPacket + 1];
    while (socketHandle >= 0) {
        ssize_t count = ::recv(socketHandle, buffer, sizeof buffer, 0);
        if (count <= 0) break;
        if (static_cast<std::size_t>(count) > replication_protocol::maximumPacket) continue;
        auto unpacked = replication_protocol::unpack(std::string_view(buffer, static_cast<std::size_t>(count)));
        if (!unpacked || unpacked->first == self) continue;
        auto existing = inbox.find(unpacked->first);
        int previous = existing == inbox.end() ? -1 : existing->second.snapshot.sequence;
        if (previous < unpacked->second.snapshot.sequence) {
            inbox[unpacked->first] = unpacked->second;
            arrivals[unpacked->first] = wallClock();
        }
    }
}

Result<void> DirectTransport::send(const ReplicationFrame& frame) {
    std::lock_guard guard(lock);
    if (socketHandle < 0) return fail(ErrorCode::unavailable, "Trasporto chiuso");
    std::string data = replication_protocol::packet(self, frame);
    for (const auto& [session, port] : peers) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::sendto(socketHandle, data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&address), sizeof address);
    }
    return {};
}

Result<std::map<SessionId, ReplicationFrame>> DirectTransport::receive() {
    std::lock_guard guard(lock);
    drain();
    return inbox;
}

std::optional<double> DirectTransport::arrival(const SessionId& session) {
    std::lock_guard guard(lock);
    drain();
    auto it = arrivals.find(session);
    if (it == arrivals.end()) return std::nullopt;
    return it->second;
}

void DirectTransport::connect(const SessionId& session, std::uint16_t port) {
    if (session == self || port == 0) return;
    std::lock_guard guard(lock);
    peers[session] = port;
}

void DirectTransport::forget(const std::set<SessionId>& keeping) {
    std::lock_guard guard(lock);
    for (auto it = peers.begin(); it != peers.end();) {
        if (!keeping.count(it->first)) {
            inbox.erase(it->first);
            arrivals.erase(it->first);
            it = peers.erase(it);
        } else ++it;
    }
}

}
