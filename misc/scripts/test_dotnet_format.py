"""Regression checks for project selection, include arguments, and failed formatters."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import dotnet_format


class FormatterTests(unittest.TestCase):
    def test_explicit_project_and_individual_include_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Game.csproj").write_text('<Project Sdk="Microsoft.NET.Sdk"/>')
            (root / "Game.sln").write_text("")
            command = dotnet_format.project_commands(["One File.cs", "Two.cs"], root)[0]
            self.assertEqual(command[2], str(root / "Game.csproj"))
            self.assertEqual(command[3:], ["--include", str(root / "One File.cs"), str(root / "Two.cs")])

    def test_nested_project_and_sample_without_generated_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Parent.csproj").write_text("<Project/>")
            (root / "sample").mkdir()
            (root / "sample/Game.csproj").write_text('<Project Sdk="Godot.NET.Sdk/4.8.0"/>')
            command = dotnet_format.project_commands(["sample/Player.cs"], root)[0]
            self.assertEqual(command[:5], ["dotnet", "format", "whitespace", str(root / "sample"), "--folder"])

    def test_formatter_failure_is_returned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "Game.csproj").write_text("<Project/>")
            with (
                patch.object(dotnet_format.Path, "cwd", return_value=root),
                patch.object(dotnet_format.subprocess, "run", return_value=Mock(returncode=7)),
            ):
                self.assertEqual(dotnet_format.main(["Player.cs"]), 1)

    def test_standalone_installed_helper_is_formatted_without_a_project(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            command = dotnet_format.project_commands(["csharp/NetApi.cs", "csharp/Other Helper.cs"], root)[0]
            self.assertEqual(
                command[:6], ["dotnet", "format", "whitespace", str(root / "csharp"), "--folder", "--include"]
            )
            self.assertEqual(command[6:], [str(root / "csharp/NetApi.cs"), str(root / "csharp/Other Helper.cs")])


if __name__ == "__main__":
    unittest.main()
