#!/usr/bin/env bash
# One entry point for building, running, testing and packaging exTracker.
# Build directories come from CMakePresets.json (build/dev, build/release);
# nothing else should name them.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_DIR"

usage() {
    cat <<'EOF'
Usage: ./x <command> [args]

  build [target...]  Build the dev tree (default: everything)
  cli [args]         Build and run the CLI
  gui                Build and run the GUI (reads ~/.config/extracker/env)
  test [ctest args]  Build everything and run the test suite
                     e.g. ./x test -R cli_record_chord
  package            Release build + tarball in the repo root
  clean [dev|release|all]  Remove a build tree (default: dev)
EOF
}

# configure <preset>: configure on first use only (the first GUI configure
# fetches and builds JUCE, which takes a few minutes).
configure() {
    if [[ ! -f "build/$1/CMakeCache.txt" ]]; then
        echo "Configuring build/$1 (first time only)..."
        cmake --preset "$1"
    fi
}

# build <preset> [target...]
build() {
    local preset="$1"
    shift
    configure "$preset"
    local targets=()
    for target in "$@"; do targets+=(--target "$target"); done
    cmake --build --preset "$preset" "${targets[@]}"
}

command="${1:-help}"
[[ $# -gt 0 ]] && shift

case "$command" in
    build)
        build dev "$@"
        ;;
    cli)
        build dev extracker >&2
        cd build/dev
        exec ./extracker "$@"
        ;;
    gui)
        build dev extracker_gui
        cd build/dev
        exec "$REPO_DIR/tools/run-gui.sh" ./extracker_gui
        ;;
    test)
        build dev
        exec ctest --preset dev "$@"
        ;;
    package)
        build release extracker extracker_gui
        exec "$REPO_DIR/tools/package.sh" "$REPO_DIR/build/release"
        ;;
    clean)
        case "${1:-dev}" in
            dev | release) rm -rf "build/${1:-dev}" ;;
            all) rm -rf build/dev build/release ;;
            *) usage >&2; exit 1 ;;
        esac
        ;;
    help | -h | --help)
        usage
        ;;
    *)
        echo "Unknown command: $command" >&2
        usage >&2
        exit 1
        ;;
esac
