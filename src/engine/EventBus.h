#pragma once
// =============================================================================
// EventBus.h – Lightweight event queue + subscribers
// =============================================================================

#include <functional>
#include <string>
#include <vector>

namespace myu::engine {

struct Event {
    std::string name;
    std::string payload; // JSON or simple string
};

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    void subscribe(Handler h) { handlers_.push_back(std::move(h)); }

    void emit(const std::string& name, const std::string& payload = "") {
        queue_.push_back({name, payload});
    }

    void dispatch() {
        for (const auto& e : queue_) {
            for (const auto& h : handlers_)
                h(e);
        }
        queue_.clear();
    }

private:
    std::vector<Event>   queue_;
    std::vector<Handler> handlers_;
};

} // namespace myu::engine
