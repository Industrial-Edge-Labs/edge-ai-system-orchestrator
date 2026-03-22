#pragma once

#include <cstdint>
#include <optional>

#include "Contracts.hpp"

namespace edge_orchestrator {

std::optional<ControlConfig> synthetic_control_config_for_tick(uint64_t tick);
std::optional<FsmPayload> synthetic_fsm_payload_for_tick(uint64_t tick, uint64_t timestamp);

} // namespace edge_orchestrator
