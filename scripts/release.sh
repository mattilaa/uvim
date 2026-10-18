#!/usr/bin/env bash
# Version bumps and GitHub releases for uVim.
#
#   scripts/release.sh --bump [VERSION|major|minor|patch]
#       Show the current version, ask for the new one (must be higher),
#       update version.txt and the version examples, add a release-notes
#       stub to README.md and commit "Version X.Y.Z".
#   scripts/release.sh --check-notes [VERSION]
#       Check README.md has finished release notes for VERSION (default:
#       the version in version.txt).
#   scripts/release.sh --publish
#       Release the version in version.txt: check main is clean and up to
#       date, the tag is new, the notes are done and CI passed, then push the
#       tag vX.Y.Z; the release workflow builds and publishes the release.
#       Afterwards main moves on to the next patch version (asks first).
#
# Every step that changes something asks first. -y answers yes.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

assume_yes=false
bump_confirmed=false
mode=""
arg=""

usage() {
  # The comment block at the top of this file.
  awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
}

die() {
  echo "release.sh: $*" >&2
  exit 1
}

# Ask a yes/no question on the terminal (default no).
confirm() {
  if $assume_yes; then
    echo "$1 [y/N] y"
    return 0
  fi
  local answer
  read -r -p "$1 [y/N] " answer </dev/tty || return 1
  [[ "$answer" == y || "$answer" == Y || "$answer" == yes ]]
}

# The release version lives in version.txt (read by CMake).
current_version() {
  local version
  [[ -f version.txt ]] || die "version.txt not found"
  version="$(tr -d '[:space:]' <version.txt)"
  valid_version "$version" ||
    die "version.txt must hold MAJOR.MINOR.PATCH, found '$version'"
  echo "$version"
}

valid_version() {
  [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

# Exit status 0 when $1 > $2 (numeric, per component).
version_greater() {
  local IFS=.
  local -a a=($1) b=($2)
  for i in 0 1 2; do
    ((10#${a[i]} > 10#${b[i]})) && return 0
    ((10#${a[i]} < 10#${b[i]})) && return 1
  done
  return 1
}

next_version() {
  local IFS=.
  local -a v=($1)
  case "$2" in
    major) echo "$((v[0] + 1)).0.0" ;;
    minor) echo "${v[0]}.$((v[1] + 1)).0" ;;
    patch) echo "${v[0]}.${v[1]}.$((v[2] + 1))" ;;
  esac
}

latest_tag() {
  git tag --list 'v[0-9]*' --sort=-v:refname | head -n 1
}

# The "### vX.Y.Z ..." section of README.md's release notes (without its
# heading), or nothing.
notes_section() {
  awk -v head="### v$1 " '
    index($0, head) == 1 { on = 1; next }
    on && (/^### v/ || /^## /) { exit }
    on { print }' README.md
}

notes_heading() {
  grep -m 1 "^### v$1 " README.md || true
}

check_notes() {
  local version="$1" heading body
  heading="$(notes_heading "$version")"
  [[ -n "$heading" ]] || {
    echo "README.md has no release notes for v$version (a \"### v$version (date)\" section under \"## Release notes\")." >&2
    return 1
  }
  body="$(notes_section "$version")"
  if ! grep -q '[^[:space:]]' <<<"$body"; then
    echo "The release notes for v$version are empty." >&2
    return 1
  fi
  if grep -qi 'TODO' <<<"$body"; then
    echo "The release notes for v$version still contain TODO:" >&2
    grep -ni 'TODO' <<<"$body" | sed 's/^/  /' >&2
    return 1
  fi
  echo "Release notes for v$version: OK ($heading)"
}

# Replaces version examples outside the "## Release notes" section (the
# history there stays as written).
replace_examples() {
  local old="$1" new="$2" file
  local old_re="${old//./\\.}"
  for file in README.md scripts/build_portable.ps1 \
    scripts/build_windows_installer.ps1 packaging/windows/uvim.iss \
    .github/workflows/portable-release.yml; do
    [[ -f "$file" ]] || continue
    awk -v old="$old_re" -v new="$new" -v readme="$([[ $file == README.md ]] && echo 1)" '
      readme && /^## Release notes/ { notes = 1 }
      readme && notes && /^## / && !/^## Release notes/ { notes = 0 }
      { if (!notes) gsub(old, new); print }' "$file" >"$file.release-tmp"
    mv "$file.release-tmp" "$file"
  done
}

add_notes_stub() {
  local version="$1"
  [[ -n "$(notes_heading "$version")" ]] && return 0
  awk -v version="$version" '
    { print }
    /^## Release notes/ && !done {
      getline blank; print blank
      print "### v" version " (unreleased)"
      print ""
      print "- TODO: describe what is new, improved and fixed."
      print ""
      done = 1
    }' README.md >README.md.release-tmp
  mv README.md.release-tmp README.md
}

do_bump() {
  local current requested new tag
  current="$(current_version)"
  tag="$(latest_tag)"
  echo "Current version: $current (version.txt)"
  echo "Latest release:  ${tag:-none}"

  requested="$arg"
  if [[ -z "$requested" ]]; then
    local suggestion
    suggestion="$(next_version "$current" patch)"
    read -r -p "New version (X.Y.Z, or major/minor/patch) [$suggestion]: " requested </dev/tty
    requested="${requested:-$suggestion}"
  fi
  case "$requested" in
    major|minor|patch) new="$(next_version "$current" "$requested")" ;;
    v*) new="${requested#v}" ;;
    *) new="$requested" ;;
  esac
  valid_version "$new" || die "not a MAJOR.MINOR.PATCH version: $requested"
  version_greater "$new" "$current" ||
    die "$new is not higher than the current version $current"
  if [[ -n "$tag" ]] && ! version_greater "$new" "${tag#v}"; then
    die "$new is not higher than the latest release $tag"
  fi

  [[ -z "$(git status --porcelain)" ]] ||
    die "the working tree has changes; commit or stash them first"
  $bump_confirmed ||
    confirm "Bump $current -> $new and commit \"Version $new\"?" || {
    echo "Nothing changed."
    return 0
  }

  printf '%s\n' "$new" >version.txt
  [[ "$(current_version)" == "$new" ]] || die "failed to update version.txt"
  replace_examples "$current" "$new"
  add_notes_stub "$new"

  git add version.txt README.md scripts packaging .github
  git add -f scripts/build_portable.ps1 scripts/build_windows_installer.ps1
  git commit -q -m "Version $new"
  echo "Committed \"Version $new\" ($(git rev-parse --short HEAD))."
  echo "Next: write the release notes in README.md (### v$new), commit,"
  echo "merge to main, then: scripts/release.sh --publish"
}

