# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: MIT
# See LICENSES/MIT.txt for the full license text.

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from .dependency_artifacts import (
        default_cache_root,
        install_dependencies,
        load_dependency_config,
    )
except ImportError:
    from dependency_artifacts import (
        default_cache_root,
        install_dependencies,
        load_dependency_config,
    )


PROJECT_ROOT = Path(__file__).resolve().parent.parent

PLATFORMS = {
    "windows": ("windows", "win"),
    "mac": ("macos", "mac"),
    "macos": ("macos", "mac"),
}


def process_deps(
    platform_name: str, *, offline: bool = False, cache_root: Path | None = None
) -> None:
    try:
        config_platform, config_suffix = PLATFORMS[platform_name]
    except KeyError as error:
        raise ValueError(f"unsupported dependency platform: {platform_name}") from error

    config_path = PROJECT_ROOT / "config" / f"dependencies_{config_suffix}.json"
    config = load_dependency_config(config_path, expected_platform=config_platform)
    print(
        f"Processing {config.platform}-{config.architecture} dependencies "
        f"from {config_path.name}..."
    )
    install_dependencies(
        config,
        project_root=PROJECT_ROOT,
        cache_root=cache_root or default_cache_root(),
        offline=offline,
    )


def process_deps_win(*, offline: bool = False) -> None:
    process_deps("windows", offline=offline)


def process_deps_mac(*, offline: bool = False) -> None:
    process_deps("macos", offline=offline)


def current_platform() -> str:
    if sys.platform == "win32":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    raise RuntimeError(
        f"unsupported host platform {sys.platform!r}; use --platform for tests only"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Install immutable Cici build dependencies from GitHub Releases."
    )
    parser.add_argument(
        "--platform",
        choices=("windows", "macos"),
        help="dependency platform (defaults to the current host)",
    )
    parser.add_argument(
        "--offline",
        action="store_true",
        help="use only SHA-256-verified artifacts already present in the local cache",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    process_deps(args.platform or current_platform(), offline=args.offline)
    print("\n-- All configured dependencies are ready --")


if __name__ == "__main__":
    main()
