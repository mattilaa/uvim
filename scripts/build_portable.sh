#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build_portable.sh [options]

Builds uVim on the current macOS or Linux host and creates a versioned
tar.gz archive containing the executable, themes, README, and license.

Options:
  --profile NAME          vi-real, vi-min, minimal, basic, or full
                          (default: full)
  -B, --build-dir DIR     Isolated CMake directory
                          (default: build-portable)
  -o, --output-dir DIR    Archive destination (default: dist)
  -j, --jobs N            Parallel build jobs (default: detected CPU count)
  --macos-arch ARCH       native, universal, arm64, or x86_64
                          (default: native)
  --deployment-target V   Minimum macOS version (default: 13.3)
  --dynamic               Do not request a static Linux executable
  --force                 Replace an existing archive and checksum
  -h, --help              Show this help

Notes:
  * Build on macOS to create a macOS archive and on Linux for a Linux archive.
  * Linux uses static linking by default. Install the static C/C++ runtime
    libraries for your compiler if the linker reports missing archives.
  * macOS does not support fully static executables. The script accepts only
    binaries linked to Apple system libraries.
  * Git, ripgrep, formatters, clipboard helpers, and language servers are
    optional external programs and are not embedded in the archive.
EOF
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

profile="full"
build_dir="build-portable"
output_dir="dist"
jobs=""
macos_arch="native"
deployment_target="13.3"
static_linux=true
force=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --profile)
      [[ $# -ge 2 ]] || { echo "error: --profile requires a value" >&2; exit 2; }
      profile="$2"
      shift 2
      ;;
    --profile=*) profile="${1#*=}"; shift ;;
    -B|--build-dir)
      [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
      build_dir="$2"
      shift 2
      ;;
    --build-dir=*) build_dir="${1#*=}"; shift ;;
    -o|--output-dir)
      [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --output-dir=*) output_dir="${1#*=}"; shift ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
      jobs="$2"
      shift 2
      ;;
    --jobs=*) jobs="${1#*=}"; shift ;;
    --macos-arch)
      [[ $# -ge 2 ]] || { echo "error: --macos-arch requires a value" >&2; exit 2; }
      macos_arch="$2"
      shift 2
      ;;
    --macos-arch=*) macos_arch="${1#*=}"; shift ;;
    --deployment-target)
      [[ $# -ge 2 ]] || { echo "error: --deployment-target requires a value" >&2; exit 2; }
      deployment_target="$2"
      shift 2
      ;;
    --deployment-target=*) deployment_target="${1#*=}"; shift ;;
    --dynamic) static_linux=false; shift ;;
    --force) force=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

case "$profile" in
  vi-real|vi-min|minimal|basic|full) ;;
  *) echo "error: unsupported profile '$profile'" >&2; exit 2 ;;
esac

case "$macos_arch" in
  native|universal|arm64|x86_64) ;;
  *) echo "error: unsupported macOS architecture '$macos_arch'" >&2; exit 2 ;;
esac

if [[ -z "$jobs" ]]; then
  if command -v getconf >/dev/null 2>&1; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  fi
  if [[ -z "$jobs" ]] && command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.ncpu 2>/dev/null || true)"
  fi
  jobs="${jobs:-1}"
fi
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo "error: jobs must be a positive integer" >&2; exit 2; }

for tool in cmake tar file; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "error: required tool '$tool' was not found" >&2
    exit 1
  }
done

host_os="$(uname -s)"
host_arch="$(uname -m)"
case "$host_arch" in
  aarch64) host_arch="arm64" ;;
  amd64) host_arch="x86_64" ;;
esac

case "$host_os" in
  Darwin) platform="macos" ;;
  Linux) platform="linux" ;;
  *) echo "error: only macOS and Linux are supported (found $host_os)" >&2; exit 1 ;;
esac

cd "$repo_root"
mkdir -p "$build_dir" "$output_dir"

generator_args=()
if command -v ninja >/dev/null 2>&1; then
  generator_args=(-G Ninja)
fi

platform_args=()
package_arch="$host_arch"
if [[ "$platform" == "macos" ]]; then
  case "$macos_arch" in
    native) package_arch="$host_arch" ;;
    universal)
      platform_args+=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64")
      package_arch="universal"
      ;;
    arm64|x86_64)
      platform_args+=("-DCMAKE_OSX_ARCHITECTURES=$macos_arch")
      package_arch="$macos_arch"
      ;;
  esac
  platform_args+=("-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment_target")
fi

echo "==> Bootstrapping uvim-config"
cmake -S "$repo_root" -B "$build_dir" \
  -DUVIM_BOOTSTRAP_CONFIG_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Release \
  "${platform_args[@]}" "${generator_args[@]}"
