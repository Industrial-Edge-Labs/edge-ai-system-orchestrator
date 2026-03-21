#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace edge_orchestrator {

enum class GlobalState : uint8_t {
    IDLE = 0,
    PERIMETER_BREACH = 1,
    AUTHORIZATION_PENDING = 2,
    EMERGENCY_HALT = 3
};

// C-Struct mimicking the FSM state changes from #2
#pragma pack(push, 1)
struct FsmPayload {
    uint64_t timestamp;
    GlobalState current_state;
    // Padded to ensure memory alignment
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

class CoreScheduler {
public:
    CoreScheduler(size_t worker_threads = 4);
    ~CoreScheduler();

    CoreScheduler(const CoreScheduler&) = delete;
    CoreScheduler& operator=(const CoreScheduler&) = delete;

    void start_rt_loop(uint32_t target_hz, uint64_t max_ticks = 0);
    void stop();

private:
    std::atomic<bool> is_running_{false};
    std::vector<std::thread> workers_;
    
    // ZMQ Contexts
    void* zmq_context_{nullptr};
    void* zmq_sub_fsm_{nullptr}; // Connects to #2
    void* zmq_rep_ctrl_{nullptr}; // Listens to #6
    
    // Lock-free config parameter swapped atomically by the REP network thread
    std::atomic<double> max_velocity_bound_{100.0};
    std::atomic<double> min_vision_conf_{0.85};
    std::atomic<uint32_t> tick_budget_us_{1000};
    std::atomic<bool> emergency_stop_{false};
    std::atomic<uint64_t> profile_revision_{0};
    std::atomic<uint8_t> last_fsm_state_{static_cast<uint8_t>(GlobalState::IDLE)};

    alignas(64) std::atomic<uint64_t> current_tick_{0};

    void pin_thread_to_core(std::thread& t, int core_id);
    void execution_tick();
    void listen_control_plane();
    static const char* state_to_string(GlobalState state);
};

} // namespace edge_orchestrator
