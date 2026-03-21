#include "CoreScheduler.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <zmq.h>

#ifdef __linux__
#include <pthread.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h> // Intel SSE optimizations
#endif

namespace edge_orchestrator {

namespace {

constexpr const char* kDecisionEndpoint = "tcp://127.0.0.1:5556";
constexpr const char* kControlPlaneEndpoint = "tcp://127.0.0.1:5557";

}

CoreScheduler::CoreScheduler(size_t worker_threads) {
    zmq_context_ = zmq_ctx_new();
    if (!zmq_context_) throw std::runtime_error("Failed to initialize ZMQ Context.");

    // 1. Setup ZMQ SUB for Decision Engine (#2)
    zmq_sub_fsm_ = zmq_socket(zmq_context_, ZMQ_SUB);
    if (zmq_connect(zmq_sub_fsm_, kDecisionEndpoint) != 0) {
        throw std::runtime_error("Failed to connect ZMQ SUB to Decision Engine (TCP 5556).");
    }
    zmq_setsockopt(zmq_sub_fsm_, ZMQ_SUBSCRIBE, "", 0);
    int subscriber_timeout_ms = 0;
    zmq_setsockopt(zmq_sub_fsm_, ZMQ_RCVTIMEO, &subscriber_timeout_ms, sizeof(subscriber_timeout_ms));

    // 2. Setup ZMQ REP for Operations Control Plane (#6)
    zmq_rep_ctrl_ = zmq_socket(zmq_context_, ZMQ_REP);
    if (zmq_bind(zmq_rep_ctrl_, kControlPlaneEndpoint) != 0) {
        throw std::runtime_error("Failed to bind ZMQ REP for Control Plane (TCP 5557).");
    }
    int control_timeout_ms = 100;
    int linger_ms = 0;
    zmq_setsockopt(zmq_rep_ctrl_, ZMQ_RCVTIMEO, &control_timeout_ms, sizeof(control_timeout_ms));
    zmq_setsockopt(zmq_rep_ctrl_, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
}

CoreScheduler::~CoreScheduler() {
    stop();
    if (zmq_sub_fsm_) zmq_close(zmq_sub_fsm_);
    if (zmq_rep_ctrl_) zmq_close(zmq_rep_ctrl_);
    if (zmq_context_) zmq_ctx_destroy(zmq_context_);
}

// OS Specific NUMA/CPU Pinning for production latency guarantees
void CoreScheduler::pin_thread_to_core(std::thread& t, int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
}

void CoreScheduler::start_rt_loop(uint32_t target_hz, uint64_t max_ticks) {
    is_running_.store(true, std::memory_order_release);
    const auto tick_duration_ns = std::chrono::nanoseconds(1'000'000'000 / target_hz);

    std::cout << "[Orchestrator] Core PIPELINE locked at " << target_hz 
              << " Hz (" << tick_duration_ns.count() << " ns bound).\n";
    if (max_ticks > 0) {
        std::cout << "[Orchestrator] Deterministic dry-run armed for " << max_ticks << " ticks.\n";
    }

    // Spawn async background thread to handle Control Plane JSON payloads via ZMQ REP
    workers_.emplace_back(&CoreScheduler::listen_control_plane, this);
    if (workers_.size() > 0) {
        pin_thread_to_core(workers_.back(), 1); // Pin Control Plane back-channel to Core 1
    }

    auto next_tick_time = std::chrono::steady_clock::now() + tick_duration_ns;
    
    // Hard-Real-Time loop
    while (is_running_.load(std::memory_order_acquire)) {
        const auto tick_started_at = std::chrono::steady_clock::now();
        execution_tick();

        // High precision spin-lock replacing context-switching sleep
        auto now = std::chrono::steady_clock::now();
        if (now < next_tick_time) {
            while (std::chrono::steady_clock::now() < next_tick_time) {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause(); // Intel SSE optimization to prevent CPU thermal throttle during lock
#endif
            }
        }
        
        next_tick_time += tick_duration_ns;
        const auto finished_tick = current_tick_.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto tick_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - tick_started_at
        ).count();
        const auto tick_budget = tick_budget_us_.load(std::memory_order_relaxed);

        if (tick_elapsed_us > tick_budget && finished_tick % 200 == 0) {
            std::cout << "[Orchestrator] Tick budget overrun detected: " << tick_elapsed_us
                      << " us (budget " << tick_budget << " us).\n";
        }

        if (max_ticks > 0 && finished_tick >= max_ticks) {
            std::cout << "[Orchestrator] Max deterministic ticks reached. Initiating controlled shutdown.\n";
            stop();
        }
    }
}

void CoreScheduler::execution_tick() {
    // 1. Poll Decision System (FSM) over TCP 5556 without blocking the Orchestrator
    FsmPayload fsm_state;
    int received = zmq_recv(zmq_sub_fsm_, &fsm_state, sizeof(FsmPayload), ZMQ_DONTWAIT);
    
    if (received == sizeof(FsmPayload)) {
        last_fsm_state_.store(static_cast<uint8_t>(fsm_state.current_state), std::memory_order_release);
        if (fsm_state.current_state == GlobalState::EMERGENCY_HALT) {
            std::cout << "[Orchestrator] << FATAL EMERGENCY OVERRIDE >> Received from Decision Layer.\n";
            std::cout << "[Orchestrator] Isolating Physical Plant Actuators and Cutting Kinematic Momentum.\n";
            emergency_stop_.store(true, std::memory_order_release);
        }
    }
    
    // 2. Read Lock-Free configurations dynamically altered by the VOCP Backend
    // This allows web UI users to shift thresholds safely during 1kHz motor operation
    double dyn_vel = max_velocity_bound_.load(std::memory_order_relaxed);
    double dyn_conf = min_vision_conf_.load(std::memory_order_relaxed);
    bool emergency_override = emergency_stop_.load(std::memory_order_relaxed);
    const auto tick = current_tick_.load(std::memory_order_relaxed);
    const auto active_state = static_cast<GlobalState>(last_fsm_state_.load(std::memory_order_relaxed));

    if (emergency_override) {
        if (tick % 250 == 0) {
            std::cout << "[Orchestrator] Emergency stop latched. Plant actuation remains gated.\n";
        }
        return;
    }

    if (tick % 250 == 0) {
        std::cout << "[Orchestrator] Tick=" << tick
                  << " | FSM=" << state_to_string(active_state)
                  << " | max_velocity_rad=" << dyn_vel
                  << " | min_conf=" << dyn_conf
                  << " | profile_rev=" << profile_revision_.load(std::memory_order_relaxed)
                  << "\n";
    }
}

// Background Task handling the HTTP REST mapped configs
void CoreScheduler::listen_control_plane() {
    ControlConfig cfg{};
    while (is_running_.load(std::memory_order_acquire)) {
        int received = zmq_recv(zmq_rep_ctrl_, &cfg, sizeof(ControlConfig), 0);
        
        if (received == sizeof(ControlConfig)) {
            // Apply bounds securely crossing into the execution thread via atomics
            max_velocity_bound_.store(cfg.max_velocity_rad, std::memory_order_release);
            min_vision_conf_.store(cfg.min_vision_confidence, std::memory_order_release);
            tick_budget_us_.store(cfg.tick_budget_us, std::memory_order_release);
            emergency_stop_.store(cfg.emergency_stop != 0, std::memory_order_release);
            profile_revision_.store(cfg.profile_revision, std::memory_order_release);
            
            std::cout << "[Orchestrator] Security Pass: Applied Control Plane Config -> Max Vel: " 
                      << cfg.max_velocity_rad
                      << " | Conf: " << cfg.min_vision_confidence
                      << " | Tick Budget: " << cfg.tick_budget_us
                      << " us | Emergency Stop: " << static_cast<int>(cfg.emergency_stop)
                      << " | Revision: " << cfg.profile_revision
                      << "\n";

            // Reply securely to Golang interface
            std::ostringstream ack_stream;
            ack_stream << "ACK revision=" << cfg.profile_revision
                       << ";max_velocity_rad=" << cfg.max_velocity_rad
                       << ";min_vision_confidence=" << cfg.min_vision_confidence
                       << ";tick_budget_us=" << cfg.tick_budget_us
                       << ";emergency_stop=" << static_cast<int>(cfg.emergency_stop);
            const auto ack = ack_stream.str();
            zmq_send(zmq_rep_ctrl_, ack.data(), static_cast<int>(ack.size()), 0);
        } else if (received == -1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void CoreScheduler::stop() {
    const bool was_running = is_running_.exchange(false, std::memory_order_acq_rel);
    if (!was_running) {
        return;
    }

    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

const char* CoreScheduler::state_to_string(GlobalState state) {
    switch (state) {
        case GlobalState::IDLE:
            return "IDLE";
        case GlobalState::PERIMETER_BREACH:
            return "PERIMETER_BREACH";
        case GlobalState::AUTHORIZATION_PENDING:
            return "AUTHORIZATION_PENDING";
        case GlobalState::EMERGENCY_HALT:
            return "EMERGENCY_HALT";
        default:
            return "UNKNOWN";
    }
}

} // namespace edge_orchestrator
