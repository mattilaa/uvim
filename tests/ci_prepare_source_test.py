"""Exercise CI rebasing against local Git repositories, without GitHub access."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "ci_prepare_source.sh"


class CiPrepareSourceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="uvim_ci_source_")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.remote = self.root / "origin.git"
        self.author = self.root / "author"
        self.checkout = self.root / "checkout"
        self.bundle = self.root / "source.bundle"
        self.env = {
            **os.environ,
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": os.devnull,
            "GITHUB_EVENT_NAME": "push",
            "GITHUB_REF": "refs/heads/feature",
            "GITHUB_STEP_SUMMARY": str(self.root / "summary.md"),
        }
        self.env.pop("CI_REBASE_BRANCHES", None)
        self.git(self.root, "init", "--bare", str(self.remote))
        self.git(self.remote, "symbolic-ref", "HEAD", "refs/heads/main")
        self.git(self.root, "init", "-b", "main", str(self.author))
        self.git(self.author, "config", "user.name", "CI test")
        self.git(self.author, "config", "user.email", "ci@example.invalid")
        self.git(self.author, "config", "commit.gpgsign", "false")
        self.commit("shared.txt", "base\n")
        self.git(self.author, "remote", "add", "origin", str(self.remote))
        self.git(self.author, "push", "origin", "main")
        self.git(self.author, "switch", "-c", "feature")
        self.commit("feature.txt", "branch change\n")
        self.git(self.author, "push", "origin", "feature")
        self.git(self.root, "clone", "--branch", "feature", str(self.remote),
                 str(self.checkout))
        self.original = self.git(self.checkout, "rev-parse", "HEAD")
        # Advance main after the runner's initial checkout: queued work must
        # fetch this update when preparation starts.
        self.git(self.author, "switch", "main")
        self.commit("main.txt", "new main change\n")
        self.git(self.author, "push", "origin", "main")
        self.main = self.git(self.author, "rev-parse", "HEAD")

    def git(self, cwd, *args):
        result = subprocess.run(["git", *args], cwd=cwd, env=self.env,
                                text=True, capture_output=True, check=True)
        return result.stdout.strip()

    def commit(self, filename, text):
        (self.author / filename).write_text(text)
        self.git(self.author, "add", filename)
        self.git(self.author, "commit", "-m", f"Update {filename}")

    def prepare(self, **env):
        return subprocess.run(["bash", str(SCRIPT), str(self.bundle)],
                              cwd=self.checkout, env={**self.env, **env},
                              text=True, capture_output=True)

    def test_default_rebases_onto_fresh_main_and_bundle_pins_all_jobs(self):
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        prepared = self.git(self.checkout, "rev-parse", "HEAD")
        self.assertNotEqual(prepared, self.original)
        self.assertEqual(self.git(self.checkout, "rev-parse", "HEAD^"), self.main)
        self.assertTrue((self.checkout / "main.txt").exists())
        self.assertTrue((self.checkout / "feature.txt").exists())
        # Neither the developer's branch ref nor its remote is rewritten.
        self.assertEqual(self.git(self.checkout, "rev-parse", "feature"), self.original)
        self.assertEqual(self.git(self.remote, "rev-parse", "feature"), self.original)
        self.assertIn(self.main, (self.root / "summary.md").read_text())

        # Main can change after preparation; downstream jobs still use exactly
        # the prepared commit, including its tracked deletions.
        self.commit("later.txt", "later main\n")
        self.git(self.author, "push", "origin", "main")
        consumer = self.root / "consumer"
        self.git(self.root, "clone", "--depth", "1", "--branch", "main",
                 self.remote.as_uri(), str(consumer))
        self.git(consumer, "fetch", "--no-tags", str(self.bundle), "HEAD")
        self.git(consumer, "checkout", "--detach", "FETCH_HEAD")
        self.assertEqual(self.git(consumer, "rev-parse", "HEAD"), prepared)
        self.assertFalse((consumer / "later.txt").exists())

    def test_false_uses_original_commit(self):
        result = self.prepare(CI_REBASE_BRANCHES="false")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.git(self.checkout, "rev-parse", "HEAD"), self.original)
        self.assertFalse((self.checkout / "main.txt").exists())

    def test_branch_merge_history_is_preserved(self):
        self.git(self.author, "switch", "feature")
        self.git(self.author, "switch", "-c", "side")
        self.commit("side.txt", "side change\n")
        self.git(self.author, "switch", "feature")
        self.commit("other.txt", "other change\n")
        self.git(self.author, "merge", "--no-ff", "side", "-m", "Merge side")
        self.git(self.author, "push", "origin", "feature")
        self.git(self.checkout, "pull", "--ff-only", "origin", "feature")
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        parents = self.git(self.checkout, "log", "-1", "--format=%P").split()
        self.assertEqual(len(parents), 2)
        self.git(self.checkout, "merge-base", "--is-ancestor", self.main, "HEAD")
        self.assertTrue((self.checkout / "side.txt").exists())
        self.assertTrue((self.checkout / "other.txt").exists())

    def test_main_and_pull_request_checkouts_are_preserved(self):
        for event, ref in [("push", "refs/heads/main"),
                           ("pull_request", "refs/pull/1/merge")]:
            with self.subTest(event=event, ref=ref):
                self.bundle.unlink(missing_ok=True)
                result = self.prepare(GITHUB_EVENT_NAME=event, GITHUB_REF=ref)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.git(self.checkout, "rev-parse", "HEAD"), self.original)

    def test_conflict_fails_without_publishing_a_bundle(self):
        self.git(self.author, "switch", "feature")
        self.commit("shared.txt", "feature version\n")
        self.git(self.author, "push", "origin", "feature")
        self.git(self.checkout, "pull", "--ff-only", "origin", "feature")
        original = self.git(self.checkout, "rev-parse", "HEAD")
        self.git(self.author, "switch", "main")
        self.commit("shared.txt", "main version\n")
        self.git(self.author, "push", "origin", "main")
        result = self.prepare()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("::error::Cannot rebase", result.stdout)
        self.assertFalse(self.bundle.exists())
        self.assertEqual(self.git(self.checkout, "rev-parse", "HEAD"), original)
        self.assertEqual(self.git(self.checkout, "status", "--porcelain"), "")
        self.assertEqual(self.git(self.remote, "rev-parse", "feature"), original)

    def test_invalid_option_fails(self):
        result = self.prepare(CI_REBASE_BRANCHES="invalid")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be true or false", result.stderr)
        self.assertFalse(self.bundle.exists())


if __name__ == "__main__":
    unittest.main()
