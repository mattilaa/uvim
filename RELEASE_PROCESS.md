# Release process

How uVim versions are numbered, how a release is prepared and published, and
what to do when something goes wrong. The steps are automated by
[`scripts/release.sh`](scripts/release.sh); every step that changes something
asks first (`-y` answers yes).

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
