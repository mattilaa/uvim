#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: build_install.sh [--install] [--no-install] [--debug] [--no-debug]
                        [--tests] [--unit-tests] [--robot-tests] [--no-tests]
                        [--doxygen]
                        [--install-if-tests-pass]
                        [--prefix <path>] [--help]

Builds uvim with the standard Ninja Release config and optionally installs it.

Options:
  --install          Install after build (default)
  --no-install       Skip install step
  --debug            Enable UVIM_DEBUG_LSP and UVIM_DEBUG_LOGGING
  --no-debug         Disable debug logging (default)
  --tests            Run unit + robot tests after build (installs only if tests pass)
  --unit-tests       Run unit tests after build (ctest, installs only if tests pass)
  --robot-tests      Run robot tests after build (installs only if tests pass)
  --no-tests         Skip all tests (default)
  --doxygen          Build Doxygen docs after build
  --install-if-tests-pass  Install only if requested tests pass
  --prefix <path>    Install prefix (default: $HOME/.local)
  --help             Show this help

Notes:
  - Install runs by default unless --no-install is set.
  - --tests/--unit-tests/--robot-tests imply --install-if-tests-pass.
  - --install-if-tests-pass requires tests to be selected.
  - --no-tests and --no-install clear --install-if-tests-pass.
EOF
}

install_after_build=true
prefix="${HOME}/.local"
debug=false
run_unit_tests=false
run_robot_tests=false
install_if_tests_pass=false
build_doxygen=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help)
      usage
      exit 0
      ;;
    --install)
      install_after_build=true
      shift
      ;;
    --no-install)
      install_after_build=false
      install_if_tests_pass=false
      shift
      ;;
    --prefix)
      if [[ $# -lt 2 ]]; then
        echo "error: --prefix requires a value" >&2
        exit 1
      fi
      prefix="$2"
      shift 2
      ;;
    --debug)
      debug=true
      shift
      ;;
    --no-debug)
      debug=false
      shift
      ;;
    --tests)
      run_unit_tests=true
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --unit-tests)
      run_unit_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --robot-tests)
      run_robot_tests=true
      install_if_tests_pass=true
      shift
      ;;
    --no-tests)
      run_unit_tests=false
      run_robot_tests=false
      install_if_tests_pass=false
      shift
      ;;
    --install-if-tests-pass)
      install_if_tests_pass=true
      shift
      ;;
    --doxygen)
      build_doxygen=true
      shift
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

cmake_args=(
  -S .
  -B build
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DUVIM_EXPORT_COMPILE_COMMANDS=ON
  -DUVIM_ENABLE_CLANGD_LSP=ON
  -DUVIM_BUILD_TESTS=ON
)

if $debug; then
  cmake_args+=(-DUVIM_DEBUG_LSP=ON -DUVIM_DEBUG_LOGGING=ON)
else
  cmake_args+=(-DUVIM_DEBUG_LSP=OFF -DUVIM_DEBUG_LOGGING=OFF)
fi

cmake "${cmake_args[@]}"

cmake --build build

if $run_unit_tests; then
  ctest --test-dir build --output-on-failure
fi

if $run_robot_tests; then
  ./tests/robot/run_robot.sh
fi

if $build_doxygen; then
  ./scripts/build_docs.sh
fi

if $install_after_build; then
  if $install_if_tests_pass && ! $run_unit_tests && ! $run_robot_tests; then
    echo "error: --install-if-tests-pass requires --tests, --unit-tests, or --robot-tests" >&2
    exit 1
  fi
  cmake --install build --prefix "$prefix" --component uvim
fi
