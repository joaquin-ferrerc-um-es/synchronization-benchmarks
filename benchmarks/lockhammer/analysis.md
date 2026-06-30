# Lockhammer Analysis Notes

This file collects the interpretation tables used while setting up the local
`runall` workflow.

## Run Matrix

The full all-variant run currently consists of combinations, not 540 distinct
benchmark algorithms.

```text
12 build variants * 15 benchmark binaries * 3 profiles = 540 runs
```

Each distinct benchmark binary appears 36 times in a full all-variant run:

```text
12 build variants * 3 profiles = 36 runs per benchmark
```

## Active Load Profiles

| Profile | Threads | Critical | Parallel | What It Stresses |
|---|---:|---:|---:|---|
| `realistic_all` | all cores | `1000ns` | `5000ns` | Realistic all-core contention with work inside and outside the synchronized region |
| `high_contention_0` | all cores | `0ns` | `0ns` | Maximum synchronization pressure, with no simulated work |
| `high_contention_200` | all cores | `200ns` | `0ns` | High contention with a short critical section |

Removed profiles:

| Profile | Threads | Critical | Parallel | Reason Removed |
|---|---:|---:|---:|---|
| `baseline_1t` | 1 | `200ns` | `1000ns` | Not focused on all-core contention |
| `realistic_small` | up to 4 | `200ns` | `1000ns` | Not focused on all-core contention |

## Build Variant Dimensions

The 12 x86_64 variants in this checkout come from combinations of build-time
choices:

| Dimension | Values | Meaning |
|---|---|---|
| Atomic implementation | default, `builtin` | Lockhammer local/inline atomic paths vs compiler `__atomic` builtins |
| OSQ wait style | default, `cond_load` | Normal polling vs `smp_cond_load_relaxed` in `lh_osq_lock` |
| CPU relax behavior | `relax_empty`, `relax_nothing`, `relax_pause` | What the busy-wait relax operation expands to |

Examples:

| Variant | Meaning |
|---|---|
| `relax_pause` | Default atomics, default OSQ polling, x86 `pause` relax |
| `builtin.relax_pause` | Compiler builtin atomics, default OSQ polling, x86 `pause` relax |
| `cond_load.relax_empty` | Default atomics, OSQ conditional-load polling, empty relax asm |
| `builtin.cond_load.relax_nothing` | Compiler builtin atomics, OSQ conditional-load polling, no relax asm |

## Benchmark Summary

All benchmarks use the same harness loop:

```c
lock_acquire(lock, thread);
blackhole(hold_count);
lock_release(lock, thread);
blackhole(post_count);
```

The benchmark-specific part is what `lock_acquire` and `lock_release` do.

| Benchmark | Real Lock? | What It Tests | Primitive / Behavior |
|---|---:|---|---|
| `lh_empty` | No | Harness overhead only | Acquire/release are no-ops |
| `lh_swap_mutex` | Yes | Simple TAS-style spin mutex | Atomic swap to acquire, release store to zero |
| `lh_cas_lockref` | Not exactly | CAS-based reference counter pattern | Increments/decrements high refcount bits with CAS while checking low lock bits |
| `lh_incdec_refcount` | No | Atomic refcount traffic | Atomic fetch-add on acquire, fetch-sub on release |
| `lh_cas_rw_lock` | Reader-side only | Reader-side RW-lock counter pressure | CAS decrements reader count on acquire, atomic increment on release |
| `lh_ticket_spinlock` | Yes | Ticket spinlock handoff | FIFO ticket acquisition; wait for serving ticket |
| `lh_queued_spinlock` | Yes | Linux queued spinlock | Fast path plus pending bit and MCS queue slow path |
| `lh_osq_lock` | Yes-ish | Linux optimistic spin queue | MCS-like optimistic spinning queue with optional unqueue/backoff path |
| `lh_clh_spinlock` | Yes | CLH queue spinlock | Threads enqueue and spin on predecessor node |
| `lh_event_mutex` | Yes | MySQL 5.7 event mutex model | Test-and-set mutex with spin, randomized delay, event-generation wake signal |
| `lh_cas_event_mutex` | Yes | MySQL event mutex atomic variant | Similar event mutex with architecture-specific atomic/CAS-style path |
| `lh_jvm_objectmonitor` | Yes | OpenJDK ObjectMonitor model | Owner CAS, spinning, waiter queues, pthread condition parking |
| `lh_tbb_spin_rw_mutex` | RW lock | Intel TBB spin RW mutex | Writer-preference reader/writer spin lock; default mostly readers |
| `lh_pthread_mutex_lock` | Yes | POSIX blocking mutex | `pthread_mutex_lock` / `pthread_mutex_unlock` |
| `lh_pthread_mutex_trylock` | Yes | POSIX trylock polling mutex | Loops on `pthread_mutex_trylock`, optional delay after `EBUSY` |

