#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: scripts/build_macos_pkg.sh --binary PATH --version VERSION [options]

Creates a macOS installer package with two parts:
  uVim command-line editor  uvim in /usr/local/bin (always installed)
  uVim app                  /Applications/uVim.app (optional, on by default)
                            opens uvim in its own terminal window: Alacritty
                            when installed, otherwise Terminal
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
for tool in file lipo xattr pkgbuild productbuild osacompile plutil codesign; do
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
binary_version="v$("$binary" --version | awk 'NR == 1 { print $2 }')"
[[ "$public_version" == "$binary_version" ]] || {
  echo "error: version $public_version does not match the binary" \
       "($binary_version)" >&2
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

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
macos_dir="$repo_root/packaging/macos"
app_identifier="$identifier.app"
short_version="${public_version#v}"
sign_args=()
if [[ -n "$sign_identity" ]]; then
  sign_args=(--sign "$sign_identity")
fi

# --- Component 1: the command-line editor in /usr/local/bin --------------
mkdir -p "$stage_dir/cli/usr/local/bin" "$stage_dir/components"
cp "$binary" "$stage_dir/cli/usr/local/bin/uvim"
chmod 0755 "$stage_dir/cli/usr/local/bin/uvim"
xattr -cr "$stage_dir/cli"
COPYFILE_DISABLE=1 pkgbuild --root "$stage_dir/cli" \
  --identifier "$identifier" --version "$short_version" \
  --install-location / --ownership recommended \
  "$stage_dir/components/uvim-cli.pkg"

# --- Component 2: uVim.app in /Applications --------------------------------
app="$stage_dir/app/Applications/uVim.app"
mkdir -p "$stage_dir/app/Applications"
osacompile -o "$app" "$macos_dir/uVim.applescript"
cp "$macos_dir/uvim-launch.sh" "$app/Contents/Resources/uvim-launch.sh"
chmod 0755 "$app/Contents/Resources/uvim-launch.sh"
# The uVim icon: replace the droplet icon and drop the asset catalog, whose
# icon would win over the .icns.
cp "$macos_dir/uVim.icns" "$app/Contents/Resources/droplet.icns"
rm -f "$app/Contents/Resources/Assets.car"
plist="$app/Contents/Info.plist"
plutil -remove CFBundleIconName "$plist" 2>/dev/null || true
plutil -replace CFBundleIdentifier -string "$app_identifier" "$plist"
plutil -replace CFBundleName -string "uVim" "$plist"
plutil -replace CFBundleShortVersionString -string "$short_version" "$plist"
plutil -replace CFBundleVersion -string "$short_version" "$plist"
xattr -cr "$app"
# Editing the bundle invalidates osacompile's ad-hoc signature; Apple
# silicon only runs signed code, so sign it again (ad hoc).
codesign --force --deep --sign - "$app"
pkgbuild --analyze --root "$stage_dir/app" "$stage_dir/app-components.plist"
plutil -replace 0.BundleIsRelocatable -bool NO "$stage_dir/app-components.plist"
COPYFILE_DISABLE=1 pkgbuild --root "$stage_dir/app" \
  --component-plist "$stage_dir/app-components.plist" \
  --identifier "$app_identifier" --version "$short_version" \
  --install-location / --ownership recommended \
  "$stage_dir/components/uvim-app.pkg"

# --- Product: both components with a choice page ---------------------------
case "$package_arch" in
  universal) host_arches="arm64,x86_64" ;;
  *) host_arches="$package_arch" ;;
esac
mkdir -p "$stage_dir/resources"
cp "$macos_dir/installer-background.png" "$stage_dir/resources/background.png"
cat > "$stage_dir/distribution.xml" <<DISTRIBUTION
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>uVim $public_version</title>
  <background file="background.png" alignment="bottomleft" scaling="none"/>
  <background-darkAqua file="background.png" alignment="bottomleft" scaling="none"/>
  <options customize="always" require-scripts="false" hostArchitectures="$host_arches"/>
  <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
  <choices-outline>
    <line choice="cli"/>
    <line choice="app"/>
  </choices-outline>
  <choice id="cli" title="uVim command-line editor" enabled="false" start_selected="true"
          description="Installs uvim in /usr/local/bin. Run it in any terminal: uvim [file]">
    <pkg-ref id="$identifier"/>
  </choice>
  <choice id="app" title="uVim app" start_selected="true"
          description="Adds uVim to Applications. It opens uVim in its own terminal window: Alacritty when it is installed, otherwise Terminal. Files dropped on the app open in uVim.">
    <pkg-ref id="$app_identifier"/>
  </choice>
  <pkg-ref id="$identifier" version="$short_version">uvim-cli.pkg</pkg-ref>
  <pkg-ref id="$app_identifier" version="$short_version">uvim-app.pkg</pkg-ref>
</installer-gui-script>
DISTRIBUTION

echo "==> Creating $package"
productbuild --distribution "$stage_dir/distribution.xml" \
  --package-path "$stage_dir/components" \
  --resources "$stage_dir/resources" \
  ${sign_args[@]+"${sign_args[@]}"} "$package"

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
