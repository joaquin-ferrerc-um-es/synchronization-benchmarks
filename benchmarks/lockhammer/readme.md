# Lockhammer Local Run Notes

This file documents the local workflow and script changes added on top of the
upstream Lockhammer benchmark in this checkout.

The original upstream documentation is still in `README.rst`. This file focuses
on the practical build and run path for this workspace.

## What Changed

The old `scripts/runall.sh` used this legacy command:

```bash
nohup sudo ./test_lockhammer.py lh_sweeptest_cfg.yaml &
```

That path was problematic because:

- It always required `sudo`, even for benchmark modes that do not need root.
- It ran under `nohup` in the background, hiding failures in `nohup.out`.
- `sudo` could not prompt for a password under `nohup`.
- The old Python runner depends on legacy Python modules such as `sh`.
- Its YAML points at `../build`, while this checkout builds variant directories such as `build.relax_pause`.

The script now delegates to:

```bash
scripts/run-all-readable.sh
```

The new runner:

- Discovers the benchmark binaries that actually exist under `build.*`.
- Runs selected build variants and benchmark binaries.
- Uses a reduced set of all-core contention profiles.
- Disables hugepages by default with `--hugepage-size none`.
- Avoids `sudo` by default.
- Saves raw JSON, logs, a plain text summary, a TSV summary, and a Markdown report.

## Dependencies

Required to build:

```bash
sudo apt install build-essential libjansson-dev
```

Useful to run and summarize:

```bash
sudo apt install jq
```

The old Python runner also needs `python3-sh` and YAML support, but the current
recommended `runall` path does not use that runner.

## Build

From this directory:

```bash
cd /home/jcebrian/02_Benchmarks/lockhammer/synchronization-benchmarks/benchmarks/lockhammer
```

Build the default variant:

```bash
make
```

Build all variants:

```bash
make -j "$(nproc)" allvariants
```

The build creates one executable per benchmark in each `build.*` directory. For
example:

```bash
build.relax_pause/lh_ticket_spinlock
build.relax_pause/lh_incdec_refcount
build.relax_pause/lh_pthread_mutex_lock
```

List built variants:

```bash
find . -maxdepth 1 -type d -name 'build.*' -printf '%f\n' | sort
```

List benchmark binaries in one variant:

```bash
find build.relax_pause -maxdepth 1 -type f -perm -111 -printf '%f\n' | sort
```

## Run One Benchmark Manually

Example:

```bash
./build.relax_pause/lh_ticket_spinlock \
  -t 32 \
  -c 1000ns \
  -p 5000ns \
  -D 0.5 \
  -Y \
  -Z \
  -M none \
  --json ticket_spinlock.json
```

Important flags:

| Flag | Meaning |
|---|---|
| `-t 32` | Run 32 worker threads |
| `-c 1000ns` | Critical section duration |
| `-p 5000ns` | Parallel/post-release duration |
| `-D 0.5` | Run each measurement for 0.5 seconds |
| `-Y` | Continue despite unknown CPU governor configuration |
| `-Z` | Suppress CPU frequency warnings |
| `-M none` | Do not use hugetlb hugepages |
| `--json file.json` | Save machine-readable results |

## Run The Recommended Suite

Default run:

```bash
./scripts/runall.sh
```

By default, this uses `build.relax_pause` when present. It runs every benchmark
binary in that variant across the configured profiles.

Dry run:

```bash
./scripts/runall.sh -n
```

List discovered variants and benchmarks:

```bash
./scripts/runall.sh --list
```

Run all build variants:

```bash
./scripts/runall.sh --all-variants
```

Run one variant:

```bash
./scripts/runall.sh -v relax_pause
```

Run one benchmark:

```bash
./scripts/runall.sh -e lh_ticket_spinlock
```

Run one benchmark across all variants:

```bash
./scripts/runall.sh --all-variants -e lh_ticket_spinlock
```

