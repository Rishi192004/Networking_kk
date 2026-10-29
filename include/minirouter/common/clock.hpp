#pragma once

#include <chrono>
#include <memory>
#include <cstdint>

namespace minirouter {

// Abstract clock interface for deterministic unit testing and production system clock
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
    virtual uint64_t now_ms() const = 0;
};

class SystemClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return std::chrono::steady_clock::now();
    }

    uint64_t now_ms() const override {
        auto dur = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(dur).count());
    }
};

class SimulatedClock : public IClock {
public:
    explicit SimulatedClock(uint64_t initial_ms = 1000)
        : current_ms_(initial_ms) {}

    std::chrono::steady_clock::time_point now() const override {
        return std::chrono::steady_clock::time_point(std::chrono::milliseconds(current_ms_));
    }

    uint64_t now_ms() const override {
        return current_ms_;
    }

    void advance_ms(uint64_t ms) {
        current_ms_ += ms;
    }

    void set_ms(uint64_t ms) {
        current_ms_ = ms;
    }

private:
    uint64_t current_ms_{1000};
};

} // namespace minirouter
