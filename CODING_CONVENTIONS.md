# Coding Conventions

## Platform macros (Windows / macOS)

### Purpose

This repository supports **Windows** and **macOS**. Use the conventions below to
keep platform detection consistent across the project.

### Shared definitions

- Use these platform macros:
  - `CICI_OS_WINDOWS`: `1` on Windows, otherwise `0`.
  - `CICI_OS_MACOS`: `1` on macOS, otherwise `0`.
- **CMake** is the primary source of these definitions. Do not rely on a PCH to
  provide them:
  - Calculate the `CICI_OS_WINDOWS` and `CICI_OS_MACOS` variables in the root
    `CMakeLists.txt`.
  - Apply them with
    `add_compile_definitions(CICI_OS_WINDOWS=... CICI_OS_MACOS=...)`.
- `ciciglobal_pch.h` supplies fallback detection and validation:
  - Infer `CICI_OS_*` only when the compiler command line does not define them.
  - Do not infer macOS from `__APPLE__` alone. That macro covers all Apple
    platforms, including iOS, tvOS and watchOS.
  - Use `<TargetConditionals.h>` and `TARGET_OS_OSX`, with
    `TARGET_OS_MAC && !TARGET_OS_IPHONE` as a fallback.

### Application code

- Use `#if CICI_OS_WINDOWS` and `#if CICI_OS_MACOS`.
- Do not use `#ifdef _WIN32`, `#ifdef WIN32`, `#ifdef __APPLE__`,
  `#if defined(TARGET_OS_MAC)` or `#ifdef TARGET_OS_MAC` in application code.
- Test the numeric platform macros with `#if`, not `#ifdef`.
- When code applies to a single platform, use a single-platform guard:
  - `#if CICI_OS_WINDOWS ... #endif`
  - `#if CICI_OS_MACOS ... #endif`
- When implementations differ by platform, enumerate every supported platform
  and reject unsupported platforms:
  - `#if CICI_OS_WINDOWS ... #elif CICI_OS_MACOS ... #else #error "Unsupported platform" #endif`
- Do not use a bare `#else` to imply the other platform. Explicit checks prevent
  silent behavior changes when platforms or build definitions change.

### Exceptions

- Compiler platform-detection macros such as `_WIN32`, `__APPLE__` and
  `TARGET_OS_*` belong in `ciciglobal_pch.h`, where they infer `CICI_OS_*`.
- CMake platform detection with `WIN32` and `APPLE` belongs in the platform
  initialization section of the root `CMakeLists.txt`.
- Vendored code may retain its original platform checks. Do not rewrite
  third-party code merely to standardize macros. Examples include
  vendored source files and
  `ciciplayer/windows/3rd/**`.
- A platform-specific target that does not use `ciciglobal_pch.h` may use compiler
  platform macros locally. Do not spread those checks into shared headers.
- Architecture and pointer-width checks such as `_WIN64`, `__i386__` and
  `__x86_64__` are outside these OS macro conventions.

### Review checklist

- Do new or modified shared platform branches use only `CICI_OS_WINDOWS` and
  `CICI_OS_MACOS`?
- Do multiple-platform branches enumerate the supported platforms and use
  `#else #error "Unsupported platform"` as the fallback?
- Is any code incorrectly treating `__APPLE__` as macOS?
- Have compiler platform-detection macros been added outside the allowed
  detection or platform-specific code?

## CMake platform checks (Windows / macOS / Xcode / MSVC)

### Shared variables

- Use `if(CICI_OS_WINDOWS)` and `if(CICI_OS_MACOS)` for OS checks.
- Use `if(CICI_GEN_XCODE)` for the Xcode generator.
- Use `if(CICI_IDE_MSVC)` for the Visual Studio generator.
- Use `if(CICI_COMPILER_MSVC)` for the MSVC toolchain, including clang-cl.
- Do not use a bare `else()` to imply another OS, compiler or IDE. Prefer:
  `if(CICI_OS_MACOS) ... elseif(CICI_OS_WINDOWS) ... else() message(FATAL_ERROR ...) endif()`.

