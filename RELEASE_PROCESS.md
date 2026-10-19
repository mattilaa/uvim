# Release process

How uVim versions are numbered, how a release is prepared and published, and
what to do when something goes wrong. The steps are automated by
[`scripts/release.sh`](scripts/release.sh); every step that changes something
asks first (`-y` answers yes).

## Release checklist

Use this checklist each time you publish a release. Run commands from the
repository root in Bash (macOS/Linux, or Git Bash on Windows). You need Git,
the GitHub CLI (`gh`), and the project's build dependencies installed.

1. **Check access and your working tree.**

   ```sh
   gh auth status
   git status --short
   git fetch origin --tags
   ```

   If GitHub CLI authentication is missing, run `gh auth login`. Commit or
   stash any working-tree changes before continuing.

2. **Bring the prepared release into `main`.** For v0.3.2:

   ```sh
   git switch main
   git pull --ff-only origin main
   git merge --no-ff origin/release/v0.3.2
   ```

   Replace `origin/release/v0.3.2` with the branch for the release you are
   making. If all release changes are already on `main`, skip the merge.
   If there are merge conflicts, resolve and commit them before continuing.

3. **Check the version, release notes, build and tests.**

   ```sh
   cat version.txt
   scripts/release.sh --check-notes
   cmake -S . -B build -DUVIM_BUILD_TESTS=ON
   cmake --build build --parallel 4
   ctest --test-dir build --output-on-failure --parallel 4
   ```

   For v0.3.2, `version.txt` must contain `0.3.2`, and `README.md` must have
   finished notes under `### v0.3.2`. Do not bump an already prepared version.
   For a new version, follow [Versions](#versions) and
   [Releasing](#releasing) below. Fix any failed checks before continuing.

4. **Push `main` and publish.**

   ```sh
   git push origin main
   scripts/release.sh --publish
   ```

   Respond to the script's prompts as follows:

   - If the notes say `(unreleased)`, accept setting today's date and pushing
     the notes commit.
   - Accept waiting for CI. If CI has not passed or no run is found, decline
     releasing anyway. Check [GitHub Actions](https://github.com/mattilaa/uvim/actions/workflows/ci.yml),
     wait for a successful run on the latest `main` commit, and run
     `scripts/release.sh --publish` again.
   - Confirm creating and pushing the release tag, for example `v0.3.2`.
     **This publishes the release:** the tag starts the package builds and
     creates the GitHub Release automatically.
   - Accept waiting for the release workflow and displaying its assets.
   - After the release succeeds, accept starting the next patch version
     (`0.3.3` after `0.3.2`). The script commits and pushes the version bump
     and an empty release-notes section for future changes.

5. **Verify the published release.** Open
   [Releases](https://github.com/mattilaa/uvim/releases) and confirm the version,
   release notes, ZIP archives, installers and `.sha256` files are present.
   Follow [Checking a release](#checking-a-release) below to verify a download.
   If a package build fails, use [Dry runs, re-runs and fixes](#dry-runs-re-runs-and-fixes).

## Where to look on GitHub

- [CI](https://github.com/mattilaa/uvim/actions/workflows/ci.yml): tests and
  builds that must pass before tagging.
- [Portable release binaries](https://github.com/mattilaa/uvim/actions/workflows/portable-release.yml):
  package builds started by the release tag; open a run to inspect failures.
- [Releases](https://github.com/mattilaa/uvim/releases): published notes and
  downloadable packages.

## Versions

The release version lives in one place: [`version.txt`](version.txt) in the
repository root, as `MAJOR.MINOR.PATCH`. CMake reads it (and reconfigures when
it changes), and `uvim --version` prints it with the local build number:

```text
uvim 0.3.2 (build 12)
```

The build number is a per-build-directory counter and is never part of a
release. Release tags are `vMAJOR.MINOR.PATCH` and must match `version.txt`;
the `uvim_version` test and the release scripts check it.

Between releases `main` already carries the next version, with an
`(unreleased)` section in the release notes, so a development build never
reports a version that has been released.

## Release notes

Release notes are the `## Release notes` section of
[`README.md`](README.md#release-notes), one `### vX.Y.Z (date)` section per
release, newest first. The release workflow publishes the section of the
released version as the GitHub Release text (README links are made absolute).

Add notes while features are merged, grouped as *New*, *Improved*, *Fixed*,
*Platforms and installation* and *Development*. A section is finished when it
has no `TODO` left:

```sh
scripts/release.sh --check-notes        # the version in version.txt
scripts/release.sh --check-notes 0.3.2
```

## Releasing

1. **Version.** If `main` is not on the version to release yet:

   ```sh
   scripts/release.sh --bump            # shows the current version and the
                                        # latest release, asks for the new one
   scripts/release.sh --bump minor      # or major / patch / 0.4.0
   ```

   It refuses a version that is not higher than the current one and the latest
   release, updates `version.txt` and the version examples in the docs and
   scripts (not the release-notes history), adds a
   `### vX.Y.Z (unreleased)` stub with a `TODO` and commits "Version X.Y.Z".

2. **Notes.** Replace the `TODO` with the release notes and commit.

3. **Main.** Branches are rebased onto `main` before they are merged
   (`git rebase main`, tests, `git push --force-with-lease`, then
   `git merge --no-ff`). The release is made from `main`.

4. **Publish.**

   ```sh
   scripts/release.sh --publish
   ```

   It checks that

   - `main` is clean and the same as `origin/main`,
   - the tag `vX.Y.Z` does not exist and the version is higher than the latest
     release,
   - the release notes for the version exist and have no `TODO` (an
     `(unreleased)` heading is replaced with today's date, committed and
     pushed),
   - CI passed for the commit (it can wait for a running CI; releasing
     without a green CI needs an explicit yes),

   then pushes the tag. The tag starts the
   [release workflow](.github/workflows/portable-release.yml), which builds
   Release binaries for every supported target in parallel and attaches them,
   with a `.sha256` file each, to the GitHub Release:

   | Target         | Assets                                                         |
   |----------------|----------------------------------------------------------------|
   | Linux x86_64   | `uvim-vX.Y.Z-linux-x86_64.zip` (static)                        |
   | Linux arm64    | `uvim-vX.Y.Z-linux-arm64.zip` (static)                         |
   | macOS arm64    | `uvim-vX.Y.Z-macos-arm64.zip`, `uvim-vX.Y.Z-macos-arm64.pkg`   |
   | Windows x86_64 | `uvim-vX.Y.Z-windows-x86_64.zip`, `…-windows-x86_64-setup.exe` |

   The script can wait for the workflow and list the published assets.

5. **Next version.** Finally it offers to move `main` to the next patch
   version (`version.txt`, an `(unreleased)` release-notes stub, commit and
   push). Accept unless the next release is already planned as a minor or
   major one; then run `scripts/release.sh --bump minor` (or `major`) instead.

## Checking a release

Download an asset and its checksum from the release page:

```sh
shasum -a 256 -c uvim-v0.3.2-macos-arm64.zip.sha256
unzip uvim-v0.3.2-macos-arm64.zip
./uvim-v0.3.2-macos-arm64/uvim --version      # uvim 0.3.2 (build 1)
```

The macOS `.pkg` and the Windows installer are unsigned: macOS asks for
approval in **System Settings › Privacy & Security**, Windows SmartScreen
needs **More info › Run anyway**.

## Without the script

The script only automates these commands:

```sh
printf '0.3.2\n' > version.txt                # plus the release notes
git commit -am "Version 0.3.2"
git tag -a v0.3.2 -m "uVim v0.3.2"
git push origin main v0.3.2
```

## Dry runs, re-runs and fixes

- **Dry run.** In GitHub, **Actions › Portable release binaries › Run
  workflow** with a tag and *publish* unchecked builds every release archive
  and installer without touching any release. Useful before tagging changes
  to the build or packaging.
- **Re-run for an existing tag.** The same manual run with *publish* checked
  rebuilds the assets of that tag and replaces them on its release (for
  example after a runner failure).
- **Wrong release text.** Fix the notes in `README.md` on `main` and re-run the
  workflow for the tag with *publish* checked; it updates the release text.
- **Broken release.** Prefer a new patch release over replacing a published
  one. Only for a release nobody could have downloaded yet: delete it and its
  tag (`gh release delete vX.Y.Z --cleanup-tag`), fix `main`, and publish
  again.

## CI

CI runs on every push: Debug builds with the unit and Robot tests (Linux,
macOS, macOS with ASan/UBSan, Windows) and Release builds of every target.
Runs are queued per branch, so pushes during a run become one follow-up run of
the newest commit. A release should only be published from a commit whose CI
passed — `--publish` checks it.
