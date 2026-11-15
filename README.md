# CiciBeauty

CiciBeauty is a real-time image and video processing plugin for OBS Studio. It
provides skin enhancement, face and body shaping, makeup, filters, background
keying, and animated effects to help streamers and content creators adjust their
live and recorded video.

CiciBeauty uses a cross-process architecture. The OBS plugin handles OBS
integration, the settings interface, and requests for media processing. A
separate **luma** process performs image and video processing. The two programs
exchange configuration, media data, and results through a public protocol.

This repository contains the source code for the OBS plugin, plugin loader,
shared foundation library, communication protocol, and build tools.

| Component | License |
| --- | --- |
| OBS plugin, settings interface, and plugin loader | GNU GPL version 2 or later, `GPL-2.0-or-later` |
| Project-authored `cicibase`, `protocol`, and tools marked MIT | MIT |
| Third-party code and dependencies | Their respective licenses |

luma is proprietary software distributed separately. This repository's source
licenses do not apply to the luma binaries, separately licensed effect assets,
models, or trademarks. See [OPEN_SOURCE_LICENSES.md](OPEN_SOURCE_LICENSES.md),
[LICENSES](LICENSES/), and individual file notices for the license scope.

## Future direction

CiciBeauty aims to make the same image and video processing capabilities
available through different interfaces. The OBS plugin is one client. Planned
interfaces include a luma command-line mode and a standalone editing application
called lumaUI.

### luma command-line mode: planned

As AI agents become part of creative workflows, image and video processing
tools need to support both human users and automated callers. luma's planned
command-line mode is intended to be friendly to AI agents, allowing them to
invoke and control media processing as a tool within content creation and
editing workflows.

The plan is for luma to enter an interactive command-line mode when launched
without arguments. A user or AI agent would submit images, videos, and processing
instructions through the command line, and luma would perform the requested
operations and produce results without requiring OBS or a graphical interface.

This mode is intended for:

- **Terminal users:** process images and videos directly through commands.
- **Scripts and automation tools:** integrate media processing into existing
  workflows.
- **AI agents:** translate user intent into processing operations, submit them,
  and retrieve the results.

The CLI design will focus on explicit commands and parameters, machine-readable
results, and actionable error feedback so that AI agents can invoke it reliably.
These are planned design goals; the actual interface will be documented when it
is released.

### lumaUI: planned

**lumaUI** is a planned standalone application for editing and creatively
reworking images and videos.

lumaUI and the OBS plugin are intended to be parallel clients, each running in
its own process:

- The OBS plugin serves real-time processing during streaming and recording.
- lumaUI serves image and video file editing and creative reworking.
- luma executes media processing for the different interfaces.

The CLI mode and lumaUI described above are future plans. They are not available
features of the current release.

## Cross-process architecture and license boundaries

### Responsibilities

The current OBS integration consists of two cooperating processes:

| Process | Main responsibilities |
| --- | --- |
| CiciBeauty plugin inside OBS | OBS source and filter integration, settings UI, user interactions, input frame submission, and result retrieval |
| Separate luma process | Configuration, asset management, image and video processing, preview generation, and processing status |

The plugin and luma are built separately. Compiling the plugin does not require
luma's source code or linking against its media processing libraries. luma's
media processing implementation does not depend on the OBS API.

The plugin starts luma when needed and establishes a local IPC connection. On
Windows, a separate `cicirunner.exe` process loads luma. On macOS, luma runs as a
separate executable. On both platforms, luma executes outside the OBS process.

### Control messages: JSON protocol

The plugin sends JSON messages to luma for operations such as:

- Initializing sessions and creating processing instances.
- Retrieving asset catalogs and configuration.
- Changing processing parameters and applying configuration.
- Opening previews and requesting color picking.
- Retrieving results and receiving necessary status notifications.

Interactions include messages that require no reply, asynchronous requests and
responses, and necessary notifications initiated by luma. Operations that return
a result use asynchronous requests on the plugin side, avoiding UI blocking
while waiting for luma.

JSON describes the processing operations and data agreed upon by both programs.
Messages travel through a local shared-memory IPC channel. The protocol
definitions are public in [protocol](protocol/).

