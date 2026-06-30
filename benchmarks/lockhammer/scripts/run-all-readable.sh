#!/bin/bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: BSD-3-Clause

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BENCH_DIR=$(cd "$SCRIPT_DIR/.." && pwd)

DURATION_SECONDS=0.5
OUT_DIR=
DRY_RUN=0
ALL_VARIANTS=0
HUGEPAGE_SIZE=none
FORCE=0

declare -a REQUESTED_VARIANTS=()
declare -a REQUESTED_TESTS=()

usage() {
cat <<'USAGE'
run-all-readable.sh [options]

Runs all available Lockhammer benchmark binaries for selected build variants
using a small set of recommended profiles, then writes JSON, logs, and an
easy-to-read report.

Options:
  -v, --variant variant     Run build.<variant>. Repeatable. Default: relax_pause
                            when present, otherwise the first build.* directory.
      --all-variants        Run every build.* directory found.
  -e, --test test           Run only this benchmark. Repeatable. Accepts
                            lh_ticket_spinlock or ticket_spinlock.
  -D, --duration seconds    Measurement duration per run. Default: 0.5.
  -o, --out directory       Output directory. Default: results/runall-<timestamp>.
  -M, --hugepage-size size  Hugepage size passed to Lockhammer. Default: none.
  -n, --dry-run             Print the commands without running benchmarks.
      --force               Allow using an existing output directory.
      --list                List discovered variants and benchmarks, then exit.
  -h, --help                Show this help.

Profiles:
  realistic_all        all threads, c=1000ns, p=5000ns
  high_contention_0    all threads, c=0ns,    p=0ns
  high_contention_200  all threads, c=200ns,  p=0ns
USAGE
}

die() {
	echo "ERROR: $*" >&2
	exit 1
}

require_cmd() {
	command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

quote_cmd() {
	local quoted=
	printf -v quoted "%q " "$@"
	echo "${quoted% }"
}

normalize_test_name() {
	local test=$1
	if [[ $test == lh_* ]]; then
		echo "$test"
	else
		echo "lh_$test"
	fi
}

normalize_variant_name() {
	local variant=$1
	variant=${variant#build.}
	echo "$variant"
}

discover_variants() {
	find "$BENCH_DIR" -maxdepth 1 -type d -name 'build.*' -printf '%f\n' \
		| sed 's/^build\.//' \
		| sort
}

discover_tests_for_variant() {
	local variant=$1
	find "$BENCH_DIR/build.$variant" -maxdepth 1 -type f -perm -111 -name 'lh_*' -printf '%f\n' \
		| sort
}

resolve_threads() {
	local selector=$1
	local nproc=$2
	case "$selector" in
		one) echo 1 ;;
		small)
			if (( nproc < 4 )); then echo "$nproc"; else echo 4; fi
			;;
		half)
			local half=$((nproc / 2))
			if (( half < 1 )); then echo 1; else echo "$half"; fi
			;;
		all) echo "$nproc" ;;
		*) die "unknown thread selector: $selector" ;;
	esac
}

list_discovered() {
	echo "variants:"
	local variant
	while read -r variant; do
		[[ -n $variant ]] && echo "  $variant"
	done < <(discover_variants)

	echo
	echo "benchmarks by variant:"
	while read -r variant; do
		[[ -z $variant ]] && continue
		echo "  $variant:"
		discover_tests_for_variant "$variant" | sed 's/^/    /'
	done < <(discover_variants)
}

