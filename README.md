# Edge AI System Orchestrator

This repository contains the deterministic control coordinator of the Industrial Edge Labs runtime. It consumes FSM state transitions from [Real-Time Vision Decision System](https://github.com/Industrial-Edge-Labs/real-time-vision-decision-system), accepts `ControlConfig` updates from [Vision Operations Control Plane](https://github.com/Industrial-Edge-Labs/vision-operations-control-plane), and maintains the actuation gate used by the physical and telemetry layers.

## Role In The System

- Consumes the canonical `FsmPayload` emitted by [Real-Time Vision Decision System](https://github.com/Industrial-Edge-Labs/real-time-vision-decision-system).
- Accepts the canonical `ControlConfig` binary block emitted by [Vision Operations Control Plane](https://github.com/Industrial-Edge-Labs/vision-operations-control-plane).
- Tracks deterministic scheduler state, emergency-stop latching, and the active control profile for downstream plant and observability integration.

## Contracts

### Decision Input

`FsmPayload`

1. `timestamp`
2. `current_state`

### Control Input

`ControlConfig`

1. `max_velocity_rad`
2. `min_vision_confidence`
3. `tick_budget_us`
4. `emergency_stop`
5. `reserved[3]`
6. `profile_revision`

## Build Modes

### Portable build

```bash
cmake -S . -B build-default
cmake --build build-default --config Release
```

### ZeroMQ-enabled integration build

```bash
cmake -S . -B build -DEDGE_ORCHESTRATOR_ENABLE_ZEROMQ=ON
cmake --build build --config Release
```

### Local tests

```bash
cmake -S . -B build -DEDGE_ORCHESTRATOR_ENABLE_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Runtime

```bash
./build-default/Release/edge_orchestrator --dry-run --ticks 24 --target-hz 250
```

```bash
./build/Release/edge_orchestrator --target-hz 1000 --decision-endpoint tcp://127.0.0.1:5556 --control-endpoint tcp://127.0.0.1:5557
```

## Notes

- The default build stays portable and does not require ZeroMQ or CUDA.
- The scheduler now validates incoming `FsmPayload` and `ControlConfig` blocks before applying them.
- Emergency-stop state is now latched independently from decision input and control-plane overrides so a control-plane update cannot silently clear a decision-triggered halt.
- Runtime parsing and synthetic-input generation are now split into dedicated modules so the repo has testable internals beyond the live scheduler loop.
- The external documentation for this node lives in [docs-Industrial-Edge-Labs/edge-ai-system-orchestrator](https://github.com/Industrial-Edge-Labs/docs-Industrial-Edge-Labs/tree/main/edge-ai-system-orchestrator).
