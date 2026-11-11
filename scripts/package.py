# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import json
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

from dependency_artifacts import Artifact, install_dependencies, load_dependency_config, stage_artifact
from runtime_package import validate_runtime
from symbols_utils import package_symbols_sidecar

ROOT = Path(__file__).resolve().parents[1]


def copy(source: Path, target: Path) -> None:
    if not source.exists():
        raise FileNotFoundError(source)
    target.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, target, symlinks=True, dirs_exist_ok=True)
    else:
        shutil.copy2(source, target)


def zip_tree(source: Path, target: Path) -> None:
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(source.rglob("*")):
            if file.is_file():
                archive.write(file, file.relative_to(source))


def write_windows_setup(stub: Path, payload: Path, target: Path) -> None:
    # Matches the existing closed installer's setup_payload.cpp trailer format.
    copy(stub, target)
    offset = target.stat().st_size
    with target.open("ab") as output, payload.open("rb") as source:
        shutil.copyfileobj(source, output)
        output.write(struct.pack("<24sIQQ", b"CICI_SETUP_PAYLOAD_V1".ljust(24, b"\0"),
                                 1, offset, payload.stat().st_size))


def copy_qt_platform_plugins(source_dir, target_dir, config):
    source_path = Path(source_dir)
    if not source_path.is_dir():
        raise FileNotFoundError(f"Qt platform plugin directory not found: {source_path}")

    destination = Path(target_dir) / source_path.name
    if destination.exists():
        shutil.rmtree(destination)

    ignored = []

    def ignore_non_runtime_artifacts(directory, names):
        directory_path = Path(directory)
        result = []
        for name in names:
            candidate = directory_path / name
            suffix = candidate.suffix.casefold()
            if candidate.is_file() and suffix in {".pdb", ".ilk", ".idb"}:
                result.append(name)
                continue

            if (
                config.strip().lower() != "debug"
                and candidate.is_file()
                and suffix == ".dll"
                and candidate.stem.casefold().endswith("d")
            ):
                release_peer = candidate.with_name(candidate.stem[:-1] + candidate.suffix)
                if release_peer.is_file():
                    result.append(name)
        ignored.extend(result)
        return result

    shutil.copytree(source_path, destination, ignore=ignore_non_runtime_artifacts)
    if ignored:
        print(
            "info: excluded Qt debug artifacts from {}: {}".format(
                config, ", ".join(sorted(ignored, key=str.casefold))
            )
        )
    print("info: copy Qt platform plugins: {} -> {}".format(source_path.name, target_dir))

def prepare_runtime(args, stage: Path) -> Path:
    platform = "macos" if sys.platform == "darwin" else "windows"
    if args.runtime_package:
        archive = args.runtime_package.resolve()
        artifact = Artifact("runtime", "", "", archive.name, "", "tar.gz", ())
        extracted = stage / "extracted"
        stage_artifact(artifact, archive, extracted)
        return extracted / "runtime"
    descriptor = json.loads(args.runtime_config.read_text(encoding="utf-8"))
    if not descriptor.get("artifacts", {}).get("runtime", {}).get("repository"):
        raise ValueError(f"Runtime Release is not configured: {args.runtime_config}. "
                         "Publish the runtime and pin its repository, tag, asset and SHA-256 first.")
    config = load_dependency_config(args.runtime_config, expected_platform=platform)
    if config.architecture != ("arm64" if platform == "macos" else "x64"):
        raise ValueError("Runtime dependency architecture mismatch")
    if len(config.artifacts) != 1 or tuple(t.destination.as_posix() for t in config.artifacts[0].install) != ("runtime",):
        raise ValueError("Runtime dependency config must install exactly one artifact into runtime")
    install_dependencies(config, project_root=stage)
    return stage / "depends" / "runtime"


