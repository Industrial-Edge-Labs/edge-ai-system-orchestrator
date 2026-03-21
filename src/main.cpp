#include <iostream>
#include <signal.h>
#include <cstdlib>
#include <string>
#include "orchestrator/CoreScheduler.hpp"

// Global pointer for clean exit on SIGINT
edge_orchestrator::CoreScheduler* g_scheduler = nullptr;

void signal_handler(int signal) {
    if (g_scheduler) {
        std::cout << "\n[Orchestrator] Caught SIGINT (" << signal << "). Commencing deterministic halt...\n";
        g_scheduler->stop();
    }
}

int main(int argc, char** argv) {
    // Scaffold system logic:
    // 1. Initialize EAL (DPDK / NUMA binding)
    // 2. Setup ZeroMQ Contexts mapping (REQ/REP/PUB/SUB channels)
    // 3. Initiate Lock-Free memory pools

    std::cout << "[Orchestrator] Bootstrapping Industrial Edge Control Plane...\n";
    std::cout << "[Orchestrator] Allocating Zero-Copy Memory Pages (NVMM & DPDK)...\n";

    edge_orchestrator::CoreScheduler scheduler(4 /* isolated CPU cores */);
    g_scheduler = &scheduler;

    // Register OS interrupt trap
    std::signal(SIGINT, signal_handler);

    // Hard real-time constraints: 1000Hz (1ms budget per cyclic tick)
    constexpr uint32_t CONTROL_FREQUENCY_HZ = 1000;
    uint64_t max_ticks = 0;

    if (const char* env_ticks = std::getenv("EDGE_ORCHESTRATOR_MAX_TICKS")) {
        max_ticks = std::strtoull(env_ticks, nullptr, 10);
    }

    if (argc > 1) {
        max_ticks = std::strtoull(argv[1], nullptr, 10);
    }

    scheduler.start_rt_loop(CONTROL_FREQUENCY_HZ, max_ticks);

    std::cout << "[Orchestrator] Kernel Halted Safely. Releasing NUMA bindings.\n";
    return EXIT_SUCCESS;
}
