#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "orchestrator/CoreScheduler.hpp"

namespace {

edge_orchestrator::CoreScheduler* g_scheduler = nullptr;

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

std::optional<std::string> read_option_value(int argc, char** argv, int& index, const std::string& arg) {
    const auto separator = arg.find('=');
    if (separator != std::string::npos) {
        return arg.substr(separator + 1);
    }

    if (index + 1 >= argc) {
        return std::nullopt;
    }

    ++index;
    return std::string(argv[index]);
}

void print_usage() {
    std::cout
        << "Usage: edge_orchestrator [--dry-run] [--ticks N] [--target-hz N]\n"
        << "                         [--decision-endpoint tcp://host:port]\n"
        << "                         [--control-endpoint tcp://host:port]\n"
        << "                         [--no-decision-input] [--no-control-plane]\n";
}

void signal_handler(int signal_number) {
    if (g_scheduler != nullptr) {
        std::cout << "[Orchestrator] Caught termination signal " << signal_number
                  << ". Commencing deterministic halt.\n";
        g_scheduler->stop();
    }
}

} // namespace

namespace edge_orchestrator {

std::optional<SchedulerConfig> parse_runtime_config(int argc, char** argv) {
    SchedulerConfig config{};

    if (const char* env_decision = std::getenv("EDGE_ORCHESTRATOR_DECISION_ENDPOINT")) {
        config.decision_endpoint = env_decision;
    }
    if (const char* env_control = std::getenv("EDGE_ORCHESTRATOR_CONTROL_ENDPOINT")) {
        config.control_endpoint = env_control;
    }
    if (const char* env_target_hz = std::getenv("EDGE_ORCHESTRATOR_TARGET_HZ")) {
        parse_u32(env_target_hz, config.target_hz);
    }
    if (const char* env_ticks = std::getenv("EDGE_ORCHESTRATOR_MAX_TICKS")) {
        parse_u64(env_ticks, config.max_ticks);
    }

    config.dry_run = env_flag("EDGE_ORCHESTRATOR_DRY_RUN", false);
    config.enable_decision_input = !env_flag("EDGE_ORCHESTRATOR_DISABLE_DECISION_INPUT", false);
    config.enable_control_plane = !env_flag("EDGE_ORCHESTRATOR_DISABLE_CONTROL_PLANE", false);

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            print_usage();
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
            const auto value = read_option_value(argc, argv, i, arg);
            if (!value || !parse_u64(*value, config.max_ticks)) {
                std::cerr << "[Orchestrator] Invalid value for --ticks.\n";
                return std::nullopt;
            }
            continue;
        }
        if (arg.rfind("--target-hz", 0) == 0) {
            const auto value = read_option_value(argc, argv, i, arg);
            if (!value || !parse_u32(*value, config.target_hz)) {
                std::cerr << "[Orchestrator] Invalid value for --target-hz.\n";
                return std::nullopt;
            }
            continue;
        }
        if (arg.rfind("--decision-endpoint", 0) == 0) {
            const auto value = read_option_value(argc, argv, i, arg);
            if (!value) {
                std::cerr << "[Orchestrator] Missing value for --decision-endpoint.\n";
                return std::nullopt;
            }
            config.decision_endpoint = *value;
            continue;
        }
        if (arg.rfind("--control-endpoint", 0) == 0) {
            const auto value = read_option_value(argc, argv, i, arg);
            if (!value) {
                std::cerr << "[Orchestrator] Missing value for --control-endpoint.\n";
                return std::nullopt;
            }
            config.control_endpoint = *value;
            continue;
        }

        std::cerr << "[Orchestrator] Unknown argument: " << arg << "\n";
        return std::nullopt;
    }

    if (config.target_hz == 0) {
        std::cerr << "[Orchestrator] --target-hz must be positive.\n";
        return std::nullopt;
    }

    if (config.dry_run) {
        config.enable_decision_input = false;
        config.enable_control_plane = false;
    }

#if !EDGE_ORCHESTRATOR_USE_ZMQ
    if (config.enable_decision_input || config.enable_control_plane) {
        std::cout << "[Orchestrator] Built without ZeroMQ support. Falling back to dry-run mode.\n";
        config.enable_decision_input = false;
        config.enable_control_plane = false;
        config.dry_run = true;
    }
#endif

    return config;
}

} // namespace edge_orchestrator

int main(int argc, char** argv) {
    const auto config = edge_orchestrator::parse_runtime_config(argc, argv);
    if (!config.has_value()) {
        return 1;
    }

    std::cout << "[Orchestrator] Bootstrapping industrial edge control coordinator.\n";
    std::cout << "[Orchestrator] Target frequency " << config->target_hz << " Hz.\n";

    edge_orchestrator::CoreScheduler scheduler(*config);
    g_scheduler = &scheduler;

    std::signal(SIGINT, signal_handler);
    scheduler.start_rt_loop(config->target_hz, config->max_ticks);

    std::cout << "[Orchestrator] Runtime halted safely. Releasing scheduler resources.\n";
    return EXIT_SUCCESS;
}
