"""Offline tests for the project-local C# bootstrapper. No SDK downloads."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "csharp_env", Path(__file__).resolve().parents[1] / "scripts" / "csharp_env.py"
)
envtool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(envtool)


class CSharpEnvironmentTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="uvim csharp test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.config = {"sdk_version": "10.0.100", "csharp_ls_version": "0.28.0"}

    def manifest(self):
        envtool.write_json(self.root / envtool.MANIFEST, self.config)

    def test_init_scaffolds_example_and_portable_helper(self):
        with patch.object(envtool, "restore") as restore:
            envtool.init(self.root, "HelloApp", False, "latest", "0.28.0", template="lsp")
        restore.assert_called_once_with(self.root)
        self.assertIn("net10.0", (self.root / "HelloApp.csproj").read_text())
        self.assertIn("Greet(42)", (self.root / "Program.cs").read_text())
        self.assertIn("class Greeter", (self.root / "Greeter.cs").read_text())
        self.assertIn("namespace HelloApp;", (self.root / "Greeter.cs").read_text())
        self.assertEqual((self.root / envtool.HELPER).read_text(),
                         Path(envtool.__file__).read_text())
        self.assertIn("/.csharp-env/", (self.root / ".gitignore").read_text())

    def test_hello_is_default_and_can_be_generated_offline(self):
        with patch.object(envtool, "restore") as restore:
            result = envtool.main(["init", str(self.root), "--name", "HelloApp", "--no-restore"])
        self.assertEqual(result, 0)
        restore.assert_not_called()
        self.assertIn('Console.WriteLine("Hello, World!");', (self.root / "Program.cs").read_text())
        self.assertFalse((self.root / "Greeter.cs").exists())
        self.assertFalse((self.root / ".venv").exists())
        self.assertFalse((self.root / envtool.STATE).exists())
        config = envtool.read_manifest(self.root)
        self.assertEqual(config["template"], "hello")
        guide = (self.root / "CSHARP_ENV.md").read_text()
        self.assertIn("Hello, World!", guide)
        self.assertNotIn("Greet(42)", guide)
        self.assertIn("## 1.", guide)

    def test_class_library_has_no_entry_point_and_guide_uses_build(self):
        with patch.object(envtool, "restore"):
            envtool.init(self.root, "GreetingLibrary", False, "latest", "0.28.0", template="classlib")
        self.assertNotIn("<OutputType>", (self.root / "GreetingLibrary.csproj").read_text())
        self.assertFalse((self.root / "Program.cs").exists())
        self.assertIn("namespace GreetingLibrary;", (self.root / "Greeter.cs").read_text())
        self.assertEqual(envtool.read_manifest(self.root)["entry_file"], "Greeter.cs")
        guide = (self.root / "CSHARP_ENV.md").read_text()
        self.assertIn("no Main entry point", guide)
        self.assertIn("edit Greeter.cs", guide)
        self.assertNotIn("python csharp-env.py run", guide)

    def test_init_can_generate_into_current_directory(self):
        with patch.object(Path, "cwd", return_value=self.root), \
             patch.object(envtool, "restore") as restore:
            self.assertEqual(envtool.main(["init", "--name", "CurrentApp", "--no-restore"]), 0)
        self.assertTrue((self.root / "CurrentApp.csproj").is_file())
        self.assertTrue((self.root / "Program.cs").is_file())
        restore.assert_not_called()

    def test_templates_command_needs_no_environment(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(envtool.main(["templates"]), 0)
        for name in ("hello", "lsp", "classlib"):
            self.assertIn(name, output.getvalue())

    def test_edit_defaults_to_template_entry_file(self):
        self.config["entry_file"] = "Greeter.cs"
        self.manifest()
        for file in (envtool.dotnet_path(self.root), envtool.server_path(self.root)):
            file.parent.mkdir(parents=True, exist_ok=True)
            file.touch()
        with patch.object(envtool, "run") as run:
            self.assertEqual(envtool.main(["-C", str(self.root), "edit"]), 0)
        self.assertEqual(run.call_args.args[0][-1], "Greeter.cs")

    def test_init_refuses_overwrites_before_installing(self):
        source = self.root / "Program.cs"
        source.write_text("keep my program")
        with patch.object(envtool, "restore") as restore:
            with self.assertRaises(envtool.SetupError):
                envtool.init(self.root, "HelloApp", False, "latest", "0.28.0")
        restore.assert_not_called()
        self.assertEqual(source.read_text(), "keep my program")
        self.assertFalse((self.root / envtool.MANIFEST).exists())

    def test_empty_init_keeps_existing_sources_and_gitignore(self):
        (self.root / "Program.cs").write_text("existing program")
        (self.root / "App.csproj").write_text("existing project")
        (self.root / ".gitignore").write_text("private.txt\n")
        with patch.object(envtool, "restore"):
            envtool.init(self.root, "HelloApp", True, "latest", "0.28.0")
        self.assertEqual((self.root / "Program.cs").read_text(), "existing program")
        self.assertTrue((self.root / ".gitignore").read_text().startswith("private.txt\n"))

    def test_environment_is_project_local_and_does_not_mutate_parent(self):
        original = dict(os.environ)
        child = envtool.environment(self.root)
        self.assertEqual(dict(os.environ), original)
        self.assertEqual(child["DOTNET_ROOT"], str(self.root / ".csharp-env" / "sdk"))
        self.assertEqual(child["DOTNET_GENERATE_ASPNET_CERTIFICATE"], "false")
        for key in ("DOTNET_CLI_HOME", "NUGET_PACKAGES", "NUGET_HTTP_CACHE_PATH",
                    "NUGET_PLUGINS_CACHE_PATH", "DOTNET_BUNDLE_EXTRACT_BASE_DIR",
                    "MSBuildUserExtensionsPath", "TEMP", "TMPDIR"):
            self.assertTrue(Path(child[key]).is_relative_to(self.root), key)

    def test_sdk_install_commands_for_posix_and_windows(self):
        for windows in (False, True):
            with self.subTest(windows=windows):
                with patch.object(envtool, "IS_WINDOWS", windows), \
                     patch.object(envtool, "download_installer") as download, \
                     patch.object(envtool.shutil, "which", return_value="test-shell"), \
                     patch.object(envtool, "run") as run:
                    child = envtool.environment(self.root)
                    envtool.install_sdk(self.root, self.config, child)
                    command, root, passed_env = run.call_args.args
                    self.assertEqual(root, self.root)
                    self.assertEqual(passed_env, child)
                    self.assertIn("10.0.100", command)
                    self.assertIn(envtool.dotnet_path(self.root).parent, command)
                    self.assertIn("-NoPath" if windows else "--no-path", command)
                    self.assertNotIn("sudo", command)
                    download.assert_called_once()

    def test_latest_sdk_is_pinned_and_not_downloaded_again(self):
        self.config["sdk_version"] = "latest"
        self.manifest()
        dotnet = envtool.dotnet_path(self.root)
        dotnet.parent.mkdir(parents=True)
        dotnet.touch()
        result = subprocess.CompletedProcess([], 0, "10.0.99 [/sdk]\n10.0.100 [/sdk]\n")
        with patch.object(envtool, "run", return_value=result), \
             patch.object(envtool, "download_installer") as download:
            envtool.install_sdk(self.root, self.config, envtool.environment(self.root))
        download.assert_not_called()
        saved = json.loads((self.root / envtool.MANIFEST).read_text())
        self.assertEqual(saved["sdk_version"], "10.0.100")
        self.assertEqual(json.loads((self.root / "global.json").read_text())["sdk"]["version"],
                         "10.0.100")

    def test_restore_uses_local_sdk_tools_sources_and_locked_mode(self):
        self.manifest()
        (self.root / ".venv").mkdir()
        (self.root / "App.csproj").touch()
        with patch.object(envtool, "install_sdk"), patch.object(envtool, "run") as run:
            envtool.restore(self.root, locked=True)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(commands[0][:4], [envtool.dotnet_path(self.root), "tool", "install", "csharp-ls"])
        self.assertIn("--tool-path", commands[0])
        self.assertIn(self.root / ".csharp-env" / "tools", commands[0])
        self.assertIn(self.root / "NuGet.Config", commands[0])
        self.assertNotIn("--global", commands[0])
        self.assertIn("--locked-mode", commands[1])

    def test_failed_sdk_install_stops_before_lsp_install(self):
        self.manifest()
        (self.root / ".venv").mkdir()
        with patch.object(envtool, "install_sdk", side_effect=OSError("download failed")), \
             patch.object(envtool, "run") as run, contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(envtool.main(["-C", str(self.root), "restore"]), 1)
        run.assert_not_called()

    def test_edit_uses_absolute_local_server_and_environment(self):
        self.manifest()
        for file in (envtool.dotnet_path(self.root), envtool.server_path(self.root)):
            file.parent.mkdir(parents=True, exist_ok=True)
            file.touch()
        with patch.object(envtool, "run") as run:
            self.assertEqual(envtool.main([
                "-C", str(self.root), "edit", "--uvim", "/some path/uvim", "Program.cs"
            ]), 0)
        command, root, child = run.call_args.args
        self.assertEqual(command, ["/some path/uvim", "--csharp-lsp", "--csharp-lsp-path",
                                   envtool.server_path(self.root), "Program.cs"])
        self.assertEqual(child["DOTNET_ROOT"], str(envtool.dotnet_path(root).parent))

    def test_help_needs_no_environment_or_downloads(self):
        with contextlib.redirect_stdout(io.StringIO()) as output:
            with self.assertRaises(SystemExit) as error:
                envtool.main(["--help"])
        self.assertEqual(error.exception.code, 0)
        self.assertIn("restore", output.getvalue())

    def test_invalid_manifest_values_fail_before_writing(self):
        for sdk, lsp in [("8.0.100", "0.28.0"), ("bad", "0.28.0"), ("latest", "latest")]:
            with self.subTest(sdk=sdk, lsp=lsp), patch.object(envtool, "restore") as restore:
                with self.assertRaises(envtool.SetupError):
                    envtool.init(self.root, "HelloApp", False, sdk, lsp)
                restore.assert_not_called()
                self.assertEqual(list(self.root.iterdir()), [])
        for value in [[], {"sdk_version": None}, {"sdk_version": "latest", "csharp_ls_version": None}]:
            with self.assertRaises(envtool.SetupError):
                envtool.validate_manifest(value)

    def test_restore_reuses_installed_tools_without_reinstalling(self):
        self.manifest()
        (self.root / ".venv").mkdir()
        server = envtool.server_path(self.root)
        server.parent.mkdir(parents=True)
        server.touch()
        result = subprocess.CompletedProcess([], 0, "csharp-ls 0.28.0 csharp-ls\n")
        with patch.object(envtool, "install_sdk"), patch.object(envtool, "run", return_value=result) as run:
            envtool.restore(self.root)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertEqual(len(commands), 1)
        self.assertEqual(commands[0][1:3], ["tool", "list"])


if __name__ == "__main__":
    unittest.main()
