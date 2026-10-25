#pragma once
#include <cstdint>

// Detects a guest spinning on the millisecond clock: a frame limiter that
// reads GetTickCount in a tight loop until the value moves. Each observation
// that sees no time pass counts toward a spin; after kSpinPolls of them
// observe() asks the caller to sleep for a millisecond instead, which frees
// the core without changing any value the guest reads. A caller that must
// not sleep passes may_sleep = false; that poll neither counts nor clears
// the spin. One instance per guest thread.
class ClockSpin {
    uint32_t last_ms_ = 0, same_ms_polls_ = 0;

  public:
    static constexpr uint32_t kSpinPolls = 16;
    bool observe(uint32_t now_ms, bool may_sleep) {
        if (!may_sleep)
            return false;
        if (now_ms != last_ms_) {
            last_ms_ = now_ms;
            same_ms_polls_ = 0;
            return false;
        }
        if (++same_ms_polls_ < kSpinPolls)
            return false;
        same_ms_polls_ = 0;
        return true;
    }
    // After the sleep the caller reads the clock again; that is the new start.
    void slept(uint32_t now_ms) {
        last_ms_ = now_ms;
        same_ms_polls_ = 0;
    }
};
