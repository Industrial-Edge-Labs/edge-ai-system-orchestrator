#include "SyntheticInputs.hpp"

namespace edge_orchestrator {

std::optional<ControlConfig> synthetic_control_config_for_tick(uint64_t tick) {
    if (tick != 1) {
        return std::nullopt;
    }

    ControlConfig cfg{};
    cfg.max_velocity_rad = 120.0;
    cfg.min_vision_confidence = 0.88;
    cfg.tick_budget_us = 1000;
    cfg.emergency_stop = 0;
    cfg.profile_revision = 1;
    return cfg;
}

std::optional<FsmPayload> synthetic_fsm_payload_for_tick(uint64_t tick, uint64_t timestamp) {
    switch (tick) {
        case 4:
            return FsmPayload{timestamp, GlobalState::PERIMETER_BREACH};
        case 8:
            return FsmPayload{timestamp, GlobalState::AUTHORIZATION_PENDING};
        case 16:
            return FsmPayload{timestamp, GlobalState::EMERGENCY_HALT};
        default:
            return std::nullopt;
    }
}

} // namespace edge_orchestrator
