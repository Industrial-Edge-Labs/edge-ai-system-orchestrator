#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "CoreScheduler.hpp"

namespace edge_orchestrator {

struct RuntimeEnvironment {
    std::optional<std::string> decision_endpoint;
    std::optional<std::string> control_endpoint;
    std::optional<std::string> target_hz;
    std::optional<std::string> max_ticks;
    bool dry_run{false};
    bool disable_decision_input{false};
    bool disable_control_plane{false};
};

std::optional<SchedulerConfig> parse_runtime_config_args(
    const std::vector<std::string>& args,
    const RuntimeEnvironment& environment,
    std::ostream& diagnostics
);

std::optional<SchedulerConfig> parse_runtime_config(int argc, char** argv);

} // namespace edge_orchestrator
