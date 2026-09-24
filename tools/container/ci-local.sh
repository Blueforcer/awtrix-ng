#!/usr/bin/env bash
# Run from the repository root, inside the local CI image.
set -euo pipefail

usage() {
    printf 'Usage: %s {native|webui|docs|mkdocs|build ENV...|all}...\n' "$0" >&2
}
if [[ ! -f platformio.ini || ! -f requirements-docs.txt ]]; then
    printf 'Run this script from the repository root.\n' >&2
    exit 2
fi
if (( $# == 0 )); then usage; exit 2; fi

# Validate the entire request before doing any work. A build consumes environment
# names until the next subcommand, so groups can be freely combined.
steps=()
while (( $# )); do
    case "$1" in
        native|webui|docs|mkdocs) steps+=("$1"); shift ;;
        all)
            steps+=(native webui docs mkdocs build:awtrix build:awtrix_s3_octal build:native_sim build:galactic_unicorn build:galactic_unicorn_2w)
            shift ;;
        build)
            shift
            count=0
            while (( $# )); do
                case "$1" in native|webui|docs|mkdocs|build|all) break ;; esac
                if [[ ! "$1" =~ ^[A-Za-z0-9_][A-Za-z0-9_.-]*$ ]]; then
                    printf 'Invalid environment: %s\n' "$1" >&2; exit 2
                fi
                steps+=("build:$1"); count=$((count + 1)); shift
            done
            if (( count == 0 )); then usage; exit 2; fi ;;
        *) printf 'Unknown subcommand: %s\n' "$1" >&2; usage; exit 2 ;;
    esac
done

failed=0
run_step() {
    local label=$1 start=$SECONDS rc=0
    shift
    printf '\n=== %s ===\n' "$label"
    # Explicitly capture failures so later steps still run under errexit.
    if "$@"; then
        printf 'PASS %s (%ds)\n' "$label" "$((SECONDS - start))"
    else
        rc=$?
        printf 'FAIL %s (%ds; exit %d)\n' "$label" "$((SECONDS - start))" "$rc"
        failed=1
    fi
}
webui_tests() (
    cd webui/test && npm ci && npm test
)
# As in .github/workflows/docs.yml: mkdocs reports a link to a missing anchor only at INFO level,
# so --strict walks past it; fail on it the way the Docs workflow does.
mkdocs_build() {
    pip install -q -r requirements-docs.txt || return
    local log rc=0
    log=$(mktemp)
    mkdocs build --strict 2>&1 | tee "$log" || rc=$?
    if grep -q "does not contain an anchor" "$log"; then
        grep "does not contain an anchor" "$log"
        rc=1
    fi
    rm -f "$log"
    return "$rc"
}

for step in "${steps[@]}"; do
    case "$step" in
        native) run_step native python scripts/test_native.py ;;
        webui)
            run_step webui webui_tests
            run_step flowconv node --test "tools/flowconv/*.test.mjs" ;;
        docs)
            for check in check_docs_sync check_flow_converter gen_agent_skill \
                         check_berry_api check_prelude_solidified check_font_sync check_partitions check_builtin_registration; do
                if [[ "$check" == gen_agent_skill ]]; then
                    run_step "$check" python "tools/$check.py" --check
                else
                    run_step "$check" python "tools/$check.py"
                fi
            done ;;
        mkdocs) run_step mkdocs mkdocs_build ;;
        build:*) run_step "$step" pio run -e "${step#build:}" ;;
    esac
done
exit "$failed"
