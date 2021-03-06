#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace eka2l1 {
    struct debug_breakpoint {
        std::uint32_t addr;
        bool is_hit;
    };

    class debugger_base {
    protected:
        std::vector<debug_breakpoint> breakpoints;
        std::mutex lock;

    public:
        debugger_base() {}

        std::function<void(bool)> on_pause_toogle;

        virtual bool should_emulate_stop() = 0;

        virtual void set_breakpoint(const std::uint32_t bkpt, const bool hit = false,
            const bool new_one = false);
        virtual void unset_breakpoint(const std::uint32_t bkpt);

        virtual void wait_for_debugger() = 0;
        virtual void notify_clients() = 0;

        std::optional<debug_breakpoint> get_nearest_breakpoint(const std::uint32_t bkpt);

        virtual void show_debugger(std::uint32_t width, std::uint32_t height, std::uint32_t fb_width,
            std::uint32_t fb_height) {}
    };
}