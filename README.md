# Cici Beauty OBS plugin

This repository contains the OBS plugin and UI, the OBS loader, and the MIT
`cicibase` and `protocol` libraries shared with the independent Cici runtime.
The plugin builds without the runtime's source code or the MRE SDK. It uses IPC
and shared memory to communicate with the separately distributed runtime.

## Build

Supported platforms are macOS arm64 (Xcode, macOS 15 or newer) and Windows x64
(Visual Studio 2022 with clang-cl). Install CMake 3.22 or newer, Python 3.10 or
newer. The downloader fetches pinned public GitHub Release assets over HTTPS
without GitHub CLI or a GitHub login, and verifies their SHA-256 before extraction.
Private company credentials are not part of the build.

OBS/Qt SDKs and the Windows VC++ redistributable are published in the public
[`build-dependencies-v1` release](https://github.com/cicibananapie/CiciBeauty/releases/tag/build-dependencies-v1).
The dependency descriptors pin their asset names and SHA-256 hashes. An existing
matching SDK can also be used as described below.

```sh
python3 scripts/cici.py build --pull-deps --config Debug
```

The built plugin and loader are under `build/artifacts/bin/Debug`. Compilation
does not download, build, or launch the private runtime. Use a matching Qt/OBS SDK
from `config/dependencies_mac.json` or `config/dependencies_win.json`.

For a preexisting SDK installation, pass `-DCICI_DEPENDENCIES_DIR=/path/to/depends`
when configuring CMake. That directory contains `obs-sdk`, `obsQT`, and `QTTool`.

## Assemble an installation package

Use a matching private runtime archive, containing the helpers, installer,
runtime libraries, and engine resources. The archive has a `runtime/` root and
a `runtime-manifest.json` recording its version, channel, configuration, platform,
architecture, and source commits. It must not contain the plugin or loader.

```sh
python3 scripts/cici.py package --version 1.0.0 --config Release \
  --channel-id production --pull-deps
```

This creates a non-official package without company signing credentials. On
macOS, the default is ad-hoc signing; Windows skips the signing step. `Release`
selects the compilation configuration and does not enable official packaging.
Set the example version and channel to match the selected runtime.

By default the entry point reads `config/runtime_mac.json` or
`config/runtime_win.json`. Each file pins one GitHub Release repository, tag,
asset name and SHA-256, using dependency schema version 2 and an install mapping
from `runtime` to `runtime`. The release producer publishes the runtime first,
then commits its generated descriptor to the corresponding platform file.
These two descriptors are currently unconfigured because no runtime Release has
been published yet; fill them before packaging. No placeholder release or hash
is treated as a working download. `--runtime-config` can select another pinned
descriptor. There is no local-runtime/source option in this public CLI.

For local OBS development, use `dev` with a matching binary runtime:

```sh
python3 scripts/cici.py dev --version 1.0.0 --channel-id production --pull-deps
```

`dev` creates a non-official package, stages it under the public build directory,
and deploys its loader and location pointers to the local OBS plugin directory.
By default each development package uses a separate directory under `dist/`.
Add `--ide` to configure and open the plugin project without building
or deploying packages.

The packager verifies the runtime metadata, builds the public plugin, and creates
the installer and update archive in `dist/`. Existing installation and updater
archive layouts are preserved. `--stage-dir` optionally retains the assembled
runtime for local deployment. Use a new output directory for each build.

Windows Qt platform plugins and the VC++ redistributable installer are added by
the public packager from the selected public dependency directory. They are not
part of the private runtime archive.

Runtime binary confidentiality and log checks run in the private repository.
The public packager does not run binary confidentiality scans. Any future local
inspection scripts and private rules must remain outside Git; `scripts/local_checks/`
is ignored for that purpose.

macOS disk images use `create-dmg` (`brew install create-dmg`) and retain the
direct-run installer layout.

Both `dev` and `package` generate an installer and update ZIP. The plugin's
version and channel must match the selected runtime. A Debug, non-official plugin
can use a Release, official runtime; their build configurations and signing
flags do not have to match.

Common options are `--build-dir`, `--config`, `--version`, `--channel-id`,
`--build-id`, `--pdb-output-root` and `--pull-deps`. `build` compiles only the
plugin and loader, without downloading a runtime. `package` and `dev` also accept
`--runtime-config`, `--output-dir`, `--sign-identity` and `--stage-dir`.
`build` and `package` accept `--official`; `dev` is always non-official and
additionally supports `--ide`. Run each command with `--help` for defaults.

## Official packaging

Add `--official` to `package` only when the required signing credentials and
tools are configured:

- On macOS, supply `--sign-identity` with a valid Developer ID Application
  identity. Configure a notarization keychain profile; `CICI_NOTARY_PROFILE`
  selects the profile and defaults to `notarytool-password`. Official packaging
  signs, notarizes, and staples the application and disk image.
- On Windows, configure the signing helper and its local credentials. Official
  packaging signs the plugin, loader, and final installer.

Official builds require an empty `--build-dir`; use a new build directory rather
than reusing the one from the examples above. Signing credentials are never
checked into this repository.

## Symbols and official builds

Windows linker PDBs are written directly to the permanent directory
`<drive>:/CiciSymbols/CiciBeauty/<channel>/<version>/<build-id>/windows-x64/plugin/<config>`.
The default storage root is chosen from writable fixed drives, preferring the
drive with the most free space. Removable and network drives are excluded.
Set `CICI_SYMBOL_ROOT` to change the storage root, or `--pdb-output-root` to
select the linker output directory. Official PDB paths must be absolute and
outside user profiles, and an existing configuration directory is rejected to
avoid overwriting an earlier build's symbols.

macOS generates dSYM before stripping distribution binaries and signing them.
Official compilation maps source and build paths to `/CiciSources/public` and
`/CiciBuild/public`; the symbol metadata records how those prefixes map back to
the checkout. The packager verifies each binary's UUID against its dSYM.
Symbols are retained under `CICI_SYMBOL_ROOT`, defaulting on macOS to
`~/Library/Application Support/CiciBuild/Symbols`, using the same
channel/version/build-id layout. Every symbols ZIP includes a manifest with
file paths, sizes, SHA-256, source commit and binary/symbol associations.
Official builds require an empty `--build-dir`; use a new directory for each
release build. Previous build trees and permanent symbols are preserved.

## Tests

Configure with `-DCICI_BUILD_TESTS=ON`, build the default targets, and run:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

The public tests cover the foundation library, IPC, translations, OBS bindings,
and plugin UI. OBS binding and feedback UI tests need an installed OBS runtime
in addition to the build SDK. Both use the CMake cache variable
`CICI_OBS_RUNTIME_DIR`, initialized from the environment variable of the same
name. On macOS it points to the installed OBS application's `Contents/Frameworks`
directory and defaults to `/Applications/OBS.app/Contents/Frameworks`.
On Windows it points to the installed OBS `bin/64bit` directory.
For headless macOS UI tests, `QT_STYLE_OVERRIDE=Fusion` can avoid native style
initialization. Backend business and cross-process implementation tests live
in the private runtime repository, which consumes this repository as a submodule.

The Windows format preflight script requires `-ObsRoot`. Its default `-PluginDll`
uses `build/artifacts/bin/Release`, `-InputFrame` uses the BGRA file created beside
`prepare_test_frame.ps1`, and `-WorkRoot` uses `build-format-preflight` in this
checkout. All three paths can be overridden. When using `-AttachCiciFilter`, also
pass `-DefaultSettingPath` pointing to the installed runtime's
`Resources/defaultsetting.json`.

Windows build helpers discover Visual Studio 2022 through `VSINSTALLDIR` or
`vswhere`; `CICI_VSDEVCMD` explicitly selects its `VsDevCmd.bat`. Signing discovers
`signtool` through `PATH`, `WindowsSdkDir`, or Windows SDK installation information;
`CICI_SIGNTOOL_PATH` explicitly selects the executable. An invalid explicit
override is an error. The standalone signing script accepts binary paths as
command-line arguments.

## License

The plugin and loader are GPL-2.0-or-later. Project-authored foundation and
protocol code is MIT. See `LICENSES/`, individual file notices, and
`cicibase/THIRD_PARTY_NOTICES.md`. Qt, OBS, and vendored code retain their own
licenses. Binary runtime components, artwork, and trademarks have separate terms;
the source licenses do not relicense those components.