Use a longer measurement duration:

```bash
./scripts/runall.sh --all-variants -D 2
```

Use a specific output directory:

```bash
./scripts/runall.sh --all-variants -o results/my-run
```

Reuse an output directory by deleting old generated summaries, JSON, and logs
inside it first:

```bash
./scripts/runall.sh --all-variants -o results/my-run --force
```

## Current Run Matrix

The current full run is:

```text
12 build variants * 15 benchmark binaries * 3 profiles = 540 runs
```

The active profiles are:

| Profile | Threads | Critical | Parallel | Intent |
|---|---:|---:|---:|---|
| `realistic_all` | all cores | `1000ns` | `5000ns` | All-core realistic lock hold and post-lock work |
| `high_contention_0` | all cores | `0ns` | `0ns` | Pure synchronization pressure |
| `high_contention_200` | all cores | `200ns` | `0ns` | Short critical section under high contention |

The removed profiles are:

| Removed Profile | Reason |
|---|---|
| `baseline_1t` | Not useful for the current all-core contention focus |
| `realistic_small` | Not useful for the current all-core contention focus |

## Output Files

Each `runall` execution writes to:

```text
results/runall-<timestamp>/
```

Files:

| Path | Purpose |
|---|---|
| `report.md` | Human-readable Markdown report |
| `summary.txt` | Human-readable aligned table |
| `summary.tsv` | Machine-readable table |
| `json/*.json` | Raw Lockhammer JSON files |
| `logs/*.log` | Per-command stdout/stderr |
| `failures.tsv` | Failed commands, if any |

Example:

```bash
less results/runall-20260608-093236/summary.txt
less results/runall-20260608-093236/report.md
```

## Reading The Main Columns

| Column | Meaning | Prefer |
|---|---|---|
| `locks_per_sec` | Completed acquire/release iterations per second | Higher |
| `cpu_ns_lock` | Total CPU time per completed iteration | Lower |
| `wall_ns_lock` | Wall-clock time per completed iteration, derived from global throughput | Lower |
| `overhead_ns` | Estimated synchronization overhead after subtracting configured critical and parallel work | Lower |
| `overhead_pct` | Percent of CPU time spent in synchronization overhead | Lower |
| `fairness_lasom` | Standard deviation of per-thread iterations divided by mean | Lower, but secondary unless fairness matters |

For atomic RMW-focused analysis, `overhead_ns` is usually more relevant than
`fairness_lasom`.

## Important Terminology

Lockhammer uses lock-oriented function names even when a benchmark is not a
traditional mutual-exclusion lock.

Read these names generically:

| Harness Term | Better Generic Meaning |
|---|---|
| `lock_acquire` | Start/perform the synchronization operation |
| `lock_release` | Finish/undo the synchronization operation |
| `locks_per_sec` | Completed synchronization iterations per second |
| `overhead_ns` | Estimated synchronization overhead per iteration |

For example, `lh_incdec_refcount` does not acquire a mutex. Its acquire path is
an atomic increment, and its release path is an atomic decrement.

## Root And Hugepages

Normal runs do not require root.

Root is only needed for specific options such as:

- `-S FIFO` or `-S RR` scheduling policies.
- Hugepage physical address inspection or selection.
- System-level CPU governor or hugepage setup.

The local `runall` script defaults to:

```bash
--hugepage-size none
```

This avoids failures on machines with no preallocated hugetlb pages.

## RISC-V Status

This checkout is not currently a complete RISC-V-compatible suite. It is mainly
implemented for `x86_64` and `aarch64`.

Some generic atomic operations fall back to compiler `__atomic_*` builtins, so a
minimal RV64GC port may be feasible for simple atomic and pthread benchmarks.
However, the harness timer code, cacheline/reservation-granule detection, Linux
queue-lock helpers, JVM monitor model, and some imported lock implementations
need RISC-V-specific support before the full suite can build and run.