write_summary() {
	local json_dir=$1
	local out_dir=$2
	local summary_tsv=$out_dir/summary.tsv
	local summary_txt=$out_dir/summary.txt
	local report_md=$out_dir/report.md

	shopt -s nullglob
	local json_files=("$json_dir"/*.json)
	shopt -u nullglob

	if (( ${#json_files[@]} == 0 )); then
		echo "No JSON result files were produced." > "$report_md"
		return
	fi

	jq -s -r '
		def round0: if type == "number" then round else . end;
		def pct2: if type == "number" then (.*100 | round / 100) else . end;
		["profile","variant","benchmark","threads","critical","parallel","cpu_ns_lock","wall_ns_lock","overhead_ns","overhead_pct","locks_per_sec","fairness_lasom"],
		(.[] | .results[] |
			[
				(.tag // "-"),
				.variant_name,
				.test_name,
				.num_threads,
				((.nominal_critical | tostring) + .nominal_critical_unit),
				((.nominal_parallel | tostring) + .nominal_parallel_unit),
				(.cputime_ns_per_lock_acquire | round0),
				(.wall_elapsed_ns_per_lock_acquire | round0),
				(.avg_lock_overhead_cputime_ns | round0),
				(.lock_overhead_cputime_percent | round0),
				(.total_lock_acquires_per_second | round0),
				(.lock_acquires_stddev_over_mean | pct2)
			]
		) | @tsv
	' "${json_files[@]}" > "$summary_tsv"

	column -t -s $'\t' "$summary_tsv" > "$summary_txt"

	{
		echo "# Lockhammer Run-All Report"
		echo
		echo "- Generated: $(date -Is)"
		echo "- Host: $(hostname -f 2>/dev/null || hostname)"
		echo "- JSON files: ${#json_files[@]}"
		echo "- Full table: summary.txt"
		echo "- Machine-readable table: summary.tsv"
		echo
		echo "## Profiles"
		echo
		echo "| Profile | Intent | Threads | Critical | Parallel |"
		echo "|---|---|---:|---:|---:|"
		echo "| realistic_all | all-core realistic lock hold and post-lock work | nproc | 1000ns | 5000ns |"
		echo "| high_contention_0 | pure lock handoff pressure | nproc | 0ns | 0ns |"
		echo "| high_contention_200 | high contention with a short critical section | nproc | 200ns | 0ns |"
		echo
		echo "## Fastest Throughput Per Profile"
		echo
		jq -s -r '
			def round0: if type == "number" then round else . end;
			[.[] | .results[]]
			| sort_by(.tag)
			| group_by(.tag)[]
			| "### " + (.[0].tag // "-") + "\n\n"
			  + "| Rank | Variant | Benchmark | Threads | Locks/sec | CPU ns/lock | Overhead % | Fairness LASOM |\n"
			  + "|---:|---|---|---:|---:|---:|---:|---:|\n"
			  + (
				sort_by(.total_lock_acquires_per_second) | reverse | .[0:10]
				| to_entries
				| map("| " + ((.key + 1) | tostring)
					+ " | " + .value.variant_name
					+ " | " + .value.test_name
					+ " | " + (.value.num_threads | tostring)
					+ " | " + (.value.total_lock_acquires_per_second | round0 | tostring)
					+ " | " + (.value.cputime_ns_per_lock_acquire | round0 | tostring)
					+ " | " + (.value.lock_overhead_cputime_percent | round0 | tostring)
					+ " | " + ((.value.lock_acquires_stddev_over_mean * 100 | round / 100) | tostring)
					+ " |")
				| join("\n")
			  )
			  + "\n"
		' "${json_files[@]}"
		echo
		echo "## Full Results"
		echo
		echo '```text'
		cat "$summary_txt"
		echo '```'
	} > "$report_md"
}

while (($#)); do
	case "$1" in
		-v|--variant)
			shift
			(($#)) || die "--variant requires an argument"
			REQUESTED_VARIANTS+=("$(normalize_variant_name "$1")")
			;;
		-e|--test)
			shift
			(($#)) || die "--test requires an argument"
			REQUESTED_TESTS+=("$(normalize_test_name "$1")")
			;;
		-D|--duration)
			shift
			(($#)) || die "--duration requires an argument"
			DURATION_SECONDS=$1
			;;
		-o|--out)
			shift
			(($#)) || die "--out requires an argument"
			OUT_DIR=$1
			;;
		-M|--hugepage-size)
			shift
			(($#)) || die "--hugepage-size requires an argument"
			HUGEPAGE_SIZE=$1
			;;
		-n|--dry-run)
			DRY_RUN=1
			;;
		--all-variants)
			ALL_VARIANTS=1
			;;
		--force)
			FORCE=1
			;;
		--list)
			list_discovered
			exit 0
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			die "unknown option: $1"
			;;
	esac
	shift
done

cd "$BENCH_DIR" || exit 1

require_cmd find
require_cmd jq
require_cmd column
require_cmd nproc

mapfile -t DISCOVERED_VARIANTS < <(discover_variants)
(( ${#DISCOVERED_VARIANTS[@]} > 0 )) || die "no build.* directories found in $BENCH_DIR"

declare -a VARIANTS
if (( ALL_VARIANTS )); then
	VARIANTS=("${DISCOVERED_VARIANTS[@]}")
elif (( ${#REQUESTED_VARIANTS[@]} > 0 )); then
	VARIANTS=("${REQUESTED_VARIANTS[@]}")
elif [[ -d build.relax_pause ]]; then
	VARIANTS=(relax_pause)
else
	VARIANTS=("${DISCOVERED_VARIANTS[0]}")
fi

for variant in "${VARIANTS[@]}"; do
	[[ -d build.$variant ]] || die "build.$variant does not exist"
done

if [[ -z $OUT_DIR ]]; then
	OUT_DIR="results/runall-$(date +%Y%m%d-%H%M%S)"
fi

[[ $OUT_DIR != "/" ]] || die "refusing to use / as output directory"

if [[ -e $OUT_DIR && $FORCE -eq 0 ]]; then
	die "$OUT_DIR already exists; choose another --out directory or pass --force"
fi

JSON_DIR=$OUT_DIR/json
LOG_DIR=$OUT_DIR/logs
if [[ -e $OUT_DIR && $FORCE -eq 1 ]]; then
	rm -rf "$JSON_DIR" "$LOG_DIR" \
		"$OUT_DIR/summary.tsv" "$OUT_DIR/summary.txt" \
		"$OUT_DIR/report.md" "$OUT_DIR/failures.tsv"
fi
mkdir -p "$JSON_DIR" "$LOG_DIR"

NPROC=$(nproc)

PROFILES=(
	"realistic_all|all|1000ns|5000ns|all-core realistic contention"
	"high_contention_0|all|0ns|0ns|pure lock handoff pressure"
	"high_contention_200|all|200ns|0ns|short critical section under high contention"
)

TOTAL_RUNS=0
for variant in "${VARIANTS[@]}"; do
	if (( ${#REQUESTED_TESTS[@]} > 0 )); then
		TOTAL_RUNS=$((TOTAL_RUNS + ${#REQUESTED_TESTS[@]} * ${#PROFILES[@]}))
	else
		mapfile -t variant_tests < <(discover_tests_for_variant "$variant")
		TOTAL_RUNS=$((TOTAL_RUNS + ${#variant_tests[@]} * ${#PROFILES[@]}))
	fi
done

echo "Lockhammer run-all"
echo "  variants: ${VARIANTS[*]}"
echo "  profiles: ${#PROFILES[@]}"
echo "  logical CPUs: $NPROC"
echo "  duration per run: ${DURATION_SECONDS}s"
echo "  hugepage size: $HUGEPAGE_SIZE"
echo "  output: $OUT_DIR"
echo "  planned runs: $TOTAL_RUNS"
echo

FAILURES=$OUT_DIR/failures.tsv
echo -e "variant\tbenchmark\tprofile\texit_status\tlog" > "$FAILURES"

run_count=0
fail_count=0

for variant in "${VARIANTS[@]}"; do
	if (( ${#REQUESTED_TESTS[@]} > 0 )); then
		tests=("${REQUESTED_TESTS[@]}")
	else
		mapfile -t tests < <(discover_tests_for_variant "$variant")
	fi

	for test in "${tests[@]}"; do
		exe="build.$variant/$test"
		if [[ ! -x $exe ]]; then
			echo "WARNING: $exe is not executable; skipping" >&2
			continue
		fi

		for profile in "${PROFILES[@]}"; do
			IFS='|' read -r profile_id thread_selector crit par profile_desc <<< "$profile"
			threads=$(resolve_threads "$thread_selector" "$NPROC")
			run_count=$((run_count + 1))
			json="$JSON_DIR/${variant}.${test}.${profile_id}.json"
			log="$LOG_DIR/${variant}.${test}.${profile_id}.log"

			cmd=(
				"$exe"
				-Y
				-Z
				--hugepage-size "$HUGEPAGE_SIZE"
				-T "$profile_id"
				-t "$threads"
				-c "$crit"
				-p "$par"
				-D "$DURATION_SECONDS"
				--json "$json"
			)

			printf '[%d/%d] %s %-27s %-21s t=%s c=%s p=%s\n' \
				"$run_count" "$TOTAL_RUNS" "$variant" "$test" "$profile_id" "$threads" "$crit" "$par"

			if (( DRY_RUN )); then
				quote_cmd "${cmd[@]}"
				continue
			fi

			"${cmd[@]}" > "$log" 2>&1
			status=$?
			if (( status != 0 )); then
				fail_count=$((fail_count + 1))
				echo -e "${variant}\t${test}\t${profile_id}\t${status}\t${log}" >> "$FAILURES"
				echo "  FAILED: see $log" >&2
			fi
		done
	done
done

if (( DRY_RUN )); then
	echo
	echo "Dry run complete. No benchmarks were executed."
	exit 0
fi

write_summary "$JSON_DIR" "$OUT_DIR"

echo
echo "Completed $run_count runs with $fail_count failures."
echo "Report: $OUT_DIR/report.md"
echo "Table:  $OUT_DIR/summary.txt"
if (( fail_count > 0 )); then
	echo "Failures: $FAILURES"
fi
