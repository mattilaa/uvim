#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build_macos_pkg.sh --binary PATH --version VERSION [options]

Creates a macOS installer package that installs uvim in /usr/local/bin.
The package is unsigned by default and does not require an Apple Developer
Program membership. Use --sign only when a signing identity is available.

Options:
  --binary PATH        Universal or single-architecture uvim executable
  --version VERSION    Public vMAJOR.MINOR.BUGFIX release version
  -o, --output-dir DIR Package destination (default: dist)
  --identifier ID      Package identifier (default: io.github.mattilaa.uvim)
  --sign IDENTITY      Optional Developer ID Installer identity
  --force              Replace an existing package and checksum
  -h, --help           Show this help
EOF
}

binary=""
public_version=""
output_dir="dist"
identifier="io.github.mattilaa.uvim"
sign_identity=""
force=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --binary)
      [[ $# -ge 2 ]] || { echo "error: --binary requires a value" >&2; exit 2; }
      binary="$2"
      shift 2
      ;;
    --binary=*) binary="${1#*=}"; shift ;;
    --version)
      [[ $# -ge 2 ]] || { echo "error: --version requires a value" >&2; exit 2; }
      public_version="$2"
      shift 2
      ;;
    --version=*) public_version="${1#*=}"; shift ;;
    -o|--output-dir)
      [[ $# -ge 2 ]] || { echo "error: $1 requires a value" >&2; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --output-dir=*) output_dir="${1#*=}"; shift ;;
    --identifier)
      [[ $# -ge 2 ]] || { echo "error: --identifier requires a value" >&2; exit 2; }
      identifier="$2"
      shift 2
      ;;
    --identifier=*) identifier="${1#*=}"; shift ;;
    --sign)
      [[ $# -ge 2 ]] || { echo "error: --sign requires a value" >&2; exit 2; }
      sign_identity="$2"
      shift 2
      ;;
    --sign=*) sign_identity="${1#*=}"; shift ;;
    --force) force=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

[[ "$(uname -s)" == "Darwin" ]] || {
  echo "error: macOS pkg installers must be built on macOS" >&2
  exit 1
}
command -v pkgbuild >/dev/null 2>&1 || {
  echo "error: pkgbuild was not found" >&2
  exit 1
}
for tool in file lipo xattr; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "error: $tool was not found" >&2
    exit 1
  }
done
[[ -n "$binary" && -x "$binary" ]] || {
  echo "error: executable uvim binary was not found: $binary" >&2
  exit 1
}
[[ "$public_version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
  echo "error: version must use vMAJOR.MINOR.BUGFIX: $public_version" >&2
  exit 2
}
[[ "$identifier" =~ ^[A-Za-z0-9][A-Za-z0-9.-]+$ ]] || {
  echo "error: invalid package identifier: $identifier" >&2
  exit 2
}

mkdir -p "$output_dir"
binary_arches="$(lipo -archs "$binary")"
if [[ " $binary_arches " == *" arm64 "* &&
      " $binary_arches " == *" x86_64 "* ]]; then
  package_arch="universal"
elif [[ "$binary_arches" =~ ^(arm64|x86_64)$ ]]; then
  package_arch="$binary_arches"
else
  echo "error: unsupported Mach-O architectures: $binary_arches" >&2
  exit 1
fi

package="$output_dir/uvim-${public_version}-macos-${package_arch}.pkg"
checksum="$package.sha256"
if [[ -e "$package" || -e "$checksum" ]]; then
  if ! $force; then
    echo "error: output already exists; pass --force to replace it: $package" >&2
    exit 1
  fi
  rm -f -- "$package" "$checksum"
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/uvim-pkg.XXXXXX")"
cleanup() { rm -rf -- "$stage_dir"; }
trap cleanup EXIT

mkdir -p "$stage_dir/root/usr/local/bin"
cp "$binary" "$stage_dir/root/usr/local/bin/uvim"
chmod 0755 "$stage_dir/root/usr/local/bin/uvim"
xattr -cr "$stage_dir/root"

pkg_args=(
  --root "$stage_dir/root"
  --identifier "$identifier"
  --version "${public_version#v}"
  --install-location /
  --ownership recommended
)
if [[ -n "$sign_identity" ]]; then
  pkg_args+=(--sign "$sign_identity")
fi

echo "==> Creating $package"
COPYFILE_DISABLE=1 pkgbuild "${pkg_args[@]}" "$package"

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$output_dir" && sha256sum "$(basename "$package")") > "$checksum"
else
  (cd "$output_dir" && shasum -a 256 "$(basename "$package")") > "$checksum"
fi

echo "macOS installer: $package"
echo "SHA-256:         $checksum"
if [[ -z "$sign_identity" ]]; then
  echo "warning: package is unsigned; macOS user approval may be required to install it" >&2
fi
file "$package"
