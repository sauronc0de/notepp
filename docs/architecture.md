# Notepp Architecture

This document describes the current source layout of the project after the
`architecture_refactor_tasks.md` refactor. It is meant as a quick map for
newcomers; the task tracker is the authoritative source for what is and
is not done.

## High-level shape

```text
.
├── CMakeLists.txt            # Top-level project
├── CMakePresets.json         # Build presets (develop_gui, Test, Release_*)
├── externals/                # Third-party CMake modules / dependency fetching
├── src/
│   ├── main.cpp              # Application entry point
│   ├── app/                  # Application glue code (depends on libraries)
│   │   ├── inc/app.hpp
│   │   └── src/
│   │       ├── app.cpp
│   │       ├── app_sdl.cpp        # SDL + OpenGL context bring-up
│   │       ├── app_imgui.cpp      # ImGui context + font setup
│   │       ├── app_frame_limiter.cpp
│   │       ├── app_history_indicator.cpp
│   │       └── app_profile_modal.cpp
│   └── libraries/            # Project-owned libraries (no app/ dependency)
│       ├── CMakeLists.txt
│       ├── notepp_options/   # Project-wide compile options interface
│       ├── string_utils/
│       ├── tiny_json/
│       ├── json_io/
│       ├── logger/
│       ├── log/
│       ├── lang/
│       ├── emoji_picker/
│       ├── note_model/
│       ├── note_history/
│       ├── note_project/
│       ├── note_storage/
│       ├── note_ui/
│       ├── markdown_code_highlight/
│       ├── markdown_sections/
│       ├── markdown_editor/
│       ├── markdown_tables/
│       ├── markdown_images/
│       ├── markdown_view/
│       ├── markdown_widgets/
│       ├── markdown_support/
│       ├── imgui_md/
│       └── mermaid/
│           ├── inc/                  # Public headers
│           ├── src/
│           │   ├── mermaid.cpp       # Flowchart (the original diagram)
│           │   ├── mermaid_diagrams.cpp  # Back-compat shims (zenuml, etc.)
│           │   ├── registry.cpp      # Render registry
│           │   └── diagrams/         # Per-diagram split sources
│           │       ├── <name>_parser.cpp
│           │       └── <name>_renderer.cpp
│           └── tests/                # Library-local unit tests
└── tests/                    # Standalone test executables (none currently)
```

## Architectural rules

1. **Libraries live under `src/libraries/`.** They may depend on other
   libraries but must not include anything from `src/app/`.
2. **Public APIs are exposed through small headers in each library.**
   Test executables should include only the public header.
3. **Parsers must not depend on ImGui, SDL, OpenGL, or rendering code.**
   Renderers may depend on ImGui, but should receive parsed models rather
   than raw text.
4. **No global mutable state in libraries.** If a library needs to cache
   state, hide it behind a small API (e.g. `NoteContentCache`).
5. **App depends on libraries, not the other way around.** The `App`
   class is the single executable target; its members consume the
   library APIs.

## Mermaid library structure

The Mermaid library renders all diagram types from a single
`MermaidDiagrams` namespace. Each diagram type exposes:

- A parser `parse_<name>(std::string_view, <Diagram>&)` that returns
  `bool` and populates the diagram value.
- A renderer `render_<name>(const <Diagram>&, int id)` that draws via
  ImGui.

The implementations live in `src/libraries/mermaid/src/diagrams/`
(one parser and one renderer file per diagram type), and the public
types live in `src/libraries/mermaid/inc/mermaid_diagrams.hpp`.

A small `registry.cpp` exposes a data-driven lookup table mapping a
diagram type name to its parser/renderer. This is used by
`markdown_support` to dispatch a block without a long `if`/`else`
chain.

The legacy `MERMAID_DISPATCH` macro chain in `markdown_support` is
preserved for now and will be migrated to the registry in a follow-up
change.

## Markdown widget library

`src/libraries/markdown_widgets/` (formerly `markdown_ui`) provides the
`MarkdownWidgets` namespace with widget rendering helpers used by
`markdown_support` for non-trivial markdown blocks (list/inventory/map
widgets, etc.). The library depends on `markdown_view` and ImGui.

## App

`src/app/` is the only place that knows about the executable entry
point and the SDL/ImGui lifecycle. The class `App` lives in
`app.hpp` and its body is split across:

- `app.cpp` – the run loop, frame UI, and most controllers
- `app_sdl.cpp` – `App::init_sdl_gl`
- `app_imgui.cpp` – `App::init_imgui`
- `app_frame_limiter.cpp` – frame-limiter functions
- `app_history_indicator.cpp` – undo/redo indicator
- `app_profile_modal.cpp` – window profile management modal

## Testing

`ctest --preset Test` runs all library-local unit tests. New tests
should live next to the library they cover and use the minimal
expectation helpers in the existing test files.