def assemble(args, binaries: Path) -> list[Path]:
    platform = "macos" if sys.platform == "darwin" else "windows"
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    suffix = f"_{args.channel_id}" if args.channel_id else ""
    if args.channel_id and any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in args.channel_id):
        raise ValueError("channel-id must contain only ASCII letters, digits, underscores and hyphens")
    with tempfile.TemporaryDirectory(prefix="cici-package-") as temporary:
        stage = Path(temporary)
        runtime = getattr(args, "runtime_root", None)
        if runtime is None:
            runtime = prepare_runtime(args, stage)
        manifest = validate_runtime(runtime, platform=platform, version=args.version,
                                    official=args.official)
        public_binaries = stage / "public-binaries"
        names = (("cicibeauty.so", "cicibeautyStub.plugin") if platform == "macos"
                 else ("cicibeauty.dll", "cicibeautyStub.dll"))
        for name in names:
            copy(binaries / name, public_binaries / name)
        plugin_files = [public_binaries / names[0],
                        public_binaries / "cicibeautyStub.plugin/Contents/MacOS/cicibeautyStub" if platform == "macos"
                        else public_binaries / names[1]]
        if platform == "macos" and args.official:
            for file in plugin_files:
                file.chmod(file.stat().st_mode | stat.S_IWUSR)
                subprocess.run(["xcrun", "strip", "-S", str(file)], check=True)
        dependencies = ROOT / "depends"
        symbol_root = binaries
        cache_file = args.build_dir / "CMakeCache.txt"
        if platform == "windows" and cache_file.is_file():
            for line in cache_file.read_text(encoding="utf-8").splitlines():
                if line.startswith("CICI_DEPENDENCIES_DIR:"):
                    dependencies = Path(line.split("=", 1)[1])
                if line.startswith("CICI_PDB_OUTPUT_ROOT:"):
                    symbol_root = Path(line.split("=", 1)[1]) / args.config
        identity = f"-{args.build_id}" if args.build_id else ""
        commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", "HEAD"],
                                capture_output=True, text=True)
        plugin_metadata = {"version": args.version, "channel": args.channel_id, "build_id": args.build_id,
                           "official_build": args.official,
                           "plugin_commit": commit.stdout.strip() if commit.returncode == 0 else "",
                           "source_prefix_maps": {"/CiciSources/public": str(ROOT),
                                                  "/CiciBuild/public": str(args.build_dir.resolve())}}
        package_symbols_sidecar(args.build_dir, args.config,
                                output / f"plugin-symbols-{args.version}{suffix}{identity}.zip",
                                require_symbols=True if args.official else None,
                                symbol_root=symbol_root,
                                binary_files=plugin_files,
                                archive_dir=(getattr(args, "symbol_archive_dir", None) / args.config
                                             if getattr(args, "symbol_archive_dir", None) else None),
                                build_metadata=plugin_metadata)
        products = []
        if platform == "macos":
            sys.path.insert(0, str(ROOT / "scripts" / "mac"))
            from codesign import codesign_bundle, codesign_file, verify_codesign
            from dittozip import create_update_zip
            from dmg import create_direct_run_dmg
            app = stage / "ciciinstaller.app"
            copy(runtime / app.name, app)
            contents = app / "Contents"
            copy(public_binaries / "cicibeauty.so", contents / "Frameworks" / "cicibeauty.so")
            copy(public_binaries / "cicibeautyStub.plugin", contents / "Helpers" / "cicibeautyStub.plugin")
            copy(ROOT / "cicibeauty" / "Resources", contents / "Resources")
            copy(ROOT / "LICENSES", contents / "Resources" / "LICENSES")
            copy(ROOT / "cicibase/THIRD_PARTY_NOTICES.md", contents / "Resources" / "THIRD_PARTY_NOTICES.md")
            if args.official and not args.sign_identity:
                raise ValueError("Official macOS packaging requires --sign-identity")
            codesign_bundle(str(app), args.sign_identity or "-",
                            hardened_runtime=args.official, timestamp=args.official)
            if args.official:
                from notarize import notarize_file
                from staple import staple_app
                notarization_zip = stage / "notarize.zip"
                subprocess.run(["ditto", "-c", "-k", "--keepParent", str(app), str(notarization_zip)], check=True)
                if not notarize_file(str(notarization_zip)) or not staple_app(str(app)):
                    raise RuntimeError("Installer notarization or stapling failed")
            if not verify_codesign(str(app)):
                raise RuntimeError("Assembled installer signature verification failed")
            update = output / f"ciciinstaller_{args.version}{suffix}.zip"
            if not create_update_zip(str(app), str(update)):
                raise RuntimeError("Cannot create update archive")
            dmg = output / f"ciciinstaller_{args.version}{suffix}.dmg"
            create_direct_run_dmg(app, dmg)
            if args.official:
                codesign_file(str(dmg), args.sign_identity)
                if not notarize_file(str(dmg)) or not staple_app(str(dmg)):
                    raise RuntimeError("Disk image notarization or stapling failed")
            products = [update, dmg]
            if args.stage_dir:
                copy(app, args.stage_dir.resolve() / app.name)
        else:
            payload = stage / "payload"
            for name in ("slot", "root"):
                copy(runtime / name, payload / name)
            copy(dependencies / "vcrt/VC_redist.x64.exe", payload / "redist/VC_redist.x64.exe")
            slot = payload / "slot"
            copy_qt_platform_plugins(dependencies / "obsQT/plugins/platforms", slot, args.config)
            for name in ("cicibeauty.dll", "cicibeautyStub.dll"):
                copy(public_binaries / name, slot / name)
            copy(ROOT / "cicibeauty" / "Resources", slot / "Resources")
            copy(ROOT / "LICENSES", slot / "Resources" / "LICENSES")
            copy(ROOT / "cicibase/THIRD_PARTY_NOTICES.md", slot / "Resources" / "THIRD_PARTY_NOTICES.md")
            if args.official:
                sys.path.insert(0, str(ROOT / "scripts" / "windows"))
                from codesign import batch_sign_files, sign_single_file
                batch_sign_files([str(slot / name) for name in ("cicibeauty.dll", "cicibeautyStub.dll")])
            update = output / f"cicibeauty{args.version}{suffix}.zip"
            zip_tree(slot, update)
            payload_zip = stage / "payload.zip"
            zip_tree(payload, payload_zip)
            setup = output / f"CiciBeauty_Setup{args.version}{suffix}.exe"
            write_windows_setup(runtime / "installer" / "ciciinstaller.exe", payload_zip, setup)
            if args.official:
                sign_single_file(str(setup))
            products = [update, setup]
            if args.stage_dir:
                copy(slot, args.stage_dir.resolve())
                copy(payload / "root", args.stage_dir.resolve().parent)
        commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", "HEAD"],
                                capture_output=True, text=True)
        (output / f"build-manifest-{args.version}{suffix}.json").write_text(
            json.dumps({"runtime": manifest, "plugin_version": args.version,
                        "plugin_commit": commit.stdout.strip() if commit.returncode == 0 else "",
                        "build_id": args.build_id,
                        "official_build": args.official}, indent=2) + "\n", encoding="utf-8")
        for product in products:
            print(product)
        return products