### Checks to avoid

Subdirectory `CMakeLists.txt` files must use the shared variables instead of
direct checks such as `if(WIN32)`, `if(APPLE)`, `if(XCODE)`,
`if(MSVC_IDE)` or `if(MSVC)`.

### Local checks

Run these commands from the repository root:

```bash
rg -n "\\bTARGET_OS_MAC\\b" -S --glob '!build/**' --glob '!3rd/**' --glob '!depends/**' --glob '!bin/**' --glob '!libs/**' --glob '!gen/**'
rg -n "#\\s*(if|ifdef|ifndef|elif)\\b[^\\n]*\\b(WIN32|_WIN32|__APPLE__)\\b" -S --glob '!build/**' --glob '!3rd/**' --glob '!depends/**' --glob '!bin/**' --glob '!libs/**' --glob '!gen/**'
rg -n "\\b(if|elseif)\\s*\\(\\s*(APPLE|WIN32|XCODE|MSVC_IDE|MSVC)\\b" -S --glob '**/CMakeLists.txt'
```

## String encoding (UTF-8) and types

### Default rules

- Treat all human-readable text as **UTF-8**, including logs, JSON fields,
  UI text and string representations of paths.
- Use these types for text in application code and shared interfaces:
  - `std::u8string` and `std::u8string_view`.
  - `const char8_t*` and `char8_t*`.
  - `u8"…"` literals, whose type is `const char8_t*` in C++20.
- Avoid `std::string` and `const char*` for internal text interfaces. Their
  encoding is implicit and can be confused with the system code page.
- Convert at external boundaries when those types are required, and document
  the reason.

### Boundary exceptions

1. **System APIs**
   - macOS APIs commonly accept `const char*` as UTF-8. Convert with
     `to_char(...)` from `ciciglobal_pch.h`.
   - On Windows, use UTF-16 `W` APIs such as `...W`, not `...A`.
   - Use `utf8ToWide(...)` before calling Windows APIs and `wideToUtf8(...)`
     for returned text. Both are declared in `cicibase/include/misc.h`.
2. **Third-party libraries**
   - JSON, logging and networking APIs may require `const char*` or
     `std::string`. Use `to_char(...)` and `to_u8(...)` at those boundaries;
     keep UTF-8 application text in `std::u8string`.
3. **Binary streams**
   - `std::string` may represent bytes rather than text.
   - Use the `BinaryStream` alias from `ciciglobal_pch.h` for that purpose.
4. **Qt UI**
   - UI code may use `QString`.
   - Convert between Qt strings and UTF-8 with `cicibeauty/qstringconv.h`:
     `to_u8(const QString&) -> std::u8string` and
     `from_u8(const std::u8string&) -> QString`.
   - Avoid ad hoc `QString::toStdString()` and `QString::fromStdString()`
     conversions in application code.

### Conversion helpers

- UTF-8 and `char*` / `std::string`:
  `to_char(...)`, `to_u8(...)` and `to_json(...)` in `ciciglobal_pch.h`.
- Windows UTF-8 and UTF-16:
  `utf8ToWide(...)` and `wideToUtf8(...)` in `cicibase/include/misc.h`.
- Qt strings:
  `to_u8(...)` and `from_u8(...)` in `cicibeauty/qstringconv.h`.

### Review checklist

- Do new or modified text interfaces prefer `std::u8string` and `char8_t*`?
- Are any modules passing text as `std::string` without an explicit boundary
  conversion?
- Does Windows code use `...A` APIs or retain `std::wstring` internally beyond
  the API boundary?
- Does Qt code bypass the shared conversion helpers?

## Commit messages

Write all Git commit messages in English.

## Private inspection tools

The public build and packaging scripts do not run binary confidentiality checks.
Keep any future local inspection scripts, rules, and credentials out of Git;
use the ignored `scripts/local_checks/` directory. Runtime confidentiality and
log checks belong to the private runtime repository.
