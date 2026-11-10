# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


@dataclass(frozen=True)
class CMakeBuildRequest:
    project_root: Path
    build_dir: Path
    config: str
    version: str
    channel_id: str
    official: bool
    output_root: Path | None = None
    pdb_output_root: Path | None = None

    def configure_command(
        self,
        generator_arguments: Sequence[str],
        extra_definitions: Sequence[str] = (),
    ) -> list[str]:
        command = [
            "cmake",
            "-S",
            str(self.project_root),
            "-B",
            str(self.build_dir),
            *generator_arguments,
            f"-DCICI_VERSION={self.version}",
            f"-DCICI_CHANNEL_ID={self.channel_id}",
            f"-DOFFICIAL_BUILD={'ON' if self.official else 'OFF'}",
            *extra_definitions,
        ]
        if self.output_root is not None:
            command.append(f"-DCICI_OUTPUT_ROOT={_cmake_path(self.output_root)}")
        if self.pdb_output_root is not None:
            command.append(
                f"-DCICI_PDB_OUTPUT_ROOT={_cmake_path(self.pdb_output_root)}"
            )
        return command

    def build_command(self, targets: Sequence[str] = ()) -> list[str]:
        command = [
            "cmake",
            "--build",
            str(self.build_dir),
            "--config",
            self.config,
            "--parallel",
        ]
        if targets:
            command.extend(("--target", *targets))
        return command


def _cmake_path(path: Path) -> str:
    return os.fspath(path).replace("\\", "/")


def run_streaming_shell(command: str) -> None:
    process = subprocess.Popen(
        command,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        bufsize=1,
    )
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="", flush=True)
    process.wait()
    if process.returncode != 0:
        raise subprocess.CalledProcessError(process.returncode, command)




def find_vs_environment(explicit: Path | None = None) -> Path:
    configured = explicit or os.environ.get("CICI_VSDEVCMD")
    if configured:
        candidate = Path(configured)
        if not candidate.is_file():
            raise FileNotFoundError(f"Visual Studio environment script not found: {candidate}")
        return candidate

    installation = os.environ.get("VSINSTALLDIR")
    if installation:
        candidate = Path(installation) / "Common7/Tools/VsDevCmd.bat"
        if candidate.is_file():
            return candidate

    locator = shutil.which("vswhere.exe") or shutil.which("vswhere")
    if not locator:
        program_files = os.environ.get("ProgramFiles(x86)")
        if program_files:
            candidate = Path(program_files) / "Microsoft Visual Studio/Installer/vswhere.exe"
            if candidate.is_file():
                locator = str(candidate)
    if locator:
        result = subprocess.run([
            locator, "-latest", "-products", "*", "-version", "[17.0,18.0)",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ], check=True, capture_output=True, text=True)
        installation = result.stdout.strip()
        if installation:
            candidate = Path(installation) / "Common7/Tools/VsDevCmd.bat"
            if candidate.is_file():
                return candidate
    raise FileNotFoundError(
        "Visual Studio 2022 was not found. Install its C++ build tools, "
        "run from a Developer Command Prompt, or set CICI_VSDEVCMD."
    )




def write_deployment_pointer(path: Path, value: str) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(value, encoding="utf-8")


def replace_deployment_artifact(source: Path, destination: Path) -> None:
    source = Path(source)
    if not source.exists():
        raise FileNotFoundError(f"deployment artifact not found: {source}")
    destination = Path(destination)
    if destination.exists():
        if destination.is_dir() and not destination.is_symlink():
            shutil.rmtree(destination)
        else:
            destination.unlink()
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination, symlinks=True)
    else:
        shutil.copy2(source, destination)


def deploy_local(binary_dir: Path) -> None:
    binary_dir = Path(binary_dir).resolve()
    if os.name == "nt":
        root = Path(os.environ["ProgramData"]) / "obs-studio/plugins/cicibeautyStub/bin/64bit"
        replace_deployment_artifact(binary_dir / "cicibeautyStub.dll", root / "cicibeautyStub.dll")
        write_deployment_pointer(root / "cicibeautyconfig", str(binary_dir.parent) + "/")
        write_deployment_pointer(binary_dir.parent / "config", binary_dir.name)
    else:
        root = Path.home() / "Library/Application Support/obs-studio/plugins"
        replace_deployment_artifact(
            binary_dir / "ciciinstaller.app/Contents/Helpers/cicibeautyStub.plugin",
            root / "cicibeautyStub.plugin",
        )
        write_deployment_pointer(root / "cicibeautyconfig", str(binary_dir))
        write_deployment_pointer(binary_dir / "config", "ciciinstaller.app")
