#include "CoreScheduler.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>

#ifndef EDGE_ORCHESTRATOR_USE_ZMQ
#define EDGE_ORCHESTRATOR_USE_ZMQ 1
#endif

#if EDGE_ORCHESTRATOR_USE_ZMQ
#include <zmq.h>
#endif

#ifdef __linux__
#include <pthread.h>
#endif

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace edge_orchestrator {

uint64_t CoreScheduler::now_ns() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count());
}

CoreScheduler::CoreScheduler(SchedulerConfig config)
    : config_(std::move(config)) {
#if EDGE_ORCHESTRATOR_USE_ZMQ
    if (!config_.enable_decision_input && !config_.enable_control_plane) {
        return;
    }

    zmq_context_ = zmq_ctx_new();
    if (!zmq_context_) {
        std::cerr << "[Orchestrator] Failed to initialize ZeroMQ context. Continuing without network ingress.\n";
        return;
    }

    int linger_ms = 0;

    if (config_.enable_decision_input) {
        zmq_sub_fsm_ = zmq_socket(zmq_context_, ZMQ_SUB);
        if (!zmq_sub_fsm_) {
            std::cerr << "[Orchestrator] Failed to create decision subscriber socket.\n";
        } else {
            int subscriber_timeout_ms = 0;
            zmq_setsockopt(zmq_sub_fsm_, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
            zmq_setsockopt(zmq_sub_fsm_, ZMQ_SUBSCRIBE, "", 0);
            zmq_setsockopt(zmq_sub_fsm_, ZMQ_RCVTIMEO, &subscriber_timeout_ms, sizeof(subscriber_timeout_ms));

            if (zmq_connect(zmq_sub_fsm_, config_.decision_endpoint.c_str()) != 0) {
                std::cerr << "[Orchestrator] Failed to connect decision subscriber to "
                          << config_.decision_endpoint << ".\n";
                zmq_close(zmq_sub_fsm_);
                zmq_sub_fsm_ = nullptr;
            }
        }
    }

    if (config_.enable_control_plane) {
        zmq_rep_ctrl_ = zmq_socket(zmq_context_, ZMQ_REP);
        if (!zmq_rep_ctrl_) {
            std::cerr << "[Orchestrator] Failed to create control-plane REP socket.\n";
        } else {
            int control_timeout_ms = 100;
            zmq_setsockopt(zmq_rep_ctrl_, ZMQ_RCVTIMEO, &control_timeout_ms, sizeof(control_timeout_ms));
            zmq_setsockopt(zmq_rep_ctrl_, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));

            if (zmq_bind(zmq_rep_ctrl_, config_.control_endpoint.c_str()) != 0) {
                std::cerr << "[Orchestrator] Failed to bind control-plane REP socket on "
                          << config_.control_endpoint << ".\n";
                zmq_close(zmq_rep_ctrl_);
                zmq_rep_ctrl_ = nullptr;
            }
        }
    }
#endif
}

CoreScheduler::~CoreScheduler() {
    stop();
#if EDGE_ORCHESTRATOR_USE_ZMQ
    if (zmq_sub_fsm_) {
        zmq_close(zmq_sub_fsm_);
    }
    if (zmq_rep_ctrl_) {
        zmq_close(zmq_rep_ctrl_);
    }
    if (zmq_context_) {
        zmq_ctx_destroy(zmq_context_);
    }
#endif
}

void CoreScheduler::pin_thread_to_core(std::thread& thread_handle, int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(thread_handle.native_handle(), sizeof(cpu_set_t), &cpuset);
#else
    (void)thread_handle;
    (void)core_id;
#endif
}

