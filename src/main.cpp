#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "orchestrator/CoreScheduler.hpp"
#include "orchestrator/RuntimeConfig.hpp"

namespace {

edge_orchestrator::CoreScheduler* g_scheduler = nullptr;

void signal_handler(int signal_number) {
    if (g_scheduler != nullptr) {
        std::cout << "[Orchestrator] Caught termination signal " << signal_number
                  << ". Commencing deterministic halt.\n";
        g_scheduler->stop();
    }
}

} // namespace

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
