#include <iostream>
#include <sstream>
#include <vector>

#include "orchestrator/Contracts.hpp"
#include "orchestrator/RuntimeConfig.hpp"
#include "orchestrator/SyntheticInputs.hpp"

namespace {

bool test_dry_run_disables_network_inputs() {
    edge_orchestrator::RuntimeEnvironment environment{};
    std::ostringstream diagnostics;
    const auto config = edge_orchestrator::parse_runtime_config_args(
        {"--dry-run", "--ticks", "24"},
        environment,
        diagnostics
    );

    return config.has_value()
        && config->dry_run
        && !config->enable_decision_input
        && !config->enable_control_plane
        && config->max_ticks == 24;
}

bool test_invalid_target_hz_is_rejected() {
    edge_orchestrator::RuntimeEnvironment environment{};
    std::ostringstream diagnostics;
    const auto config = edge_orchestrator::parse_runtime_config_args(
        {"--target-hz", "0"},
        environment,
        diagnostics
    );

    return !config.has_value();
}

bool test_synthetic_control_packet() {
    const auto cfg = edge_orchestrator::synthetic_control_config_for_tick(1);
    return cfg.has_value()
        && edge_orchestrator::is_valid_control_config(*cfg)
        && cfg->profile_revision == 1;
}

bool test_synthetic_fsm_sequence() {
    const auto perimeter = edge_orchestrator::synthetic_fsm_payload_for_tick(4, 100);
    const auto authorization = edge_orchestrator::synthetic_fsm_payload_for_tick(8, 101);
    const auto emergency = edge_orchestrator::synthetic_fsm_payload_for_tick(16, 102);

    return perimeter.has_value()
        && authorization.has_value()
        && emergency.has_value()
        && perimeter->current_state == edge_orchestrator::GlobalState::PERIMETER_BREACH
        && authorization->current_state == edge_orchestrator::GlobalState::AUTHORIZATION_PENDING
        && emergency->current_state == edge_orchestrator::GlobalState::EMERGENCY_HALT;
}

} // namespace

int main() {
    if (!test_dry_run_disables_network_inputs()) {
        std::cerr << "test_dry_run_disables_network_inputs failed\n";
        return 1;
    }
    if (!test_invalid_target_hz_is_rejected()) {
        std::cerr << "test_invalid_target_hz_is_rejected failed\n";
        return 1;
    }
    if (!test_synthetic_control_packet()) {
        std::cerr << "test_synthetic_control_packet failed\n";
        return 1;
    }
    if (!test_synthetic_fsm_sequence()) {
        std::cerr << "test_synthetic_fsm_sequence failed\n";
        return 1;
    }

    std::cout << "orchestrator_tests passed\n";
    return 0;
}