void CoreScheduler::start_rt_loop(uint32_t target_hz, uint64_t max_ticks) {
    is_running_.store(true, std::memory_order_release);
    current_tick_.store(0, std::memory_order_release);

    const auto tick_duration_ns = std::chrono::nanoseconds(1'000'000'000ULL / target_hz);
    std::cout << "[Orchestrator] Scheduler locked at " << target_hz
              << " Hz (" << tick_duration_ns.count() << " ns per tick).\n";

    if (config_.dry_run) {
        std::cout << "[Orchestrator] Dry-run mode enabled. Using synthetic FSM and control updates.\n";
    } else {
        if (zmq_sub_fsm_) {
            std::cout << "[Orchestrator] Listening for FSM updates on " << config_.decision_endpoint << ".\n";
        }
        if (zmq_rep_ctrl_) {
            std::cout << "[Orchestrator] Listening for control-plane requests on " << config_.control_endpoint << ".\n";
        }
    }

    if (max_ticks > 0) {
        std::cout << "[Orchestrator] Deterministic execution budget set to " << max_ticks << " ticks.\n";
    }

    if (zmq_rep_ctrl_) {
        workers_.emplace_back(&CoreScheduler::listen_control_plane, this);
        pin_thread_to_core(workers_.back(), 1);
    }

    auto next_tick_time = std::chrono::steady_clock::now() + tick_duration_ns;
    while (is_running_.load(std::memory_order_acquire)) {
        const auto tick_started_at = std::chrono::steady_clock::now();
        execution_tick();

        auto now = std::chrono::steady_clock::now();
        if (now < next_tick_time) {
            while (std::chrono::steady_clock::now() < next_tick_time) {
#if defined(__x86_64__) || defined(_M_X64)
                _mm_pause();
#endif
            }
        }

        next_tick_time += tick_duration_ns;
        const auto finished_tick = current_tick_.fetch_add(1, std::memory_order_relaxed) + 1;
        const auto tick_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - tick_started_at
        ).count();
        const auto tick_budget = tick_budget_us_.load(std::memory_order_relaxed);

        if (tick_elapsed_us > static_cast<long long>(tick_budget) && finished_tick % 200 == 0) {
            std::cout << "[Orchestrator] Tick budget overrun detected: " << tick_elapsed_us
                      << " us (budget " << tick_budget << " us).\n";
        }

        if (max_ticks > 0 && finished_tick >= max_ticks) {
            std::cout << "[Orchestrator] Max tick budget reached. Initiating controlled shutdown.\n";
            stop();
        }
    }
}

void CoreScheduler::execution_tick() {
    const auto tick = current_tick_.load(std::memory_order_relaxed) + 1;

    if (config_.dry_run) {
        inject_synthetic_inputs(tick);
    } else if (zmq_sub_fsm_) {
        poll_decision_bus();
    }

    const auto active_state = static_cast<GlobalState>(last_fsm_state_.load(std::memory_order_relaxed));
    const bool emergency_override =
        decision_emergency_latched_.load(std::memory_order_relaxed)
        || control_plane_emergency_.load(std::memory_order_relaxed);

    if (emergency_override) {
        if (config_.dry_run || tick % 250 == 0) {
            std::cout << "[Orchestrator] Emergency gate active. Actuation remains disabled.\n";
        }
        return;
    }

    if (config_.dry_run || tick % 250 == 0) {
        std::cout << "[Orchestrator] Tick=" << tick
                  << " | FSM=" << state_to_string(active_state)
                  << " | max_velocity_rad=" << max_velocity_bound_.load(std::memory_order_relaxed)
                  << " | min_conf=" << min_vision_conf_.load(std::memory_order_relaxed)
                  << " | profile_rev=" << profile_revision_.load(std::memory_order_relaxed)
                  << "\n";
    }
}

void CoreScheduler::poll_decision_bus() {
#if EDGE_ORCHESTRATOR_USE_ZMQ
    while (true) {
        FsmPayload payload{};
        const int received = zmq_recv(zmq_sub_fsm_, &payload, sizeof(payload), ZMQ_DONTWAIT);
        if (received == -1) {
            break;
        }

        if (received == sizeof(payload) && is_valid_fsm_payload(payload)) {
            apply_fsm_payload(payload, "decision-bus");
        } else {
            std::cerr << "[Orchestrator] Dropped malformed FsmPayload of " << received << " bytes.\n";
        }
    }
#endif
}

