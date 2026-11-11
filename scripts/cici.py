#!/usr/bin/env python3
# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

"""Build the plugin and assemble the complete product with a binary runtime."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from build_common import CMakeBuildRequest, deploy_local, find_vs_environment, run_streaming_shell
from symbols_utils import default_symbol_storage_root, validate_pdb_output_root

ROOT = Path(__file__).resolve().parents[1]


def build(args) -> Path:
    directory = args.build_dir.resolve()
    request = CMakeBuildRequest(ROOT, directory, args.config, args.version,
                                args.channel_id, args.official,
                                pdb_output_root=args.pdb_output_root)
    generator = ()
    if not (directory / "CMakeCache.txt").exists():
        generator = (("-G", "Xcode", "-DCMAKE_OSX_ARCHITECTURES=arm64") if sys.platform == "darwin"
                     else ("-G", "Visual Studio 17 2022", "-A", "x64", "-T", "ClangCL"))
    commands = [request.configure_command(generator)]
    if not args.ide:
        commands.append(request.build_command(("cicibeauty", "cicibeautyStub")))
    if sys.platform == "win32":
        environment = find_vs_environment()
        run_streaming_shell(f'"{environment}" && ' + " && ".join(subprocess.list2cmdline(command) for command in commands))
    else:
        for command in commands:
            subprocess.run(command, check=True)
    return directory / "artifacts" / "bin" / args.config


def create_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("build", "package", "dev"):
        command = commands.add_parser(name)
        command.add_argument("--build-dir", type=Path, default=ROOT / "build")
        command.add_argument("--config", choices=("Debug", "Release", "RelWithDebInfo"), default="Debug")
        command.add_argument("--version", default="0.0.0" if name == "build" else None,
                             help="plugin version; defaults to the selected runtime version when packaging")
        command.add_argument("--channel-id", default="debug_channel" if name == "dev" else "")
        if name != "dev":
            command.add_argument("--official", action="store_true")
            command.set_defaults(ide=False)
        else:
            command.set_defaults(official=False)
            command.add_argument("--ide", action="store_true", help="configure and open the plugin IDE project without building")
        command.add_argument("--build-id", default="")
        command.add_argument("--pdb-output-root", type=Path, help="permanent linker PDB directory on Windows")
        command.add_argument("--pull-deps", action="store_true")
        if name != "build":
            command.add_argument("--runtime-config", type=Path,
                                 default=ROOT / "config" / ("runtime_mac.json" if sys.platform == "darwin" else "runtime_win.json"),
                                 help="pinned GitHub Release dependency JSON (defaults to config/runtime_<platform>.json)")
            command.set_defaults(runtime_package=None)
            command.add_argument("--output-dir", type=Path)
            command.add_argument("--sign-identity", default=os.environ.get("CICI_SIGN_IDENTITY", ""))
            command.add_argument("--stage-dir", type=Path, help="optional assembled runtime for local deployment")
    return parser


def main(argv: list[str] | None = None) -> None:
    parser = create_parser()
    args = parser.parse_args(argv)
    if sys.platform not in ("darwin", "win32"):
        parser.error("Only macOS and Windows are supported")
    if args.version is not None and not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        parser.error("version must have the form X.Y.Z")
    if not re.fullmatch(r"[A-Za-z0-9_-]*", args.channel_id):
        parser.error("channel-id must contain only ASCII letters, digits, underscores and hyphens")
    if args.build_id and not re.fullmatch(r"[A-Za-z0-9_-]+", args.build_id):
        parser.error("build-id must contain only ASCII letters, digits, underscores and hyphens")
    run(args)


def run(args) -> None:
    """Run a prepared build request; internal source integration supplies its runtime here."""
    if args.command != "build" and not args.ide:
        from package import prepare_runtime
        from runtime_package import validate_runtime
        with tempfile.TemporaryDirectory(prefix="cici-runtime-") as temporary:
            args.runtime_root = prepare_runtime(args, Path(temporary))
            manifest = validate_runtime(args.runtime_root,
                                        platform="macos" if sys.platform == "darwin" else "windows",
                                        version=args.version, official=args.official)
            if args.version is None:
                args.version = manifest["version"]
            run_prepared(args)
    else:
        if args.version is None:
            args.version = "0.0.0"
        run_prepared(args)


def validate_official_source(version: str) -> None:
    tag = f"{'macos' if sys.platform == 'darwin' else 'windows'}_{version}"

    def git(*arguments):
        return subprocess.run(["git", "-C", str(ROOT), *arguments],
                              check=True, capture_output=True, text=True).stdout.strip()

    try:
        tagged_commit = git("rev-parse", f"refs/tags/{tag}^{{commit}}")
    except subprocess.CalledProcessError as error:
        raise ValueError(f"Plugin tag {tag!r} does not exist") from error
    if git("rev-parse", "HEAD") != tagged_commit:
        raise ValueError(f"Plugin HEAD does not match required tag {tag!r}")
    if git("status", "--porcelain", "--untracked-files=all"):
        raise ValueError("Official plugin builds require a clean worktree")


def run_prepared(args) -> None:
    if not args.build_id:
        args.build_id = f"{'release' if args.official else 'dev'}-{time.time_ns()}"
    args.symbol_archive_dir = (default_symbol_storage_root() / "CiciBeauty" / (args.channel_id or "default")
                               / args.version / args.build_id
                               / ("macos-arm64" if sys.platform == "darwin" else "windows-x64") / "plugin")
    if sys.platform == "win32" and args.pdb_output_root is None:
        args.pdb_output_root = args.symbol_archive_dir
    if args.official and not args.ide:
        if args.build_dir.exists() and any(args.build_dir.iterdir()):
            raise ValueError("Official builds require an empty --build-dir; preserve earlier builds and symbols.")
        validate_official_source(args.version)
        if sys.platform == "win32":
            validate_pdb_output_root(args.pdb_output_root)
            (args.pdb_output_root / args.config).mkdir(parents=True, exist_ok=False)
        else:
            args.symbol_archive_dir.mkdir(parents=True, exist_ok=False)
    if args.command != "build":
        if args.output_dir is None:
            args.output_dir = ROOT / "dist"
            if args.command == "dev":
                args.output_dir /= f"local-{time.time_ns()}"
        if args.command == "dev" and args.stage_dir is None:
            args.stage_dir = args.build_dir.resolve() / "artifacts/bin" / args.config
    if args.pull_deps:
        from pull_depends import process_deps
        process_deps("macos" if sys.platform == "darwin" else "windows")
    binaries = build(args)
    if args.ide:
        project = args.build_dir.resolve() / ("cicibeauty.xcodeproj" if sys.platform == "darwin" else "cicibeauty.sln")
        if sys.platform == "darwin":
            subprocess.run(["open", str(project)], check=True)
        else:
            os.startfile(project)
        return
    if args.command != "build":
        from package import assemble
        assemble(args, binaries)
        if args.command == "dev":
            deploy_local(args.stage_dir)


if __name__ == "__main__":
    main()