cmake --build "$build_dir" --target uvim-config --parallel "$jobs"

uvim_config="$build_dir/uvim-config"
[[ -x "$uvim_config" ]] || { echo "error: uvim-config was not built" >&2; exit 1; }

cache_file="$build_dir/portable_cache.cmake"
config_args=(
  --preset "$profile"
  --config Release
  --platform POSIX
  --optimization O2
  --jobs "$jobs"
  --source-dir "$repo_root"
  --build-dir "$build_dir"
  --output "$cache_file"
  --disable tests
  --disable compile-commands
  --disable auto-build-number
  --disable sanitizers
  --disable debug-logging
  --disable debug-lsp
  --enable lto
  --enable gc-sections
  --enable strip
)
if [[ "$platform" == "linux" ]] && $static_linux; then
  config_args+=(--enable static-link)
else
  config_args+=(--disable static-link)
fi

echo "==> Generating portable $profile configuration"
"$uvim_config" "${config_args[@]}"

echo "==> Building uVim"
cmake -C "$cache_file" -S "$repo_root" -B "$build_dir" \
  -DUVIM_BOOTSTRAP_CONFIG_ONLY=OFF \
  "${platform_args[@]}" "${generator_args[@]}"
cmake --build "$build_dir" --target uvim --parallel "$jobs"

binary="$build_dir/uvim"
[[ -x "$binary" ]] || { echo "error: expected binary is missing: $binary" >&2; exit 1; }

dependency_report=""
if [[ "$platform" == "macos" ]]; then
  command -v otool >/dev/null 2>&1 || { echo "error: otool is required on macOS" >&2; exit 1; }
  dependency_report="$(otool -L "$binary")"
  non_system="$(printf '%s\n' "$dependency_report" | tail -n +2 | awk '{print $1}' | grep -Ev '^(/usr/lib/|/System/Library/)' || true)"
  if [[ -n "$non_system" ]]; then
    echo "error: non-system macOS libraries prevent portable packaging:" >&2
    printf '  %s\n' "$non_system" >&2
    exit 1
  fi
else
  if command -v ldd >/dev/null 2>&1; then
    dependency_report="$(ldd "$binary" 2>&1 || true)"
  else
    dependency_report="$(file "$binary")"
  fi
  if $static_linux; then
    if [[ "$dependency_report" != *"not a dynamic executable"* &&
          "$dependency_report" != *"statically linked"* &&
          "$dependency_report" != *"static-pie linked"* ]]; then
      echo "error: Linux binary is still dynamically linked:" >&2
      printf '%s\n' "$dependency_report" >&2
      echo "Install static runtime libraries, or pass --dynamic." >&2
      exit 1
    fi
  fi
fi

version="$($binary --version | awk 'NR == 1 { print $2 }')"
[[ -n "$version" ]] || version="unknown"
bundle_name="uvim-${version}-${platform}-${package_arch}"
archive="$output_dir/${bundle_name}.tar.gz"
checksum="$archive.sha256"

if [[ -e "$archive" || -e "$checksum" ]]; then
  if ! $force; then
    echo "error: output already exists; pass --force to replace it: $archive" >&2
    exit 1
  fi
  rm -f -- "$archive" "$checksum"
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/uvim-portable.XXXXXX")"
cleanup() { rm -rf -- "$stage_dir"; }
trap cleanup EXIT
bundle_dir="$stage_dir/$bundle_name"
mkdir -p "$bundle_dir"
cp "$binary" "$bundle_dir/uvim"
cp "$repo_root/LICENSE" "$repo_root/README.md" "$bundle_dir/"
cp -R "$repo_root/themes" "$bundle_dir/themes"
printf '%s\n' "$dependency_report" > "$bundle_dir/DEPENDENCIES.txt"
cat > "$bundle_dir/PORTABLE.txt" <<EOF
uVim $version portable build
Platform: $platform
Architecture: $package_arch
Profile: $profile

Run directly:
  ./uvim [file]

Optional system programs are discovered through PATH and are not bundled:
Git, ripgrep/fzf, formatters, clipboard helpers, and language servers.

Generate a user configuration and copy the bundled themes with:
  ./uvim --init-config
EOF

echo "==> Creating $archive"
COPYFILE_DISABLE=1 tar -czf "$archive" -C "$stage_dir" "$bundle_name"
if command -v sha256sum >/dev/null 2>&1; then
  (cd "$output_dir" && sha256sum "$(basename "$archive")") > "$checksum"
else
  (cd "$output_dir" && shasum -a 256 "$(basename "$archive")") > "$checksum"
fi

echo "Portable archive: $archive"
echo "SHA-256:         $checksum"
file "$binary"
