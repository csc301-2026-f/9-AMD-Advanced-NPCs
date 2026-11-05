#!/usr/bin/env bash
# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

set -euo pipefail

: "${UE_ENGINE_ROOT:?Set UE_ENGINE_ROOT to the Unreal Engine directory containing Engine/Binaries.}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
example_dir="$(cd "${script_dir}/.." && pwd)"
project="${example_dir}/BackgroundNPCs.uproject"
editor="${UE_ENGINE_ROOT}/Engine/Binaries/Mac/UnrealEditor-Cmd"
report_dir="${example_dir}/Saved/Automation/SharedPolicy/isolated"
trials="${TRIALS:-3}"
warmups="${WARMUPS:-3}"
samples="${SAMPLES:-20}"

if [[ ! -x "${editor}" ]]; then
    echo "UnrealEditor-Cmd not found or not executable: ${editor}" >&2
    exit 1
fi
for value_name in trials warmups samples; do
    value="${!value_name}"
    if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
        echo "${value_name} must be a non-negative integer, got '${value}'." >&2
        exit 1
    fi
done
if [[ "${trials}" -lt 1 || "${samples}" -lt 1 ]]; then
    echo "TRIALS and SAMPLES must be at least 1." >&2
    exit 1
fi

read -r -a batches <<< "${BATCHES:-1 16 64 256}"
read -r -a expert0_percents <<< "${EXPERT0_PERCENTS:-100 50 10 0}"
for batch in "${batches[@]}"; do
    if [[ ! "${batch}" =~ ^[1-9][0-9]*$ ]]; then
        echo "BATCHES entries must be positive integers, got '${batch}'." >&2
        exit 1
    fi
done
for expert0_percent in "${expert0_percents[@]}"; do
    if [[ ! "${expert0_percent}" =~ ^[0-9]+$ || "${expert0_percent}" -gt 100 ]]; then
        echo "EXPERT0_PERCENTS entries must be integers from 0 through 100, got '${expert0_percent}'." >&2
        exit 1
    fi
done
mkdir -p "${report_dir}"

reports=()
for batch in "${batches[@]}"; do
    for expert0_percent in "${expert0_percents[@]}"; do
        for ((trial = 0; trial < trials; ++trial)); do
            report="${report_dir}/cd-b${batch}-e${expert0_percent}-t${trial}.json"
            process_log="${report_dir}/cd-b${batch}-e${expert0_percent}-t${trial}.log"
            echo "Running batch=${batch} expert_0=${expert0_percent}% trial=${trial}"
            rm -f "${report}"
            "${editor}" "${project}" \
                -nullrhi -unattended -nosound -NoSplash \
                "-SharedPolicyBatch=${batch}" \
                "-SharedPolicyExpert0Percent=${expert0_percent}" \
                "-SharedPolicyTrial=${trial}" \
                "-SharedPolicyWarmups=${warmups}" \
                "-SharedPolicySamples=${samples}" \
                '-ExecCmds=Automation RunTests Schola.Inference.SharedPolicy.Routing.CDWorkloadMatrix;Quit' \
                '-TestExit=Automation Test Queue Empty' \
                >"${process_log}" 2>&1
            if [[ ! -f "${report}" ]]; then
                echo "Unreal exited successfully but did not write ${report}." >&2
                exit 1
            fi
            reports+=("${report}")
        done
    done
done

summary="${report_dir}/cd-isolated-summary.json"
python3 "${script_dir}/aggregate_cd_isolated_matrix.py" --output "${summary}" "${reports[@]}"
echo "Isolated C/D summary: ${summary}"