### Video data: shared-memory frame queues

Real-time video frames travel through separate shared-memory queues:

1. The plugin writes an input frame and its media metadata to the input queue.
2. luma reads the frame and processes it.
3. luma writes the processed frame to the output queue.
4. The plugin reads the result and returns it to the OBS video pipeline.

The frame protocol carries information such as resolution, pixel format, color
metadata, timestamps, and pixel data. It does not transmit OBS object pointers
or require luma to access OBS's internal objects.

Explicit read and write ownership prevents a producer from overwriting a buffer
that a consumer is still using. When processing cannot keep up, the plugin can
reuse the last available result or skip submitting the current input frame.

### UI previews: shared GPU textures

Settings previews use GPU textures shared across processes:

- luma creates and writes the preview textures.
- The plugin accesses them through the platform's shared resource mechanism.
- JSON messages exchange preview configuration and shared resource identifiers.
- A shared-memory queue carries texture slot identifiers and read/write
  synchronization information.

This preview queue does not carry pixel data. The image resides in the shared
GPU resource, while the queue coordinates when it may be read or written again.

Video frame queues and preview texture channels serve different purposes: OBS
video processing and settings UI display, respectively.

### Shared code and protocol

Project-authored code shared by the two programs is provided in two public
directories:

- [cicibase](cicibase/): process and filesystem helpers, scheduling,
  synchronization, shared memory, logging, JSON utilities, and other foundation
  functionality.
- [protocol](protocol/): communication formats for control messages, video
  frames, and preview data.

This project-authored shared code is licensed under MIT, permitting its use in
both open-source and proprietary software subject to that license's conditions.

### Relationship to the GPL

The project's design follows the GNU GPL FAQ's discussion of communication
between separate programs, considering both the communication mechanism and
the meaning of the information exchanged.

The FAQ explains that pipes, sockets, and command-line arguments are mechanisms
normally used between separate programs. It also explains that sufficiently
intimate communication, such as exchanging complex internal data structures,
can be a basis for treating two parts as a combined program. See:

