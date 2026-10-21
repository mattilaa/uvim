#!/usr/bin/env bash
# Prepare one source snapshot for every job in a CI run. Run from the checkout;
# the first argument is the output bundle path (outside the checkout).
set -euo pipefail

bundle_path="${1:?usage: ci_prepare_source.sh OUTPUT_BUNDLE}"
rebase_branches="${CI_REBASE_BRANCHES:-true}"
case "$rebase_branches" in
  true|false) ;;
  *) echo "CI_REBASE_BRANCHES must be true or false" >&2; exit 1 ;;
esac

original_sha="$(git rev-parse HEAD)"
main_sha=""
if [[ "$rebase_branches" == true && "${GITHUB_EVENT_NAME:-}" == push &&
      "${GITHUB_REF:-}" == refs/heads/* &&
      "${GITHUB_REF:-}" != refs/heads/main ]]; then
  # Fetch when the queued run actually starts, rather than using the version
  # of main that existed when its triggering push occurred.
  git fetch --no-tags origin +refs/heads/main:refs/remotes/origin/main
  main_sha="$(git rev-parse refs/remotes/origin/main)"
  # Rewrite only this temporary checkout, leaving branch refs untouched.
  git checkout --detach "$original_sha"
  if ! git -c user.name='github-actions[bot]' \
           -c user.email='41898282+github-actions[bot]@users.noreply.github.com' \
           rebase --rebase-merges "$main_sha"; then
    git rebase --abort
    echo "::error::Cannot rebase this branch onto main ($main_sha). Resolve the conflicts on the branch and push again."
    exit 1
  fi
fi

# Includes Git history so downstream jobs can check out the exact same commit,
# including on other platforms. No credentials or local Git config are bundled.
git bundle create "$bundle_path" HEAD
prepared_sha="$(git rev-parse HEAD)"
echo "Prepared CI source: $prepared_sha (original: $original_sha)"
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo '### CI source'
    echo "- Triggering commit: \`$original_sha\`"
    if [[ -n "$main_sha" ]]; then
      echo "- Rebased onto main: \`$main_sha\`"
    else
      echo '- Using the triggering checkout without rebasing.'
    fi
    echo "- Tested commit: \`$prepared_sha\`"
  } >> "$GITHUB_STEP_SUMMARY"
fi