do_check_notes() {
  local version="${arg:-$(current_version)}"
  check_notes "${version#v}"
}

# "completed success", "in_progress ", ... or nothing when unknown (no run,
# gh not logged in, no network).
ci_status() {
  gh run list --workflow ci.yml --commit "$1" --limit 1 \
    --json status,conclusion --jq '.[0] | "\(.status) \(.conclusion)"' \
    2>/dev/null || true
}

do_publish() {
  command -v gh >/dev/null 2>&1 || die "the GitHub CLI (gh) is needed"
  local version tag branch sha
  version="$(current_version)"
  tag="v$version"
  echo "Version to release: $version (latest release: $(latest_tag || true))"

  branch="$(git rev-parse --abbrev-ref HEAD)"
  [[ "$branch" == main ]] || die "releases are made from main (on $branch)"
  [[ -z "$(git status --porcelain)" ]] || die "the working tree has changes"
  git fetch -q origin main --tags
  sha="$(git rev-parse HEAD)"
  [[ "$sha" == "$(git rev-parse origin/main)" ]] ||
    die "main is not the same as origin/main; pull or push first"
  if git rev-parse -q --verify "refs/tags/$tag" >/dev/null ||
    git ls-remote --exit-code --tags origin "refs/tags/$tag" >/dev/null 2>&1; then
    die "$tag already exists; bump the version first (--bump)"
  fi
  local previous
  previous="$(latest_tag)"
  if [[ -n "$previous" ]] && ! version_greater "$version" "${previous#v}"; then
    die "$version is not higher than the latest release $previous"
  fi

  # Content first, so nothing is committed while the notes are unfinished.
  check_notes "$version" || die "finish the release notes first"
  if grep -q "^### v$version (unreleased)" README.md; then
    echo "The release notes heading says \"(unreleased)\"."
    if confirm "Set it to today's date ($(date +%F)), commit and push to main?"; then
      sed -i.release-tmp "s/^### v$version (unreleased)/### v$version ($(date +%F))/" README.md
      rm -f README.md.release-tmp
      git add README.md
      git commit -q -m "Release notes date for v$version"
      git push -q origin main
      sha="$(git rev-parse HEAD)"
    fi
  fi

  local status
  status="$(ci_status "$sha")"
  echo "CI for ${sha:0:7}: ${status:-no run found}"
  if [[ "$status" == in_progress* || "$status" == queued* || "$status" == pending* ]]; then
    if confirm "Wait for CI to finish?"; then
      while status="$(ci_status "$sha")"; [[ "$status" != completed* ]]; do
        sleep 30
      done
      echo "CI for ${sha:0:7}: $status"
    fi
  fi
  if [[ "$status" != "completed success" ]]; then
    confirm "CI has not passed for this commit. Release anyway?" ||
      die "release cancelled"
  fi

  confirm "Create and push tag $tag (publishes the GitHub release)?" || {
    echo "Nothing pushed."
    return 0
  }
  git tag -a "$tag" -m "uVim $tag"
  git push -q origin "$tag"
  echo "Pushed $tag; the release workflow builds and publishes it."

  if confirm "Wait for the release workflow and show the release?"; then
    local run=""
    for _ in $(seq 1 30); do
      run="$(gh run list --workflow portable-release.yml --branch "$tag" \
        --limit 1 --json databaseId --jq '.[0].databaseId')"
      [[ -n "$run" ]] && break
      sleep 5
    done
    [[ -n "$run" ]] || die "no release workflow run found for $tag"
    gh run watch "$run" --exit-status >/dev/null ||
      die "the release workflow failed: gh run view $run"
    gh release view "$tag" --json url,assets \
      --jq '.url, (.assets[] | "  \(.name)")'
  fi

  # The released version is done: main moves on to the next one, so it is
  # never still the released number.
  local next
  next="$(next_version "$version" patch)"
  echo
  echo "Start the next version on main: $version -> $next (version.txt and"
  echo "a \"### v$next (unreleased)\" release-notes stub)."
  if confirm "Bump to $next, commit and push to main?"; then
    arg="$next"
    bump_confirmed=true
    do_bump
    git push -q origin main
    echo "main is now at $next (pushed)."
  else
    echo "main stays at $version; run scripts/release.sh --bump before the"
    echo "next changes are released."
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bump | --check-notes)
      mode="${1#--}"
      if [[ $# -ge 2 && "$2" != -* ]]; then
        arg="$2"
        shift
      fi
      ;;
    --publish) mode=publish ;;
    -y | --yes) assume_yes=true ;;
    -h | --help)
      usage
      exit 0
      ;;
    *) die "unknown option: $1 (see --help)" ;;
  esac
  shift
done

case "$mode" in
  bump) do_bump ;;
  check-notes) do_check_notes ;;
  publish) do_publish ;;
  *)
    usage
    exit 2
    ;;
esac
