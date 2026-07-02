# CMake Guidelines

This document defines the **conventions, structure, and best practices** for
writing `CMakeLists.txt` files.

The goal is to ensure:

-   Consistency across the entire codebase
-   Predictable layout for every library, executable, and external
-   Easy onboarding for new contributors
-   Composable, single-purpose libraries
-   Reproducible builds through presets
-   A clean split between **logic** and **graphics**, so the application
    can be exercised without rendering

These rules apply to every `CMakeLists.txt` you add or modify. They are not
optional.

------------------------------------------------------------------------

# Table of Contents

1.  [Project Layout](#1-project-layout)
2.  [Code Categories](#2-code-categories)
3.  [Top-Level `CMakeLists.txt`](#3-top-level-cmakeliststxt)
4.  [Library Structure](#4-library-structure)
5.  [The Single-Purpose Rule](#5-the-single-purpose-rule)
6.  [Public Header Convention](#6-public-header-convention)
7.  [Libraries Aggregator](#7-libraries-aggregator)
8.  [CMake Presets (`CMakePresets.json`)](#8-cmake-presets-cmakepresetsjson)
9.  [External Dependencies](#9-external-dependencies)
10. [Tests](#10-tests)
11. [Packaging and Installers](#11-packaging-and-installers)
12. [Generated Files and Custom Commands](#12-generated-files-and-custom-commands)
13. [General Best Practices](#13-general-best-practices)
14. [Anti-Patterns to Avoid](#14-anti-patterns-to-avoid)

------------------------------------------------------------------------

# 1. Project Layout

The repository follows a clear separation between **application code**,
**core logic**, **generic utilities**, **visual code**, **external
dependencies**, and **build artifacts**.

    <project>/
    ├── CMakeLists.txt            # Top-level build entry point
    ├── CMakePresets.json         # Configure / build / test presets
    ├── cmake/                    # Helper CMake scripts
    │   └── *.cmake
    ├── src/
    │   ├── libraries/            # Generic utilities, app-independent
    │   │   ├── CMakeLists.txt    # Aggregates every library
    │   │   ├── <libA>/
    │   │   │   ├── CMakeLists.txt
    │   │   │   ├── inc/<libA>.hpp
    │   │   │   ├── src/<libA>.cpp
    │   │   │   └── tests/
    │   │   └── ...
    │   ├── core/                 # App logic, no render, no GUI
    │   │   ├── CMakeLists.txt
    │   │   ├── inc/
    │   │   └── src/
    │   ├── render/               # Visual / UI / graphics
    │   │   ├── CMakeLists.txt
    │   │   ├── inc/
    │   │   └── src/
    │   └── app/                  # Executables (one subdir per target)
    │       ├── cli/              # e.g. command-line interface
    │       │   ├── CMakeLists.txt
    │       │   ├── inc/
    │       │   └── src/
    │       │       └── main.cpp
    │       ├── gui/              # e.g. graphical interface
    │       │   ├── CMakeLists.txt
    │       │   ├── inc/
    │       │   └── src/
    │       │       └── main.cpp
    │       └── ...
    ├── externals/                # Third-party code (FetchContent, find_package, vendored)
    │   └── modules/              # Custom Find*.cmake modules
    ├── tools/                    # Installer / packaging templates
    └── build/                    # Out-of-source build directory (per preset)

**Rules**

-   `src/libraries/` contains **generic, application-independent** utilities
    that could be lifted into any other project as-is.
-   `src/core/` contains the **application's main logic** — domain model,
    business rules, file IO, computation. It must not include any rendering
    or GUI code.
-   `src/render/` contains **all visual and UI code** — widgets, render
    passes, image loading for display, font rendering. It is the only place
    that may depend on a graphics toolkit.
-   `src/app/` contains the **executables**. Each subdirectory under `app/`
    defines exactly one executable, with its own `main.cpp` and
    `CMakeLists.txt`.
-   The build directory is **always** out-of-source. Never commit `build/`.
-   New directories must be added to the top-level `CMakeLists.txt`
    explicitly with `add_subdirectory()`.

------------------------------------------------------------------------

# 2. Code Categories

The source tree is organized into four tiers. The dependency direction is
**strict and one-way**:

    libraries  ←  core  ←  render  ←  app

A tier may only depend on tiers to its **left**.

## 2.1 Tier 1 — Generic Utilities (`src/libraries/`)

Independent, reusable libraries. They have no knowledge of the application
and could be lifted into another project as-is.

Examples:

-   A logging facade.
-   A string manipulation library.
-   A JSON parser.
-   A platform abstraction layer.

**Rule:** `libraries/` must **not** depend on `core/`, `render/`, or `app/`.

## 2.2 Tier 2 — Application Core (`src/core/`)

The application's main logic. It expresses the domain model, business rules,
file IO, network protocols, parsing, simulation, and any non-visual
computation.

**Rule:** `core/` may depend on `libraries/` but **not** on `render/` or
`app/`.

This separation is what lets you exercise the application's logic without
any graphics — for unit tests, integration tests, simulations, headless
servers, or batch processing.

## 2.3 Tier 3 — Render / Visual (`src/render/`)

All graphical and UI code lives here: widgets, render passes, image loaders
for display, font rendering, theme handling, input mapping for the
graphical frontend.

**Rule:** `render/` may depend on `libraries/` and `core/` but **not** on
`app/`.

If a project has no GUI, this directory does not exist (and `app/cli` or
similar links only against `core/` and `libraries/`).

## 2.4 Tier 4 — Executables (`src/app/`)

One subdirectory per executable. Each subdirectory defines a complete
target with its own entry point and `CMakeLists.txt`.

Typical subdirectories:

    src/app/
    ├── cli/                      # Command-line interface
    ├── gui/                      # Graphical interface
    ├── worker/                   # Headless worker (test, simulation, batch job)
    └── ...

Each subdirectory has the standard layout:

    src/app/<executable>/
    ├── CMakeLists.txt
    ├── inc/                      # optional
    └── src/
        └── main.cpp

**Rules**

-   `app/` may depend on all other tiers.
-   `app/` is the **only** tier that defines executables (`add_executable`).
-   The subdirectory name is the executable name: `cli/` → `cli` target,
    `gui/` → `gui` target.
-   A CLI executable typically links against `core/` and `libraries/` only.
    A GUI executable links against `core/`, `render/`, and `libraries/`.

## 2.5 Dependency Matrix

| Tier       | May depend on                                      | Must NOT depend on       |
|------------|----------------------------------------------------|--------------------------|
| libraries  | (nothing internal)                                 | core, render, app        |
| core       | libraries                                          | render, app              |
| render     | libraries, core                                    | app                      |
| app        | libraries, core, render                            | —                        |

A target that links against a higher tier means its code is being used
**the wrong way around**. The `cli` target must not link `render/`. The
`gui` target must not link another executable's internals.

------------------------------------------------------------------------

# 3. Top-Level `CMakeLists.txt`

The top-level `CMakeLists.txt` is the **single entry point** that configures
the whole project. Its responsibilities are limited and ordered.

## 3.1 Required Boilerplate

``` cmake
cmake_minimum_required(VERSION 3.16)

if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

set(ProjectName "<ProjectName>")
project(${ProjectName} VERSION 0.1.0 LANGUAGES C CXX)

# Use the highest C++ standard the toolchain reliably supports (C++23 today).
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

**Rules**

-   Always pin `cmake_minimum_required` to the lowest version that supports
    all features used by the project.
-   Set the C++ standard **globally at the top level** to the highest
    standard the toolchain supports. Do not set it per target.
-   Set `CMAKE_CXX_EXTENSIONS OFF` so the project stays portable across
    GCC, Clang, and MSVC.
-   Enable `CMAKE_EXPORT_COMPILE_COMMANDS` so IDEs and clang tooling work.
-   Forbid CMake 4 from raising the policy version by setting
    `CMAKE_POLICY_VERSION_MINIMUM 3.5`.

## 3.2 Global Compile Options

Apply strict warnings and color flags globally. These are language-specific
but compiler-agnostic, so they belong at the top level:

``` cmake
add_compile_options(
  # Diagnostic color
  $<$<COMPILE_LANG_AND_ID:C,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fdiagnostics-color=always>
  $<$<COMPILE_LANG_AND_ID:C,Clang>:-fcolor-diagnostics>
  $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fcolor-diagnostics>
  # Warnings (treated as errors)
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wall -Wextra -Wconversion -Wshadow -Werror>
)
```

**Rules**

-   `-Wall -Wextra` cover the bulk of useful warnings.
-   `-Wconversion` flags implicit narrowing conversions — required for
    numerically correct code.
-   `-Wshadow` flags variable shadowing — required to keep refactors safe.
-   `-Werror` promotes every warning into a build break. It enforces a
    zero-warning policy across the entire codebase.
-   Per-target flags belong on the target itself, not here.
-   If a third-party header pollutes the build with warnings, prefer fixing
    the upstream bug or wrapping the include in a directory-scoped
    `target_compile_options(<tgt> PRIVATE -Wno-error ...)` for that path —
    do not weaken the global policy.

## 3.3 Project Options

Use `option()` for every user-tunable feature. Defaults must be sensible for
a fresh clone.

``` cmake
option(ENABLE_LOG     "Enable logging"   OFF)
option(ENABLE_RENDER  "Enable rendering" ON)
option(ENABLE_TEST    "Enable tests"     OFF)
```

**Naming**

-   Prefix every option with the project / subsystem name (e.g. `ENABLE_LOG`,
    `ENABLE_DEB_PACKAGE`).
-   Use `ON` / `OFF` literally — CMake treats these as truthy booleans.
-   Document every option in the second argument.

## 3.4 Compile Definitions

`add_compile_definitions()` belongs at the top level when definitions are
project-wide (version macros, asset paths, global feature flags):

``` cmake
add_compile_definitions(
  ASSETS_PATH="${PROJECT_ASSETS_PATH}"
  DATA_PATH="${PROJECT_DATA_PATH}"
  PROJECT_VERSION="${PROJECT_VERSION}"
  $<$<CONFIG:Debug>:PROJECT_DEBUG_UI>
  $<$<BOOL:${ENABLE_LOG}>:ENABLE_LOG=true>
)
```

Per-target definitions use `target_compile_definitions()` on the target.

## 3.5 Find Dependencies at the Top

Locate third-party packages once at the top level. Add custom module paths
**before** the corresponding `find_package` call.

``` cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/modules")
find_package(<Package> REQUIRED)
```

**Rules**

-   Always mark dependencies `REQUIRED` to fail fast on missing ones.
-   Use the imported targets when the package provides them (e.g.
    `SDL2::SDL2`, `OpenGL::GL`).

## 3.6 Subdirectory Order

The subdirectory order **mirrors the dependency direction**. Lower tiers
are added first so higher tiers can link against them.

``` cmake
add_subdirectory(externals)         # 1. Third-party first
add_subdirectory(src/libraries)     # 2. Generic utilities (no internal deps)
add_subdirectory(src/core)          # 3. Application core (depends on libraries)
add_subdirectory(src/render)        # 4. Render / UI (depends on core, libraries)
add_subdirectory(src/app)           # 5. Executables (depends on all tiers)
```

**Rules**

-   Third-party code is added first so internal targets can link against it.
-   Within `src/`, add subdirectories in tier order: `libraries` → `core`
    → `render` → `app`.
-   Each executable subdirectory (`src/app/<executable>/`) defines its own
    target and is added with `add_subdirectory(src/app/<executable>)` in
    `src/app/CMakeLists.txt`.
-   If a tier is absent (e.g. no GUI in a CLI-only project), simply omit
    the corresponding `add_subdirectory()` call.

------------------------------------------------------------------------

# 4. Library Structure

Every in-tree library lives under `src/libraries/<name>/` and follows the
same layout:

    src/libraries/<name>/
    ├── CMakeLists.txt        # Library definition
    ├── inc/                  # Public headers
    │   └── <name>.hpp        # Main public header (must match library name)
    ├── src/                  # Implementation
    │   └── <name>.cpp
    └── tests/                # Unit tests (optional)
        └── <name>_tests.cpp

**Example**

    src/libraries/<name>/
    ├── CMakeLists.txt
    ├── inc/<name>.hpp
    ├── src/<name>.cpp
    └── tests/<name>_tests.cpp

## 4.1 Minimal `CMakeLists.txt` for a Library

``` cmake
add_library(<name> STATIC src/<name>.cpp)

target_include_directories(<name> PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

target_link_libraries(<name>
  PUBLIC
    <public_dependencies>
  PRIVATE
    <private_dependencies>
)

if(ENABLE_TEST)
  add_executable(<name>_tests tests/<name>_tests.cpp)
  target_link_libraries(<name>_tests PRIVATE <name>)
  add_test(NAME <name>_tests COMMAND <name>_tests)
endif()
```

## 4.2 `target_include_directories` Rules

-   Always use `${CMAKE_CURRENT_SOURCE_DIR}/inc` (the absolute path).
-   Use the `PUBLIC` scope for headers your library exposes to its consumers.
-   Never add the source root or build directory to the include path
    carelessly — it leaks private headers.

## 4.3 `target_link_libraries` Visibility

Split dependencies by visibility:

``` cmake
target_link_libraries(<name>
  PUBLIC
    <dep_a>             # exposed in our public header
  PRIVATE
    <dep_b>             # only used inside <name>.cpp
)
```

-   `PUBLIC` if the dependency is used in `inc/<name>.hpp`.
-   `PRIVATE` if it is only used in `src/<name>.cpp`.
-   Be conservative: prefer `PRIVATE` unless the public API requires the
    dependency.

------------------------------------------------------------------------

# 5. The Single-Purpose Rule

> **One library / module = one purpose.**

Every library in `src/libraries/`, every cohesive unit in `src/core/`, and
every visual module in `src/render/` must solve **one well-defined problem**
and expose a small, focused API.

**Good examples**

-   A library that provides string manipulation helpers.
-   A library that implements a minimal JSON reader/writer.
-   A library that parses a specific document format (sections, tables, ...).
-   A library that manages undo/redo history for a domain object.
-   A library that provides a logging facade.

**Bad examples**

-   `utils` — a bag of unrelated helpers.
-   `engine` — too broad, hides multiple subsystems.
-   `kitchen_sink` — everything the application needs.
-   `app_helpers` — mixes UI, file IO, and network calls.

## 5.1 When a Module Grows

If a library starts to take on a second concern, **split it**:

1.  Create a new directory `src/libraries/<new_name>/` (or
    `src/core/<new_name>/`).
2.  Move the second concern's sources, headers, and tests.
3.  Register the new module in its tier aggregator
    (`src/libraries/CMakeLists.txt` or `src/core/CMakeLists.txt`).
4.  Update the original module to depend on the new one (or vice versa).

Do **not** pile unrelated code into an existing module just to avoid
creating a directory.

## 5.2 What Belongs Where

-   **In a library (`libraries/`)**: code that is reusable across projects,
    with no knowledge of the application's domain.
-   **In core (`core/`)**: the application's domain logic, file IO,
    parsers, network, simulation — anything that should run without a
    display.
-   **In render (`render/`)**: anything that draws to a window, handles
    input through a graphical toolkit, or owns a UI widget.
-   **In an executable (`app/`)**: only the wiring that boots a tier set
    into a runnable program — `main()` and friends.

## 5.3 What Does NOT Belong in a Library

-   Application-specific domain rules (those go in `core/`).
-   Any UI or rendering code (that goes in `render/`).
-   Glue code that wires many modules together (that goes in `app/`).
-   The application entry point (`main.cpp`).

------------------------------------------------------------------------

# 6. Public Header Convention

> **The public header file is named after the library.**

For a library named `<name>`, the public header must be:

    src/libraries/<name>/inc/<name>.hpp

This is **not optional**. It is how consumers include your library:

``` cpp
#include <logging.hpp>     // from the "logging" library
#include <document.hpp>    // from the "document" library
```

The same rule applies to modules in `src/core/` and `src/render/`:
their public header is `inc/<module>.hpp` and the target name matches.

## 6.1 Rationale

-   **Discoverability.** Finding the public API of a module is one file
    lookup away.
-   **Stable surface.** The module name, the target name, the directory,
    and the header all match — there is no mapping table to maintain.
-   **Include simplicity.** Callers always include `<name>.hpp`; no need
    to remember which header holds the class.

## 6.2 Rules for the Public Header

-   Place it at `inc/<name>.hpp`.
-   Use `#pragma once`.
-   Keep it focused: declarations only.
-   Prefer forward declarations in the public header; include heavy STL /
    third-party headers only in `src/<name>.cpp`.
-   Wrap the public API in a namespace that matches the module name (e.g.
    `namespace <name> { ... }`).

## 6.3 Private Headers

If a module has helpers that should not be part of its public API, put them
in `src/` (not in `inc/`) and **do not** add `src/` to the public include
directories.

    src/libraries/<name>/
    ├── inc/<name>.hpp        # public
    └── src/
        ├── <name>.cpp        # public API implementation
        └── internal.hpp      # private (only included by .cpp files in src/)

## 6.4 Exceptions

The only sanctioned exception is **vendored third-party code** in
`externals/`, which keeps its original layout and header names.

------------------------------------------------------------------------

# 7. Libraries Aggregator

`src/libraries/CMakeLists.txt` is a one-line-per-library file. It exists
for two reasons:

1.  The top-level `CMakeLists.txt` does not need to know every library by
    name.
2.  Adding a new library is a single-line change in one obvious place.

``` cmake
add_subdirectory(<libA>)
add_subdirectory(<libB>)
add_subdirectory(<libC>)
add_subdirectory(<libD>)
```

**Rules**

-   One `add_subdirectory()` call per library, alphabetized within the file
    (or grouped by domain if a clear grouping is established).
-   Do not declare targets or set variables in this file.
-   When you create a new library, add it here **in the same commit** as
    the library itself.

The same pattern applies to `src/core/CMakeLists.txt` and
`src/render/CMakeLists.txt` (when those tiers exist) and to
`src/app/CMakeLists.txt` for the executable subdirectories.

------------------------------------------------------------------------

# 8. CMake Presets (`CMakePresets.json`)

Use CMake Presets (v3) as the **only** supported way to configure the
build. Hand-rolled `-D` invocations are discouraged.

`CMakePresets.json` defines three arrays:

-   `configurePresets` — how to configure the build tree.
-   `buildPresets` — how to invoke a build.
-   `testPresets` — how to run tests.

## 8.1 Hierarchy of Presets

Use `inherits` to avoid duplication. The pattern is:

1.  `config-base` — hidden base with generator, binary dir, ccache, etc.
2.  `config-<platform>` — hidden platform-specific layer (compiler, OS
    toolchain).
3.  Concrete presets — `develop`, `release-<platform>`, `test`, etc.

## 8.2 Hidden Base Preset

``` json
{
  "name": "config-base",
  "displayName": "Base (Ninja)",
  "description": "General build",
  "hidden": true,
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build/${presetName}",
  "environment": {
    "CCACHE_BASEDIR": "${sourceDir}"
  },
  "cacheVariables": {
    "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
  }
}
```

-   Mark every abstract preset `hidden: true`. Only concrete, user-facing
    presets appear in IDEs and `cmake --list-presets`.

## 8.3 Platform Presets

``` json
{
  "name": "config-<platform>",
  "displayName": "Base (Ninja, <platform>)",
  "hidden": true,
  "inherits": "config-base",
  "cacheVariables": {
    "CMAKE_C_COMPILER":   "<path/to/c-compiler>",
    "CMAKE_CXX_COMPILER": "<path/to/cxx-compiler>"
  }
}
```

Each supported platform gets one hidden preset that pins the toolchain.

## 8.4 Concrete Presets

A concrete preset is what users actually invoke. It inherits a platform
preset and sets build type, features, optimization, and packaging flags.

### Development Preset

``` json
{
  "name": "develop",
  "displayName": "Develop",
  "inherits": "config-<platform>",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE":     "Debug",
    "ENABLE_LOG":           "ON",
    "ENABLE_RENDER":        "ON",
    "CMAKE_C_COMPILER_LAUNCHER":   "ccache",
    "CMAKE_CXX_COMPILER_LAUNCHER": "ccache",
    "CMAKE_UNITY_BUILD":           "ON",
    "CMAKE_UNITY_BUILD_BATCH_SIZE":"4",
    "CMAKE_CXX_FLAGS_DEBUG":       "-O0 -g",
    "CMAKE_EXE_LINKER_FLAGS_DEBUG":     "-fuse-ld=lld",
    "CMAKE_SHARED_LINKER_FLAGS_DEBUG":  "-fuse-ld=lld",
    "USE_PORTABLE_PATHS": "OFF"
  }
}
```

### Release Presets

One release preset per shipping target. Each pins:

-   `CMAKE_BUILD_TYPE` = `Release`
-   `ENABLE_LOG` = `OFF`
-   `USE_PORTABLE_PATHS` = `ON` (relative paths in the bundle)
-   Specific linker / packaging flags

### Test Preset

``` json
{
  "name": "test",
  "displayName": "Test",
  "inherits": "config-<platform>",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE":   "RelWithDebInfo",
    "ENABLE_TEST":        "ON",
    "CMAKE_UNITY_BUILD":  "ON",
    "CMAKE_UNITY_BUILD_BATCH_SIZE": "4",
    "USE_PORTABLE_PATHS": "OFF"
  }
}
```

## 8.5 Build and Test Presets

`buildPresets` and `testPresets` mirror the concrete configure presets
one-to-one:

``` json
{
  "name": "develop",
  "configurePreset": "develop",
  "jobs": 16
}
```

-   `jobs: 0` lets CMake / Ninja decide (useful for release builds on CI).
-   `jobs: 16` is a sensible default for local development.
-   `outputOnFailure: true` is the rule for test presets.

## 8.6 Usage

``` bash
# Configure + build
cmake --preset develop
cmake --build --preset develop

# Run tests
ctest --preset test

# Build a release
cmake --preset release-<platform>
cmake --build --preset release-<platform>
```

------------------------------------------------------------------------

# 9. External Dependencies

All third-party code lives under `externals/`. There are three supported
strategies; pick the one that best matches the dependency.

## 9.1 `FetchContent` (Preferred)

Use `FetchContent` for libraries that should be downloaded at configure
time:

``` cmake
include(FetchContent)
FetchContent_Declare(
  <dep>
  GIT_REPOSITORY https://github.com/owner/<dep>.git
  GIT_TAG        v1.2.3
)
FetchContent_MakeAvailable(<dep>)
```

-   Pin a specific tag or commit hash. Never track `main`.
-   For reproducible builds, set `FETCHCONTENT_BASE_DIR` in the release
    presets to a stable location.

## 9.2 `find_package` (System / Pre-installed)

Use `find_package` for system libraries that are typically present on the
target platform:

``` cmake
find_package(<Package> REQUIRED)
```

-   Always mark dependencies `REQUIRED` to fail fast on missing ones.
-   Use the imported targets when the package provides them (e.g.
    `SDL2::SDL2`, `OpenGL::GL`).

## 9.3 Custom `Find*.cmake` Modules

Place custom modules in `externals/modules/` and prepend the path **before**
the `find_package` call:

``` cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/externals/modules")
find_package(<Package> REQUIRED)
```

-   Module file names follow CMake convention: `Find<PackageName>.cmake`.
-   Modules should define an imported target whenever possible.

## 9.4 Vendored

For libraries that cannot reasonably be fetched or installed, vendor a
specific upstream release under `externals/<dep>/` with its own
`CMakeLists.txt`. This is the **last resort** — always prefer `FetchContent`
first.

## 9.5 Keeping Third-Party Warnings Out of `-Werror`

Because the global compile options include `-Werror`, third-party headers
must be included as **system** headers so their warnings are ignored:

``` cmake
target_include_directories(<target> SYSTEM PRIVATE ${THIRD_PARTY_INCLUDE_DIR})
```

The `SYSTEM` keyword is the supported way to tell the compiler "this is
not my code, do not warn me about it."

------------------------------------------------------------------------

# 10. Tests

Tests are co-located with the module they exercise.

    src/libraries/<name>/tests/<name>_tests.cpp
    src/core/<module>/tests/<module>_tests.cpp

## 10.1 Wiring Tests

``` cmake
if(ENABLE_TEST)
  add_executable(<name>_tests tests/<name>_tests.cpp)
  target_link_libraries(<name>_tests PRIVATE <name>)
  add_test(NAME <name>_tests COMMAND <name>_tests)
endif()
```

**Rules**

-   Test executables are gated behind `ENABLE_TEST` so release builds stay
    small.
-   Test target name: `<name>_tests` (plural).
-   Test binary links the module it tests **privately** — tests are not
    re-exported.
-   Register the test with `add_test()` so `ctest` picks it up.

## 10.2 Fuzz / Larger Test Suites

For modules with multiple test files or sub-suites, delegate to a
subdirectory:

``` cmake
if(ENABLE_TEST)
  add_subdirectory(tests)
endif()
```

And inside `tests/CMakeLists.txt`, follow the same one-test-per-file
convention with a single umbrella test name when appropriate.

## 10.3 Testing the Core Without Graphics

One of the main benefits of the `core/` / `render/` split is that **core
tests do not need a display**. A core test executable links only against
`core/` and `libraries/`:

``` cmake
add_executable(<module>_tests tests/<module>_tests.cpp)
target_link_libraries(<module>_tests
  PRIVATE
    <module>        # from src/core/<module>/
    <dep_a>         # from src/libraries/<dep_a>/
)
add_test(NAME <module>_tests COMMAND <module>_tests)
```

This lets the entire test suite run on a headless CI runner.

------------------------------------------------------------------------

# 11. Packaging and Installers

Packaging logic is **always** in the top-level `CMakeLists.txt` and is
**always** gated behind a feature option.

## 11.1 Configure Installer Templates

``` cmake
configure_file(
  ${CMAKE_SOURCE_DIR}/tools/installer/<installer>.in
  ${CMAKE_BINARY_DIR}/<installer>
  @ONLY
)
```

-   Templates live in `tools/installer/`.
-   Generated files live in the build directory — never in the source tree.

## 11.2 Windows Installer

``` cmake
if(WIN32)
  find_program(<INSTALLER_EXE>
    NAMES <installer-exe>
    PATHS "<installer-install-path>"
    REQUIRED
  )
  add_custom_target(installer ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CMAKE_SOURCE_DIR}/dist/<ProjectName>"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:${PROJECT_NAME}>"
      "${CMAKE_SOURCE_DIR}/dist/<ProjectName>/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${<INSTALLER_EXE>}" "${CMAKE_BINARY_DIR}/<installer>"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Staging app files and building installer"
    VERBATIM
  )
endif()
```

## 11.3 Linux (CPack / .deb / .rpm)

``` cmake
elseif(ENABLE_<PACKAGE>)
  install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin COMPONENT <component>)
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION "share/<project>/assets" COMPONENT <component>)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_NAME "<project>")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_CONTACT "<contact-email>")
  set(CPACK_PACKAGE_FILE_NAME "<project>")
  set(CPACK_COMPONENTS_ALL "<component>")
  set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};<component>;/")
  include(CPack)

  add_custom_target(<package>_package ALL
    COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Building <package> package"
    VERBATIM
  )
endif()
```

**Rules**

-   Always gate platform-specific packaging behind `if(WIN32)` /
    `elseif(...)`.
-   Always wrap non-build steps in `add_custom_target(... VERBATIM)`.
-   Never commit `dist/` or the build directory.

------------------------------------------------------------------------

# 12. Generated Files and Custom Commands

Generated files belong in `${CMAKE_BINARY_DIR}` — never in the source
tree.

## 12.1 Pattern

``` cmake
set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

add_custom_command(
  OUTPUT  "${GEN_DIR}/<name>.hpp"
  COMMAND ${CMAKE_COMMAND}
    -DINPUT_FILE=${CMAKE_SOURCE_DIR}/<input>
    -DOUTPUT_FILE="${GEN_DIR}/<name>.hpp"
    -P ${CMAKE_SOURCE_DIR}/cmake/<script>.cmake
  DEPENDS "${CMAKE_SOURCE_DIR}/<input>"
  COMMENT "Generating <name>.hpp"
  VERBATIM
)

add_custom_target(<name>_gen
  DEPENDS "${GEN_DIR}/<name>.hpp"
)
```

## 12.2 Rules

-   One `add_custom_command` per generated file, one `add_custom_target`
    per logical group.
-   Always list every input in `DEPENDS`. Stale generated headers are a
    common build break.
-   Always use `VERBATIM` to keep the command portable across generators.
-   Make the executable `add_dependencies(${PROJECT_NAME} <gen_target>)`
    so the generated headers are produced before the binary links.

## 12.3 Generator Scripts

Scripts invoked by `-P` live in `cmake/` and receive their inputs through
`-D` cache variables on the command line. They are pure CMake scripts —
no project context, no `project()` call.

------------------------------------------------------------------------

# 13. General Best Practices

## 13.1 Modern CMake

-   Always use **targets** (`add_library`, `add_executable`), never
    `add_definitions`, `include_directories`, or `link_libraries`
    (global).
-   Always use `target_*` commands to set includes, definitions, compile
    options, and link libraries.
-   Specify **visibility** (`PUBLIC` / `PRIVATE` / `INTERFACE`)
    explicitly on every `target_link_libraries` call.
-   Prefer **generator expressions** over conditional logic for
    configuration-dependent values.

## 13.2 Naming

-   Target name = directory name = `inc/<name>.hpp` header name.
-   Test executable name = `<name>_tests`.
-   Use lowercase `snake_case` for target names.
-   Cache variables / options: `UPPER_SNAKE_CASE` with a project prefix
    (`<PROJECT>_*`, `ENABLE_*`).

## 13.3 Required Cache Variables

Set these at the top level when relevant:

-   `CMAKE_CXX_STANDARD` (project default — highest supported).
-   `CMAKE_CXX_STANDARD_REQUIRED` = `ON`.
-   `CMAKE_CXX_EXTENSIONS` = `OFF` (keeps the project portable).
-   `CMAKE_EXPORT_COMPILE_COMMANDS` = `ON`.
-   `CMAKE_UNITY_BUILD` — controlled per-preset.

## 13.4 Minimal Required Version

-   `cmake_minimum_required(VERSION 3.16)` at the top level.
-   Each `CMakeLists.txt` should not redeclare the project or required
    version — that belongs to the top-level file.

## 13.5 Conditions and Branching

-   Use `option()` for user-facing toggles.
-   Use `if(DEFINED ...)` to detect optional features.
-   Avoid `if(${VAR})` — use `if(VAR)` so empty / `OFF` / `NOTFOUND`
    behave correctly.

## 13.6 Generator Expressions Over Variables

``` cmake
target_compile_definitions(<name> PRIVATE
  $<$<CONFIG:Debug>:PROJECT_DEBUG_UI>
  $<$<BOOL:${ENABLE_LOG}>:ENABLE_LOG=true>
)
```

This avoids polluting global state and keeps configuration-specific values
scoped to the target.

## 13.7 Compiler Launcher (ccache)

-   Set `CMAKE_C_COMPILER_LAUNCHER` and `CMAKE_CXX_COMPILER_LAUNCHER` to
    `ccache` in development / test presets.
-   Do not enable ccache in release presets used for CI packaging if the
    CI environment does not have a ccache cache configured.

## 13.8 Unity Builds

-   Enabled per-preset (`CMAKE_UNITY_BUILD=ON`,
    `CMAKE_UNITY_BUILD_BATCH_SIZE=4`) to speed up local dev and test
    builds.
-   Disabled in release packaging presets to keep object boundaries
    clean.

## 13.9 Diagnostics

-   Use `message(STATUS ...)` for informational output (detected paths,
    enabled options).
-   Use `message(FATAL_ERROR ...)` for unrecoverable configuration
    problems.

## 13.10 Versioning

-   `project()` declares the canonical project version (`VERSION
    x.y.z`).
-   The version is exposed to C++ as `PROJECT_VERSION` via
    `add_compile_definitions`.
-   Bump the version in the top-level `CMakeLists.txt` only — never copy
    it elsewhere.

## 13.11 Enforce the Tier Boundaries

When reviewing a `CMakeLists.txt`, ask:

-   Does this `target_link_libraries` respect the dependency direction?
-   Does this `target_include_directories` add a tier's `src/` to another
    tier?
-   Does any header in `libraries/` include anything from `core/`,
    `render/`, or `app/`?

If the answer to any of these is yes, the build is leaking tier boundaries
and should be refactored.

------------------------------------------------------------------------

# 14. Anti-Patterns to Avoid

| ❌ Anti-pattern                                          | ✅ Use instead                                                    |
|----------------------------------------------------------|-------------------------------------------------------------------|
| `add_definitions(-DFOO=1)`                              | `target_compile_definitions(<tgt> PRIVATE FOO=1)`                 |
| `include_directories(inc)`                               | `target_include_directories(<tgt> PUBLIC inc)`                   |
| `link_libraries(<dep>)`                                 | `target_link_libraries(<tgt> PRIVATE <dep>::<dep>)`              |
| Mixing private includes into `PUBLIC`                    | Use `PRIVATE` for headers only used inside your `.cpp`            |
| `file(GLOB_RECURSE SRC *.cpp)`                           | Explicit source lists — `GLOB` is non-deterministic              |
| `add_library(<name> src/a.cpp src/b.cpp src/c.cpp ...)`  | Keep source list readable; wrap inside a helper if huge           |
| Naming a module `utils` / `engine` / `helpers`          | One purpose per module, named after its concern                    |
| Public header named `helpers.hpp` inside `<lib_name>/`   | Header file must match the module name                             |
| Adding a module but forgetting `add_subdirectory()`     | Always add the new module in its tier aggregator                  |
| Linking `core/` into `render/`'s tests                  | Tests must respect the tier direction                            |
| Linking `render/` into `app/cli`                         | A CLI executable must not depend on render                        |
| Vendoring without a tag pin                              | Use `FetchContent` with `GIT_TAG` or a vendored release tarball   |
| Hand-rolled build commands instead of presets            | Add a `CMakePresets.json` entry                                  |
| In-source builds (`cmake .`)                            | Always out-of-source via a preset                                |
| `set(CMAKE_CXX_FLAGS "-Wall")`                          | `add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra ...>)` |
| `if(${ENABLE_LOG})`                                     | `if(ENABLE_LOG)` — never re-expand a boolean                      |
| `add_custom_command(... NON_VERBATIM)`                  | Always `VERBATIM` for portable commands                          |
| Generating files inside the source tree                 | Generate under `${CMAKE_BINARY_DIR}/generated`                    |
| Tests in a global `tests/` directory                     | Co-locate tests with the module under test                       |
| Third-party headers not marked `SYSTEM` under `-Werror` | Use `target_include_directories(<tgt> SYSTEM PRIVATE ...)`         |

------------------------------------------------------------------------

# Quick Checklist for Adding a New Library

Use this checklist every time you add a new library to the project.

-   [ ] Create `src/libraries/<name>/` with subdirs `inc/`, `src/`, and
        optionally `tests/`.
-   [ ] Add public header `inc/<name>.hpp` and implementation
        `src/<name>.cpp`.
-   [ ] Write `src/libraries/<name>/CMakeLists.txt` using the minimal
        library template.
-   [ ] If the library has tests, add `<name>_tests` gated by
        `ENABLE_TEST`.
-   [ ] Register the library in `src/libraries/CMakeLists.txt`
        (`add_subdirectory(<name>)`).
-   [ ] Link it from the executable (or from `core/` / `render/` modules)
        using `target_link_libraries(... PUBLIC <name>)`.
-   [ ] Configure with a preset: `cmake --preset develop`.
-   [ ] Build: `cmake --build --preset develop`.
-   [ ] If tests exist: `ctest --preset test`.
-   [ ] Update project documentation if the new library is part of the
        public architecture.

------------------------------------------------------------------------

# Quick Checklist for Adding a New Executable

Use this checklist every time you add a new executable target.

-   [ ] Pick a tier-3-free name (e.g. `cli`, `gui`, `worker`, `bench`).
-   [ ] Create `src/app/<executable>/` with `inc/` (optional), `src/`,
        and `src/main.cpp`.
-   [ ] Write `src/app/<executable>/CMakeLists.txt` that calls
        `add_executable(<executable> src/main.cpp)`.
-   [ ] Link only the tiers the executable needs (a CLI links `core/` and
        `libraries/`, never `render/`).
-   [ ] Register the executable in `src/app/CMakeLists.txt`
        (`add_subdirectory(<executable>)`).
-   [ ] Configure and build with a preset.
-   [ ] Run the executable to confirm it starts without a display if it
        is a headless target.

------------------------------------------------------------------------

# Quick Checklist for Adding a New Preset

-   [ ] Pick a base: `config-base`, `config-<platform>`, or another
        concrete preset.
-   [ ] Inherit from it via `"inherits": "..."`.
-   [ ] Set `cacheVariables` for build type, features, optimization,
        linker, ccache, and packaging.
-   [ ] Add a matching `buildPresets` entry with `configurePreset` and
        `jobs`.
-   [ ] Add a matching `testPresets` entry with `outputOnFailure: true`.
-   [ ] Test locally with `cmake --preset <name> && cmake --build --preset
        <name>`.

------------------------------------------------------------------------

# References

-   [CMake Documentation](https://cmake.org/cmake/help/latest/)
-   [CMake Presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
-   [Modern CMake](https://cliutils.gitlab.io/modern-cmake/)
-   [Effective Modern CMake](https://gist.github.com/mbinna/c61dbb39cba0e4fb7a1ba5ad217ccd8b)
