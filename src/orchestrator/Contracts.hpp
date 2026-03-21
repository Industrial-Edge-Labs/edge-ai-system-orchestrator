#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace edge_orchestrator {

enum class GlobalState : uint8_t {
    IDLE = 0,
    PERIMETER_BREACH = 1,
    AUTHORIZATION_PENDING = 2,
    EMERGENCY_HALT = 3
};

#pragma pack(push, 1)
struct FsmPayload {
    uint64_t timestamp;
    GlobalState current_state;
};

struct ControlConfig {
    double max_velocity_rad;
    double min_vision_confidence;
    uint32_t tick_budget_us;
    uint8_t emergency_stop;
    uint8_t reserved[3];
    uint64_t profile_revision;
};
#pragma pack(pop)

static_assert(sizeof(FsmPayload) == 9, "FsmPayload must remain a 9-byte wire contract.");
static_assert(sizeof(ControlConfig) == 32, "ControlConfig must remain a 32-byte wire contract.");

inline bool is_valid_state(GlobalState state) {
    return static_cast<uint8_t>(state) <= static_cast<uint8_t>(GlobalState::EMERGENCY_HALT);
}

inline bool is_valid_fsm_payload(const FsmPayload& payload) {
    return payload.timestamp > 0 && is_valid_state(payload.current_state);
}

inline bool is_valid_control_config(const ControlConfig& cfg) {
    return std::isfinite(cfg.max_velocity_rad)
        && std::isfinite(cfg.min_vision_confidence)
        && cfg.max_velocity_rad > 0.0
        && cfg.max_velocity_rad <= 500.0
        && cfg.min_vision_confidence >= 0.50
        && cfg.min_vision_confidence <= 0.999
        && cfg.tick_budget_us >= 100
        && cfg.tick_budget_us <= 10000;
}

inline const char* state_to_string(GlobalState state) {
    switch (state) {
        case GlobalState::IDLE:
            return "IDLE";
        case GlobalState::PERIMETER_BREACH:
            return "PERIMETER_BREACH";
        case GlobalState::AUTHORIZATION_PENDING:
            return "AUTHORIZATION_PENDING";
        case GlobalState::EMERGENCY_HALT:
            return "EMERGENCY_HALT";
        default:
            return "UNKNOWN";
    }
}

inline std::string make_control_ack(const ControlConfig& cfg, const char* status) {
    return std::string(status)
        + " revision=" + std::to_string(cfg.profile_revision)
        + ";max_velocity_rad=" + std::to_string(cfg.max_velocity_rad)
        + ";min_vision_confidence=" + std::to_string(cfg.min_vision_confidence)
        + ";tick_budget_us=" + std::to_string(cfg.tick_budget_us)
        + ";emergency_stop=" + std::to_string(static_cast<int>(cfg.emergency_stop));
}

} // namespace edge_orchestrator