[GNU GPL FAQ: Mere aggregation versus combining modules into one program](https://www.gnu.org/licenses/old-licenses/gpl-2.0-faq.en.html#MereAggregation)

The concrete design boundaries in this project are:

- The plugin and luma run in separate processes and address spaces.
- The plugin builds independently and does not link against luma's media
  processing implementation.
- A public protocol carries processing configuration, media frames, preview
  resource identifiers, and results.
- Communication does not transmit OBS internal objects, process-local object
  pointers, or private processing objects.
- Shared memory transports messages and media data and coordinates buffer
  access; shared GPU resources provide preview images.
- The plugin and loader source code, build scripts, and license notices are
  publicly available in this repository.

These are the implemented facts used to explain the architecture and license
boundaries. The planned CLI and lumaUI will expand luma's independent uses;
descriptions of the current version's boundaries rely on the design already
implemented.

Rights to use, modify, and redistribute the plugin and other GPL-covered code
are governed by their applicable licenses. Third-party dependencies retain their
own terms, which must also be satisfied when building and distributing binaries.
See [OPEN_SOURCE_LICENSES.md](OPEN_SOURCE_LICENSES.md) for details.

## Building and usage

### Supported platforms

| Platform | Architecture | Development tools |
| --- | --- | --- |
| macOS 15 or later | arm64 | Xcode |
| Windows | x64 | Visual Studio 2022, Desktop development with C++, Windows SDK, and clang-cl |

Additional requirements:

- Git.
- CMake 3.22 or later.
- Python 3.10 or later.
- On macOS, `create-dmg` is required to create installation disk images. Install
  it with `brew install create-dmg`.

The examples use `python3`. On Windows, substitute `python` if that is the command
provided by your Python installation.

### Get the source

```sh
git clone https://github.com/cicibananapie/CiciBeauty.git
cd CiciBeauty
```

### Build the plugin only

```sh
python3 scripts/cici.py build --pull-deps --config Debug
```

This command prepares the public build dependencies and compiles the plugin and
loader. The default output directory is:

```text
build/artifacts/bin/Debug
```

`build` does not download or launch luma, and does not produce a complete
installer. Running the full functionality requires a separate environment that
includes luma, or the `dev` or `package` workflow below.

The OBS SDK, Qt SDK, and Windows VC++ Redistributable are available in the public
[`build-dependencies-v1` release](https://github.com/cicibananapie/CiciBeauty/releases/tag/build-dependencies-v1).
The downloader uses HTTPS directly, requires neither GitHub CLI nor a GitHub
login, and verifies SHA-256 before extraction.

Dependency versions and hashes are recorded in:

```text
config/dependencies_mac.json
config/dependencies_win.json
```

### luma binary runtime package

A complete runtime environment is assembled from the public plugin source and
a separately published luma binary runtime package. Building the installer does
not require access to luma's source code.

The public build entry point downloads a pinned GitHub Release asset using:

```text
config/runtime_mac.json
config/runtime_win.json
```

Each descriptor pins the repository, release tag, asset name, and SHA-256. A
manifest inside the runtime package records its version, platform, architecture,
and other metadata, which are checked before assembly.

**These two runtime descriptors are currently unconfigured. The `package` and
`dev` workflows below can complete once the luma binaries are published and the
descriptors are updated. Plugin-only `build` is unaffected.**

Use `--runtime-config` to select another valid runtime descriptor.

### Create a non-official installation package

Once the runtime descriptor is configured, run:

```sh
python3 scripts/cici.py package --pull-deps --config Release
```

This command downloads and verifies the runtime package, compiles the plugin and
loader, and generates an installer and an update ZIP. The default output
directory is `dist/`.

This is a non-official packaging workflow and requires no company signing
certificate:

- macOS uses ad-hoc signing by default.
- Windows skips signing by default.

`Release` selects the compilation configuration; it does not enable official
release mode.

### Local development and deployment

```sh
python3 scripts/cici.py dev --pull-deps --config Debug --ide
```

This command builds and packages the software, deploys it to the local OBS
installation, and opens the plugin's IDE project after those steps succeed.

`dev` uses a published luma binary runtime package and is always non-official.
Use it to modify the plugin or settings UI and verify the result in local OBS.

### Versions and channels

When packaging, the plugin version defaults to the version of the selected luma
runtime package. Non-official builds may select a different plugin version with
`--version`.

The channel, selected with `--channel-id`, is a plugin distribution attribute.
The same luma runtime package may serve plugins distributed through different
channels.

At startup, the plugin sends its version, official-build flag, and channel to
luma. Version or official-build differences are logged; development builds
allow these combinations to continue running.

### Official releases

Official packaging uses `package --official` and requires a configured release
signing environment:

- macOS requires a valid Developer ID Application signing identity and
  notarization configuration.
- Windows requires the signing tools and their local credentials.

Official plugin builds require a clean worktree and HEAD at the tag for the
corresponding platform and version:

```text
macos_<version>
windows_<version>
```

Official packaging requires matching plugin and runtime versions and a new,
empty build directory. Official builds also apply source path mappings and
archive debugging symbols: PDB on Windows and dSYM on macOS, with checks that
the symbols correspond to the binaries.

Signing credentials are not included in this repository.

### Common options

| Option | Purpose |
| --- | --- |
| `--config` | Select Debug, Release, or RelWithDebInfo |
| `--build-dir` | Select the build directory |
| `--version` | Set the plugin version |
| `--channel-id` | Set the plugin distribution channel |
| `--pull-deps` | Prepare public build dependencies |
| `--runtime-config` | Select a runtime descriptor for `package` or `dev` |
| `--output-dir` | Select the package output directory |
| `--ide` | Open the IDE project after `dev` completes |
| `--official` | Enable official-build requirements for supported commands |

For the full option list and defaults, run:

```sh
python3 scripts/cici.py build --help
python3 scripts/cici.py package --help
python3 scripts/cici.py dev --help
```

### Run tests

Configure CMake with:

```text
-DCICI_BUILD_TESTS=ON
```

Build the test targets, then run:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

Public tests cover the foundation library, IPC, translations, OBS bindings, and
plugin UI. Some tests require a local OBS installation; use
`CICI_OBS_RUNTIME_DIR` to specify its runtime library directory.