## Lockhammer Terminology

Lockhammer uses lock-oriented names for the harness even when the benchmark is
not a mutual-exclusion lock.

| Harness Term | Generic Interpretation |
|---|---|
| `lock_acquire` | Start or perform the synchronization operation |
| `lock_release` | Finish or undo the synchronization operation |
| `locks_per_sec` | Completed synchronization iterations per second |
| `cpu_ns_lock` | CPU nanoseconds per completed synchronization iteration |
| `overhead_ns` | Estimated synchronization overhead per iteration |

For `lh_incdec_refcount`, for example:

| Harness Term | Actual Operation |
|---|---|
| `lock_acquire` | Atomic increment |
| `lock_release` | Atomic decrement |
| `locks_per_sec` | Atomic increment/decrement pairs per second |

## Metrics

| Column | Meaning | Prefer | Notes |
|---|---|---:|---|
| `locks_per_sec` | Completed acquire/release iterations per wall-clock second | Higher | Throughput |
| `cpu_ns_lock` | Total CPU time per completed iteration, including configured critical and parallel work | Lower | Total CPU cost |
| `wall_ns_lock` | Wall-clock time per completed iteration across all threads | Lower | Derived from global elapsed time and total iterations |
| `overhead_ns` | Estimated synchronization overhead after subtracting critical and parallel work | Lower | Best existing proxy for acquisition/release/wait cost |
| `overhead_pct` | Percent of CPU time attributed to synchronization overhead | Lower | Can be high in zero-work profiles by design |
| `fairness_lasom` | Standard deviation of per-thread iteration counts divided by mean | Lower | Fairness/imbalance indicator |

## Interpreting Fairness

`fairness_lasom` is Lockhammer's `lock_acquires_stddev_over_mean`.

```text
fairness_lasom = stddev(lock_acquires_per_thread) / mean(lock_acquires_per_thread)
```

| `fairness_lasom` | Interpretation |
|---:|---|
| `0` | Perfectly balanced iteration counts |
| `0.01` | Very balanced |
| `0.10` | Noticeable imbalance |
| `0.50+` | Some threads dominate while others make much less progress |

This is useful when starvation or fairness matters. It is not the primary metric
for atomic RMW acquisition/wait cost.

## Acquisition / Wait Cost

For acquisition or waiting before progress, focus on:

| Metric | Usefulness |
|---|---|
| `overhead_ns` | Best existing proxy for acquire + wait/spin + release overhead |
| `cpu_ns_lock` | Useful total CPU cost, but includes critical and parallel work |
| `locks_per_sec` | Throughput signal, but not a direct wait-time measure |
| `fairness_lasom` | Imbalance signal, not direct acquisition latency |

Lockhammer currently estimates:

```text
overhead_ns ~= total CPU time - critical_ns - parallel_ns
```

For high-contention profiles:

| Profile | Critical | Parallel | What `overhead_ns` Mostly Captures |
|---|---:|---:|---|
| `high_contention_0` | `0ns` | `0ns` | Synchronization operation cost, retrying, spinning, cacheline movement, release overhead |
| `high_contention_200` | `200ns` | `0ns` | Synchronization overhead around a short critical section |
| `realistic_all` | `1000ns` | `5000ns` | Synchronization overhead under realistic work spacing |

