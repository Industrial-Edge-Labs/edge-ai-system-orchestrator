#pragma once

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Contracts.hpp"

namespace edge_orchestrator {

struct SchedulerConfig {
    std::string decision_endpoint{"tcp://127.0.0.1:5556"};
    std::string control_endpoint{"tcp://127.0.0.1:5557"};
    uint32_t target_hz{1000};
    uint64_t max_ticks{0};
    bool dry_run{false};
    bool enable_decision_input{true};
    bool enable_control_plane{true};
};

std::optional<SchedulerConfig> parse_runtime_config(int argc, char** argv);

class CoreScheduler {
public:
    explicit CoreScheduler(SchedulerConfig config = {});
    ~CoreScheduler();

    CoreScheduler(const CoreScheduler&) = delete;
    CoreScheduler& operator=(const CoreScheduler&) = delete;

    void start_rt_loop(uint32_t target_hz, uint64_t max_ticks = 0);
    void stop();

private:
    SchedulerConfig config_{};
    std::atomic<bool> is_running_{false};
    std::vector<std::thread> workers_;

    void* zmq_context_{nullptr};
    void* zmq_sub_fsm_{nullptr};
    void* zmq_rep_ctrl_{nullptr};

    std::atomic<double> max_velocity_bound_{100.0};
    std::atomic<double> min_vision_conf_{0.85};
    std::atomic<uint32_t> tick_budget_us_{1000};
    std::atomic<bool> decision_emergency_latched_{false};
    std::atomic<bool> control_plane_emergency_{false};
    std::atomic<uint64_t> profile_revision_{0};
    std::atomic<uint8_t> last_fsm_state_{static_cast<uint8_t>(GlobalState::IDLE)};
    std::atomic<uint64_t> current_tick_{0};

    void pin_thread_to_core(std::thread& thread_handle, int core_id);
    void execution_tick();
    void poll_decision_bus();
    void listen_control_plane();
    void apply_fsm_payload(const FsmPayload& payload, const char* source);
    void apply_control_config(const ControlConfig& cfg, const char* source);
    void inject_synthetic_inputs(uint64_t tick);

    static uint64_t now_ns();
};

} // namespace edge_orchestrator
