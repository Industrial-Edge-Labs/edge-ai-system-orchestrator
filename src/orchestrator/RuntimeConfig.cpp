#include "RuntimeConfig.hpp"

#include <cstdlib>
#include <iostream>

namespace {

bool env_flag(const char* name, bool default_value = false) {
    if (const char* raw = std::getenv(name)) {
        const std::string value(raw);
        return value == "1" || value == "true" || value == "TRUE"
            || value == "yes" || value == "YES" || value == "on" || value == "ON";
    }
    return default_value;
}

bool parse_u32(const std::string& value, uint32_t& output) {
    try {
        size_t consumed = 0;
        output = static_cast<uint32_t>(std::stoul(value, &consumed, 10));
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

bool parse_u64(const std::string& value, uint64_t& output) {
    try {
        size_t consumed = 0;
        output = std::stoull(value, &consumed, 10);
        return consumed == value.size();
    } catch (...) {
        return false;
    }
}

std::optional<std::string> read_option_value(const std::vector<std::string>& args, size_t& index) {
    const auto separator = args[index].find('=');
    if (separator != std::string::npos) {
        return args[index].substr(separator + 1);
    }

    if (index + 1 >= args.size()) {
        return std::nullopt;
    }

    ++index;
    return args[index];
}

void print_usage(std::ostream& output) {
    output
        << "Usage: edge_orchestrator [--dry-run] [--ticks N] [--target-hz N]\n"
        << "                         [--decision-endpoint tcp://host:port]\n"
        << "                         [--control-endpoint tcp://host:port]\n"
        << "                         [--no-decision-input] [--no-control-plane]\n";
}

} // namespace

namespace edge_orchestrator {

std::optional<SchedulerConfig> parse_runtime_config_args(
    const std::vector<std::string>& args,
    const RuntimeEnvironment& environment,
    std::ostream& diagnostics
) {
    SchedulerConfig config{};

    if (environment.decision_endpoint.has_value()) {
        config.decision_endpoint = *environment.decision_endpoint;
    }
    if (environment.control_endpoint.has_value()) {
        config.control_endpoint = *environment.control_endpoint;
    }
    if (environment.target_hz.has_value()) {
        parse_u32(*environment.target_hz, config.target_hz);
    }
    if (environment.max_ticks.has_value()) {
        parse_u64(*environment.max_ticks, config.max_ticks);
    }

    config.dry_run = environment.dry_run;
    config.enable_decision_input = !environment.disable_decision_input;
    config.enable_control_plane = !environment.disable_control_plane;

    for (size_t index = 0; index < args.size(); ++index) {
        const std::string& arg = args[index];

        if (arg == "--help" || arg == "-h") {
            print_usage(diagnostics);
            return std::nullopt;
        }
        if (arg == "--dry-run") {
            config.dry_run = true;
            continue;
        }
        if (arg == "--no-decision-input") {
            config.enable_decision_input = false;
            continue;
        }
        if (arg == "--no-control-plane") {
            config.enable_control_plane = false;
            continue;
        }
        if (arg.rfind("--ticks", 0) == 0) {
            const auto value = read_option_value(args, index);
            if (!value || !parse_u64(*value, config.max_ticks)) {
                diagnostics << "[Orchestrator] Invalid value for --ticks.\n";
                return std::nullopt;
            }
            continue;
        }
        if (arg.rfind("--target-hz", 0) == 0) {
            const auto value = read_option_value(args, index);
            if (!value || !parse_u32(*value, config.target_hz)) {
                diagnostics << "[Orchestrator] Invalid value for --target-hz.\n";
                return std::nullopt;
            }
            continue;
        }
        if (arg.rfind("--decision-endpoint", 0) == 0) {
            const auto value = read_option_value(args, index);
            if (!value) {
                diagnostics << "[Orchestrator] Missing value for --decision-endpoint.\n";
                return std::nullopt;
            }
            config.decision_endpoint = *value;
            continue;
        }
        if (arg.rfind("--control-endpoint", 0) == 0) {
            const auto value = read_option_value(args, index);
            if (!value) {
                diagnostics << "[Orchestrator] Missing value for --control-endpoint.\n";
                return std::nullopt;
            }
            config.control_endpoint = *value;
            continue;
        }

        diagnostics << "[Orchestrator] Unknown argument: " << arg << "\n";
        return std::nullopt;
    }

    if (config.target_hz == 0) {
        diagnostics << "[Orchestrator] --target-hz must be positive.\n";
        return std::nullopt;
    }

    if (config.dry_run) {
        config.enable_decision_input = false;
        config.enable_control_plane = false;
    }

#if !EDGE_ORCHESTRATOR_USE_ZMQ
    if (config.enable_decision_input || config.enable_control_plane) {
        diagnostics << "[Orchestrator] Built without ZeroMQ support. Falling back to dry-run mode.\n";
        config.enable_decision_input = false;
        config.enable_control_plane = false;
        config.dry_run = true;
    }
#endif

    return config;
}

std::optional<SchedulerConfig> parse_runtime_config(int argc, char** argv) {
    RuntimeEnvironment environment{};
    if (const char* env_decision = std::getenv("EDGE_ORCHESTRATOR_DECISION_ENDPOINT")) {
        environment.decision_endpoint = env_decision;
    }
    if (const char* env_control = std::getenv("EDGE_ORCHESTRATOR_CONTROL_ENDPOINT")) {
        environment.control_endpoint = env_control;
    }
    if (const char* env_target_hz = std::getenv("EDGE_ORCHESTRATOR_TARGET_HZ")) {
        environment.target_hz = env_target_hz;
    }
    if (const char* env_ticks = std::getenv("EDGE_ORCHESTRATOR_MAX_TICKS")) {
        environment.max_ticks = env_ticks;
    }

    environment.dry_run = env_flag("EDGE_ORCHESTRATOR_DRY_RUN", false);
    environment.disable_decision_input = env_flag("EDGE_ORCHESTRATOR_DISABLE_DECISION_INPUT", false);
    environment.disable_control_plane = env_flag("EDGE_ORCHESTRATOR_DISABLE_CONTROL_PLANE", false);

    std::vector<std::string> args;
    args.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }

    return parse_runtime_config_args(args, environment, std::cout);
}

} // namespace edge_orchestrator