Important limitation:

```text
overhead_ns = acquire wait + atomic attempts + successful acquire + release overhead
```

It is not specifically:

```text
time before first successful atomic RMW
```

To measure that exact quantity, each benchmark implementation would need
additional instrumentation inside its retry/acquire loop.

## Metric Reading Patterns

| Pattern | Interpretation |
|---|---|
| Low `overhead_ns`, high `locks_per_sec` | Good synchronization efficiency |
| High `overhead_ns`, low `locks_per_sec` | Poor progress under contention |
| High `overhead_ns`, high `locks_per_sec` | Expensive per operation but still productive |
| Low `locks_per_sec`, low `fairness_lasom` | Slow but evenly shared progress |
| High `locks_per_sec`, high `fairness_lasom` | Fast aggregate throughput but possible domination by a subset of threads |
| High `overhead_pct` in `high_contention_0` | Expected; almost all CPU time is synchronization because no work was configured |

## Atomic RMW Focus

Atomic RMW operations do not necessarily imply a lock. Several Lockhammer tests
use the lock harness to measure non-lock synchronization patterns.

| Benchmark | Atomic RMW Focus |
|---|---|
| `lh_incdec_refcount` | Raw atomic increment/decrement traffic |
| `lh_cas_lockref` | CAS-based refcount-style update |
| `lh_cas_rw_lock` | CAS/fetch-add reader counter pressure |
| `lh_swap_mutex` | Atomic swap under lock contention |
| `lh_ticket_spinlock` | Atomic ticket acquisition plus waiting for turn |
| `lh_queued_spinlock` | Atomic fast path plus queued slow path |

For pure atomic RMW analysis, prioritize:

| Metric | Why |
|---|---|
| `overhead_ns` | Closest existing proxy for RMW/retry/synchronization cost |
| `locks_per_sec` | Shows aggregate completed operation rate |
| `cpu_ns_lock` | Shows total CPU budget consumed per completed operation |

Do not overinterpret `fairness_lasom` unless you are specifically studying
per-thread progress imbalance.

## RISC-V Compatibility Assessment

This checkout is not currently a full RISC-V-compatible suite.

| Area | Current State | RISC-V Impact |
|---|---|---|
| Build variants | Mainly `x86_64` and `aarch64` | No RISC-V-specific variant set |
| Timer support | x86 TSC and AArch64 `cntvct_el0` | Needs RISC-V timer support |
| Cache/reservation granule | x86 fixed value and AArch64 CTR read | Needs RISC-V fallback or probe |
| CPU relax | x86 `pause`, AArch64 options | Needs RISC-V `pause`/`nop`/empty choice |
| Linux imported atomics | x86_64/AArch64 helpers | Queue locks and OSQ need RISC-V helpers |
| JVM monitor | Explicit unsupported-ISA path outside x86_64/AArch64 | Needs RISC-V support |
| CLH/SMS support | Arm/x86 build config | Needs RISC-V build config |

Likely porting effort by benchmark:

| Benchmark Group | RISC-V Effort |
|---|---|
| `lh_empty` | Easy after harness timer/cacheline support |
| `lh_incdec_refcount`, `lh_cas_lockref`, `lh_cas_rw_lock`, `lh_swap_mutex` | Likely straightforward with `USE_BUILTIN=1` |
| `lh_pthread_mutex_lock`, `lh_pthread_mutex_trylock` | Likely straightforward after harness support |
| `lh_event_mutex`, `lh_cas_event_mutex`, `lh_tbb_spin_rw_mutex` | Moderate validation required |
| `lh_ticket_spinlock` | Needs RISC-V implementation |
| `lh_queued_spinlock`, `lh_osq_lock` | Larger Linux-style atomic/barrier port |
| `lh_clh_spinlock`, `lh_jvm_objectmonitor` | Needs architecture support additions |

Compiler `__atomic_*` builtins may generate RISC-V AMO or LR/SC instructions
when targeting an ISA with atomic support, but the full suite has additional
architecture-specific code beyond the generic atomic helpers.