void CoreScheduler::listen_control_plane() {
#if EDGE_ORCHESTRATOR_USE_ZMQ
    while (is_running_.load(std::memory_order_acquire)) {
        ControlConfig cfg{};
        const int received = zmq_recv(zmq_rep_ctrl_, &cfg, sizeof(cfg), 0);

        if (received == -1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::string ack;
        if (received != sizeof(cfg)) {
            ack = "ERR invalid_size";
            std::cerr << "[Orchestrator] Rejected control packet with invalid size " << received << ".\n";
        } else if (!is_valid_control_config(cfg)) {
            ack = "ERR invalid_payload";
            std::cerr << "[Orchestrator] Rejected control packet with invalid values.\n";
        } else {
            apply_control_config(cfg, "control-plane");
            ack = make_control_ack(cfg, "ACK");
        }

        zmq_send(zmq_rep_ctrl_, ack.data(), static_cast<int>(ack.size()), 0);
    }
#endif
}

void CoreScheduler::apply_fsm_payload(const FsmPayload& payload, const char* source) {
    const auto previous_state = static_cast<GlobalState>(
        last_fsm_state_.exchange(static_cast<uint8_t>(payload.current_state), std::memory_order_acq_rel)
    );

    if (previous_state != payload.current_state) {
        std::cout << "[Orchestrator] FSM transition from " << state_to_string(previous_state)
                  << " to " << state_to_string(payload.current_state)
                  << " received from " << source << ".\n";
    }

    if (payload.current_state == GlobalState::EMERGENCY_HALT) {
        const bool was_latched = decision_emergency_latched_.exchange(true, std::memory_order_acq_rel);
        if (!was_latched) {
            std::cout << "[Orchestrator] Decision-layer emergency halt latched.\n";
        }
    }
}

void CoreScheduler::apply_control_config(const ControlConfig& cfg, const char* source) {
    max_velocity_bound_.store(cfg.max_velocity_rad, std::memory_order_release);
    min_vision_conf_.store(cfg.min_vision_confidence, std::memory_order_release);
    tick_budget_us_.store(cfg.tick_budget_us, std::memory_order_release);
    control_plane_emergency_.store(cfg.emergency_stop != 0, std::memory_order_release);
    profile_revision_.store(cfg.profile_revision, std::memory_order_release);

    std::cout << "[Orchestrator] Applied " << source
              << " profile revision=" << cfg.profile_revision
              << " max_velocity_rad=" << cfg.max_velocity_rad
              << " min_vision_confidence=" << cfg.min_vision_confidence
              << " tick_budget_us=" << cfg.tick_budget_us
              << " emergency_stop=" << static_cast<int>(cfg.emergency_stop)
              << "\n";
}

void CoreScheduler::inject_synthetic_inputs(uint64_t tick) {
    if (tick == 1) {
        ControlConfig cfg{};
        cfg.max_velocity_rad = 120.0;
        cfg.min_vision_confidence = 0.88;
        cfg.tick_budget_us = 1000;
        cfg.emergency_stop = 0;
        cfg.profile_revision = 1;
        apply_control_config(cfg, "dry-run");
        return;
    }

    if (tick == 4) {
        apply_fsm_payload(FsmPayload{now_ns(), GlobalState::PERIMETER_BREACH}, "dry-run");
        return;
    }

    if (tick == 8) {
        apply_fsm_payload(FsmPayload{now_ns(), GlobalState::AUTHORIZATION_PENDING}, "dry-run");
        return;
    }

    if (tick == 16) {
        apply_fsm_payload(FsmPayload{now_ns(), GlobalState::EMERGENCY_HALT}, "dry-run");
    }
}

void CoreScheduler::stop() {
    const bool was_running = is_running_.exchange(false, std::memory_order_acq_rel);
    if (!was_running) {
        return;
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

} // namespace edge_orchestrator
