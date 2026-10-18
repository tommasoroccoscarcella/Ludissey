#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace ld {

template <typename... Args>
class Signal {
public:
    using Handler = std::function<void(Args...)>;

    class Connection {
    public:
        Connection() = default;
        void disconnect() {
            if (auto slot = entry.lock()) slot->alive = false;
        }
        bool connected() const {
            auto slot = entry.lock();
            return slot && slot->alive;
        }

    private:
        friend class Signal;
        struct Slot {
            Handler handler;
            bool alive = true;
        };
        std::weak_ptr<Slot> entry;
    };

    Connection connect(Handler handler) {
        auto slot = std::make_shared<typename Connection::Slot>();
        slot->handler = std::move(handler);
        slots.push_back(slot);
        Connection connection;
        connection.entry = slot;
        return connection;
    }

    void emit(Args... args) {
        auto current = slots;
        for (auto& slot : current)
            if (slot->alive) slot->handler(args...);
        slots.erase(std::remove_if(slots.begin(), slots.end(), [](const auto& slot) { return !slot->alive; }), slots.end());
    }

    std::size_t count() const {
        return static_cast<std::size_t>(std::count_if(slots.begin(), slots.end(), [](const auto& slot) { return slot->alive; }));
    }

    void clear() {
        for (auto& slot : slots) slot->alive = false;
        slots.clear();
    }

private:
    std::vector<std::shared_ptr<typename Connection::Slot>> slots;
};

}
