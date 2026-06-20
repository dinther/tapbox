#pragma once
#include <stdint.h>
#include "esp_timer.h"

static inline uint32_t _tap_now_ms() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

enum class TapState { COLD_START, RUNNING };

struct TapResult {
    bool newSession;  // tap 1 of a new session
    bool bpmChanged;  // bpm() has a new value to push to Link
    bool wentLive;    // cold start → running: enable Link now
};

class TapTempo {
public:
    TapResult tap() {
        uint32_t now = _tap_now_ms();
        TapResult r  = {};

        if (_lastTap > 0 && (now - _lastTap) > TIMEOUT_MS) {
            _tapCount = 0;
        }

        _lastTap = now;
        _tapCount++;

        if (_tapCount == 1) {
            _sessionStart = now;
            r.newSession  = true;
            return r;
        }

        uint32_t elapsed = now - _sessionStart;
        double   bpm     = _clamp((_tapCount - 1) * 60000.0 / elapsed, BPM_MIN, BPM_MAX);

        if (_tapCount >= 4) {
            bool wasRunning = (_state == TapState::RUNNING);
            _activeBpm   = bpm;
            _state       = TapState::RUNNING;
            r.bpmChanged = true;
            r.wentLive   = !wasRunning;
        }

        return r;
    }

    void setBpm(double bpm) { _activeBpm = _clamp(bpm, BPM_MIN, BPM_MAX); }

    double   bpm()            const { return _activeBpm; }
    TapState state()          const { return _state; }
    uint32_t sessionStartMs() const { return _sessionStart; }

private:
    static constexpr uint32_t TIMEOUT_MS = 2000;
    static constexpr double   BPM_MIN    = 40.0;
    static constexpr double   BPM_MAX    = 360.0;

    static double _clamp(double v, double lo, double hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    TapState _state        = TapState::COLD_START;
    uint32_t _sessionStart = 0;
    uint32_t _lastTap      = 0;
    uint32_t _tapCount     = 0;
    double   _activeBpm    = 120.0;
};
