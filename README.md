# Edge AI System Orchestrator ⚙️

The Monolithic Central Nervous System of the `Industrial-Edge-Labs` ecosystem. 

Designed for **Hard Real-Time (HRT) Industrial Automation**, this C++20 engine orchestrates deterministic acyclic execution graphs across high-frequency domains: from asynchronous CUDA-based computer vision perception to strict sub-millisecond mechanical actuation (via `multi-physics-simulation-and-control-system`).

## Architectural Paradigm: The Cyber-Physical Concurrency Model
To achieve standard deviation jitter of less than $10 \mu s$, the orchestrator abandons generic POSIX thread-scheduling in favor of:

1. **Kernel-Bypass Networking**: utilizing memory-mapped packet I/O (`epoll` or DPDK) rather than the standard socket Linux stack.
2. **NUMA-Aware Affinity**: Threads are pinned to specific CPU cores strictly aligned with the PCIe lane connected to the inference GPU (TensorRT nodes) or the NIC (ZeroMQ ingestion channels).
3. **Lock-Free Bounded Queues**: Multi-producer/Multi-consumer concurrent patterns using `std::atomic` fences (Compare-And-Swap) replacing slow `pthread_mutex`.

## Core Loop Sequence ($T_{tick} = 1ms$)

For a $1kHz$ operation loop, the orchestrator commits to a topological sort of system updates:
```mermaid
sequenceDiagram
    participant Vision (NVMM)
    participant Orchestrator (C++20)
    participant Decision Runtime (FSM)
    participant Plant (Multi-Physics)
    participant Telemetry (eBPF)

    loop 1000 Hz Tick
        Note over Orchestrator (C++20): Wait on Spin-Lock Barrier
        Vision (NVMM)->>Orchestrator (C++20): Zero-Copy Ingest (Object State Vector)
        Orchestrator (C++20)->>Decision Runtime (FSM): Evaluate Formal Inference Graph
        Decision Runtime (FSM)-->>Orchestrator (C++20): Produce Actuation Delta
        Orchestrator (C++20)->>Plant (Multi-Physics): Transmit PID/State-Space Torques via UDP/ZMQ
        Orchestrator (C++20)->>Telemetry (eBPF): Async Metrics Dispatch (Ring Buffer)
    end
```

## Compilation (Linux / Windows Native)
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

Ensure NVIDIA CUDA Toolkit $12.x$ and ZeroMQ are present.

*Note: Execution requires `sudo` (CAP_SYS_NICE capability) on Linux to elevate the thread priority to SCHED_FIFO.*
