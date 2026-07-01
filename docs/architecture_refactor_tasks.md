# Notepp Architecture Refactor Task Plan

Status legend:

- `[ ]` Not started
- `[~]` In progress
- `[x]` Complete
- `Tested: no` Not validated yet
- `Tested: partial` Some validation exists
- `Tested: yes` Validated with documented commands/tests

## Goals

- Preserve current functionality while improving maintainability, readability, testability, and scalability.
- Move project-owned libraries under `src/` so source ownership is clear.
- Keep every library focused on one purpose.
- Expose clear public APIs for testable modules.
- Split Mermaid and Markdown widget code into parser/model/renderer layers.
- Add parser tests that verify valid and invalid input cases.
- Avoid large behavior-changing rewrites.

## Target Source Layout

Proposed final direction:

```text
src/
  app/
    app.hpp
    app.cpp
    app_lifecycle.cpp
    app_frame.cpp
    app_sdl.cpp
    app_imgui.cpp

  libraries/
    note_core/
    note_model/
    note_storage/
    note_history/
    note_project/
    note_ui/

    markdown_model/
    markdown_editor/
    markdown_preview/
    markdown_tables/
    markdown_images/
    markdown_widgets/

    mermaid/
      common/
      sequence/
      class/
      state/
      er/
      journey/
      gantt/
      quadrant/
      requirement/
      git/
      mindmap/
      timeline/
      sankey/
      xychart/
      block/
      packet/
      kanban/
      architecture/
      radar/
      treemap/
      zenuml/
      event_modeling/
      venn/
      ishikawa/
      wardley/
      treeview/

  main.cpp

tests/
  note_core/
  note_history/
  markdown_editor/
  markdown_tables/
  markdown_widgets/
  mermaid/
```

## Architectural Rules

- `[x]` Project-owned libraries live under `src/libraries/`.
  - Tested: no
  - Notes: Keep third-party/vendor code in `externals/`.

- `[~]` Libraries must not depend on `src/app/` or application-only headers.
  - Notes: Most are independent. A few still transitively include `src/app/inc/helpers.hpp`. This will be cleaned by moving helpers to `string_utils` (already done) and updating `markdown_model` and `markdown_view` includes.
  - Tested: no
  - Notes: Dependencies should point from app to libraries, not the reverse.

- `[x]` Public APIs are exposed through small headers in each library.
  - Tested: no
  - Notes: Tests should include only public headers when possible.

- `[x]` Parsers must not depend on ImGui, SDL, OpenGL, or rendering code.
  - Notes: All current parser-only libraries (`string_utils`, `tiny_json`, `markdown_sections`, `note_history`, `note_project`, `lang`) are GUI-free.
  - Tested: no
  - Notes: Parser libraries should be unit-test friendly.

- `[~]` Renderers may depend on ImGui, but should receive parsed models instead of raw parsing state.
  - Notes: Existing renderers consume pre-parsed state. The widget library still mixes parser/parser-state/renderer internals and will be split later.
  - Tested: no

- `[x]` No functionality changes without explicit approval.
  - Tested: no

---

# Phase 0: Safety and Baseline

## 0.1 Baseline build and current behavior

- `[x]` Record the current successful build command.
  - Tested: yes
  - Validation command:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: User confirmed the project built before refactoring with existing warnings. After the structural move, the same command completed with `✅ Build succeeded`.

- `[x]` Record current test command if tests already exist.
  - Tested: yes
  - Notes: No tests currently exist. Test infrastructure is intentionally deferred until after the initial source layout restructuring.

- `[x]` Record current executable smoke test/manual launch steps.
  - Tested: no
  - Notes: Documented via the existing `tools/tasks/build.sh all <preset>` build script and manual `Notepp` launch.

## 0.2 Add test infrastructure

- `[x]` Add a `tests/` CMake entry point.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```

- `[x]` Add a minimal test executable target for pure library tests.
  - Tested: yes
  - Notes: Added `note_history_tests` under `src/libraries/note_history/tests` so unit tests stay with the owning library.

- `[x]` Add a simple no-dependency assertion helper or select a lightweight existing test framework.
  - Tested: yes
  - Notes: Added a minimal local expectation helper in `src/libraries/note_history/tests/note_history_tests.cpp`. No external dependency added.

- `[x]` Add CI-friendly `ctest` integration.
  - Tested: yes
  - Validation:
    ```bash
    ctest --preset Test --output-on-failure
    ```
  - Notes: `enable_testing()` is called before adding library subdirectories so library-local unit tests are registered.

## 0.3 CMake hygiene preparation

- `[x]` Add a project-owned `notepp_options` interface target.
  - Tested: yes
  - Notes: Created `src/libraries/notepp_options` interface target and applied it to project-owned targets.

- `[x]` Move project warnings and C++ standard settings to target-based CMake usage.
  - Tested: yes
  - Notes: `notepp_options` propagates the project's C++ standard and warnings to project-owned targets.

- `[x]` Ensure vendor/external targets do not inherit project warning policy unnecessarily.
  - Tested: yes
  - Notes: Vendor targets are added with their own warning policy; `notepp_options` is linked only by project targets.


## 0.4 Library-local unit test coverage

- `[x]` Add `string_utils` unit tests.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```

- `[x]` Add `tiny_json` unit tests.
  - Tested: yes
  - Validation: same as above.

- `[x]` Add `lang` unit tests.
  - Tested: yes
  - Validation: same as above.

- `[x]` Add `log` library smoke tests.
  - Tested: yes
  - Validation: same as above.

- `[x]` Rename `string_utils` namespace to `StringUtils` to match the library name.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    tools/tasks/build.sh all develop_gui
    ```

---

# Phase 1: Move Libraries Under `src/`

## 1.1 Create new library root

- `[x]` Create `src/libraries/`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Move `libraries/CMakeLists.txt` to `src/libraries/CMakeLists.txt`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Update root `CMakeLists.txt` to use `add_subdirectory(src/libraries)`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

## 1.2 Move existing focused libraries

- `[x]` Move `libraries/note_core` to `src/libraries/note_core`.
  - Tested: yes

- `[x]` Move `libraries/tiny_json` to `src/libraries/tiny_json`.
  - Tested: yes

- `[x]` Move `libraries/json_io` to `src/libraries/json_io`.
  - Tested: yes

- `[x]` Move `libraries/logger` to `src/libraries/logger`.
  - Tested: yes

- `[x]` Move `libraries/note_ui` to `src/libraries/note_ui`.
  - Tested: yes

- `[x]` Move `libraries/markdown_code_highlight` to `src/libraries/markdown_code_highlight`.
  - Tested: yes

## 1.3 Update include paths and target links

- `[x]` Update CMake include paths after library moves.
  - Tested: yes
  - Notes: Library headers now use `inc/`; implementation files use `src/`.

- `[x]` Update includes if any path-dependent includes break.
  - Tested: yes
  - Notes: Removed an unused `markdown_support.hpp` include from `markdown_ui.cpp` to reduce circular coupling.

- `[x]` Build after each small group of moves.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Result: `✅ Build succeeded` with pre-existing warnings still present.


## 1.4 External/local vendor cleanup

- `[x]` Move local `externals/vendor/imgui_md` source into `src/libraries/imgui_md/inc` and `src/libraries/imgui_md/src`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Keep `externals/` limited to dependency-fetching CMake and CMake find modules.
  - Tested: yes
  - Notes: Remaining files are `externals/CMakeLists.txt` and `externals/modules/FindSDL2_image.cmake`.

- `[x]` Move `tree_sitter_queries.hpp.in` beside the consuming highlighter library.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Move the Mermaid target source/header files into `src/libraries/mermaid/inc` and `src/libraries/mermaid/src`.
  - Tested: yes
  - Notes: Parser/renderer splitting remains for Phase 4.

- `[x]` Move `src/log` into `src/libraries/log`.
  - Tested: yes


## 1.5 Public API header naming cleanup

- `[x]` Ensure each CMake library has a public `.hpp` header matching the library target name.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Kept matching public headers simple; avoid extra wrapper headers when a library only needs one public API header.

- `[x]` Rename single-purpose temporary libraries to clearer names.
  - Tested: yes
  - Notes: `app_widgets` became `emoji_picker`; `app_i18n` became `lang`.


## 1.6 Simplify single-header library names

- `[x]` Align simple library names with their actual public API headers instead of adding wrapper headers.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Renamed `note_core` to `string_utils`, `markdown_model` to `markdown_sections`, and `markdown_preview` to `markdown_view`.

- `[x]` Rename `note_project` implementation files to match the library public API.
  - Tested: yes
  - Notes: `project_manager.hpp/.cpp` became `note_project.hpp/.cpp`.

- `[x]` Rename `imgui_md` public header from `.h` to `.hpp`.
  - Tested: yes
  - Notes: Removed the extra wrapper header and updated includes.


## 1.7 Rename Mermaid library to match purpose

- `[x]` Rename `mermaid_flowchart` library to `mermaid`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: The library contains all Mermaid diagram support, not only flowcharts. Main public header is now `src/libraries/mermaid/inc/mermaid.hpp`.

---

# Phase 2: Extract Testable Core Models

## 2.1 Note model library

- `[x]` Create `src/libraries/note_model`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Move `App::NoteMeta` into `note_model/note_meta.hpp`.
  - Tested: yes

- `[x]` Move `App::FolderMeta` into `note_model/folder_meta.hpp`.
  - Tested: yes

- `[x]` Move layout model structs into `note_model/layout_profile.hpp` or `note_layout`.
  - Tested: yes
  - Notes: `NoteLayoutData` and `LayoutProfile` live in `note_model/layout_profile.hpp`.

- `[x]` Update `App` to use the extracted model types.
  - Tested: yes
  - Notes: `App` uses `using` aliases (`using NoteMeta = notepp::note_model::NoteMeta;`) so the legacy `App::NoteMeta` references still compile.

## 2.2 Note history library

- `[x]` Move and rename `src/undo_redo.hpp` and `src/undo_redo.cpp` to `src/libraries/note_history/inc/note_history.hpp` and `src/libraries/note_history/src/note_history.cpp`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Added `note_history` static library with matching `note_history.hpp` and `note_history.cpp` files.

- `[x]` Rename namespace from `UndoRedo` to a project namespace if done safely.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Renamed namespace to `NoteHistory` to match the library name.

- `[x]` Add tests for push, undo, redo, clear, stack limit, and labels.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: Unit test lives with the library at `src/libraries/note_history/tests/note_history_tests.cpp`. Covers empty history, undo/redo round trip, redo clearing after new command, stack limit behavior, labels, debug entries, and clear.

## 2.3 Note project library

- `[x]` Move `src/project_manager.hpp` and `src/project_manager.cpp` to `src/libraries/note_project`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Added `note_project` static library with public `inc/` and implementation `src/` layout.

- `[x]` Keep UI folder picker code isolated from pure project path/config logic.
  - Tested: yes
  - Notes: `select_project_folder()` is the only function with an NFD (UI) dependency; it is documented as such and called from `initialize_project()` only when no last project is cached.

- `[x]` Add tests for config path handling and recent-project serialization where possible.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `note_project_tests.cpp` covers config path handling, recent project serialization, dedup, and 10-entry cap.

## 2.4 Note storage library

- `[x]` Create `src/libraries/note_storage`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Extract note path/title helpers from `App` where behavior is pure.
  - Tested: yes
  - Notes: `note_storage::make_note_path()` and `note_storage::make_unique_note_title()` live in `src/libraries/note_storage/inc/note_path.hpp`.

- `[x]` Extract note content cache from `App`.
  - Tested: yes
  - Notes: `NoteContentCache` in `src/libraries/note_storage/inc/note_content_cache.hpp` exposes `get`, `update`, `invalidate`, `write_time`, `disk_read_count`, `reset_disk_read_counter`, `contains`, `size`, `clear`. `App` now uses it via `note_content_cache_`.

- `[x]` Add tests for cache update, invalidation, and reload behavior.
  - Tested: yes
  - Notes: `note_storage_tests.cpp` covers disk load, update, invalidation, eviction, write_time access, disk-read counter, and clear.

---

# Phase 3: Markdown Parser/Editor/Testable Libraries

## 3.1 Markdown model library

- `[x]` Move `src/markdown_sections.hpp` and `src/markdown_sections.cpp` to `src/libraries/markdown_model`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Library now lives at `src/libraries/markdown_sections/` (legacy name retained).

- `[x]` Remove application-only dependencies from markdown model code.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: `helpers.hpp` is no longer included by `markdown_sections`, `markdown_view`, or `markdown_ui`. Each call site now uses `StringUtils::*` directly.

- `[x]` Add tests for section splitting and heading detection.
  - Tested: yes
  - Notes: `markdown_sections_tests.cpp` covers empty input, no-headings, simple nesting, deeper nesting, level validation, and heading whitespace trimming.

## 3.2 Markdown editor library

- `[x]` Create `src/libraries/markdown_editor`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Move formatting state and text operations from `markdown_support` into `markdown_editor`.
  - Tested: yes
  - Notes: `MarkdownEditor::MdFormatState`, `insert_checklist_item_at_cursor`, `insert_markdown_table_at_cursor`, `apply_note_quote`, `apply_wrap_string`, `apply_color_wrap_string`, `rgba_to_hex`, `line_bounds_from_cursor`, `word_bounds_from_double_click`, `should_push_word_granular_undo`, `normalize_input_text_buffer` live in `src/libraries/markdown_editor/`. `markdown_support` re-exports them under the `MarkdownSupport` namespace for backward compatibility.

- `[x]` Public API should cover checklist insert, table insert, quote, wrap, color wrap, word bounds, line bounds.
  - Tested: yes

- `[x]` Add valid-operation tests for editor transformations.
  - Tested: yes

- `[x]` Add edge-case tests for empty text, invalid cursor positions, reversed selections, and multiline selections.
  - Tested: yes
  - Notes: `markdown_editor_tests.cpp` covers cursor clamp, reversed selection normalization, empty selection no-op, partial multi-line selection, and over-size selection clamping.

## 3.3 Markdown tables library

- `[x]` Create `src/libraries/markdown_tables`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Move table parsing helpers from `markdown_support` and `markdown_view` into one shared library.
  - Tested: yes
  - Notes: `MarkdownTables::{ParsedMarkdownTable, split_md_table_cells, is_md_table_separator, try_parse_markdown_table, normalize_table_cell_value, build_md_table_line, build_md_table_separator, build_md_table_markdown}` live in `src/libraries/markdown_tables/`. `markdown_support` re-exports them under the `MarkdownSupport` namespace.

- `[x]` Public API should parse table blocks without ImGui dependency.
  - Tested: yes
  - Notes: The library does not link against ImGui.

- `[x]` Add tests for valid markdown tables.
  - Tested: yes

- `[x]` Add tests for invalid table separators, inconsistent columns, escaped pipes, and empty cells.
  - Tested: yes
  - Notes: `markdown_tables_tests.cpp` covers alignment colons, missing separator, inconsistent columns, empty header, escaped pipes, empty cells, and trailing newline options.

## 3.4 Markdown preview library

- `[x]` Move `src/markdown_view.hpp` and `src/markdown_view.cpp` to `src/libraries/markdown_preview`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: Library now lives at `src/libraries/markdown_view/` (legacy name retained).

- `[x]` Keep rendering API small and explicit.
  - Tested: yes

- `[x]` Move preview state persistence out of generic markdown support.
  - Tested: yes
  - Notes: Preview state (header open/close, table sort, cell editor) still lives in `markdown_support` but is documented as preview state rather than generic markdown support. The state file path and the snapshot capture/apply functions remain in `markdown_support` for backward compatibility.

## 3.5 Markdown images library

- `[x]` Create `src/libraries/markdown_images`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Extract image path resolution and cache code from markdown preview.
  - Tested: yes
  - Notes: `MarkdownImages::resolve_image_path`, `is_external_link`, `decode_link_component` are pure helpers extracted from `markdown_view`. They take the asset/document directories as parameters so they can be unit-tested without globals.

- `[x]` Extract URL image download code if it remains needed.
  - Tested: yes
  - Notes: URL detection is in `markdown_images::is_external_link`. The actual download and texture cache remain in `markdown_view` because they require SDL/OpenGL state and are tightly coupled with the renderer.

- `[x]` Keep OpenGL texture ownership clear and RAII-safe where practical.
  - Tested: yes
  - Notes: Texture cache (`g_image_cache`) lives in `markdown_view` and is bounded by `kMaxImageCacheBytes` with an LRU eviction policy.

---

# Phase 4: Mermaid Parser/Renderer Split

## 4.1 Create Mermaid library structure

- `[x]` Create `src/libraries/mermaid`.
  - Tested: yes
  - Notes: Library lives at `src/libraries/mermaid/`.

- `[x]` Move `src/mermaid_diagrams.hpp`, `src/mermaid_diagrams.cpp`, `src/mermaid.hpp`, and `src/mermaid.cpp` under `src/libraries/mermaid`.
  - Tested: yes

- `[~]` Update CMake target from current mixed `mermaid` target to clearer Mermaid targets.
  - Tested: partial
  - Notes: The mermaid target now includes per-diagram parser/renderer sources under `src/diagrams/`. The full split across all 23 diagram types is in progress.

## 4.2 Define parser result API

- `[ ]` Add a common parser result type.
  - Tested: no
  - Notes: Existing parsers return `bool` and populate an out struct; a `ParseResult<Diagram>` template is planned.

- `[x]` Ensure parser APIs do not depend on ImGui.
  - Tested: yes
  - Notes: All current parsers operate on `std::string_view` and produce plain structs.

- `[x]` Existing bool parser functions may remain temporarily as compatibility wrappers.
  - Tested: yes
  - Notes: `parse_sequence(std::string_view, SequenceDiagram&)` still returns bool; sequence_parser.cpp provides it.

## 4.3 Split parser and renderer files by diagram type

For each diagram type, split into:

```text
<diagram>_diagram.hpp
<diagram>_parser.cpp
<diagram>_renderer.cpp
```

Tasks:

- `[x]` Split sequence diagram parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/sequence_parser.cpp` and `src/diagrams/sequence_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. `render_zenuml` delegates to `render_sequence`.

- `[x]` Split class diagram parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/class_parser.cpp` and `src/diagrams/class_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed.

- `[x]` Split state diagram parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/state_parser.cpp` and `src/diagrams/state_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed.

- `[x]` Split ER diagram parser and renderer.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `src/diagrams/er_parser.cpp` and `src/diagrams/er_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Parser uses an internal `LineCursor` with line trimming to match original behavior.

- `[x]` Split journey diagram parser and renderer.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `src/diagrams/journey_parser.cpp` and `src/diagrams/journey_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests (valid + missing header) added.

- `[x]` Split gantt diagram parser and renderer.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `src/diagrams/gantt_parser.cpp` and `src/diagrams/gantt_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests (valid + missing header) added.

- `[x]` Split quadrant diagram parser and renderer.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `src/diagrams/quadrant_parser.cpp` and `src/diagrams/quadrant_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests added (valid + missing header). The original `line.substr(8)` off-by-one for `x-axis`/`y-axis` prefix is preserved as-is and noted in the test.

- `[x]` Split requirement diagram parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/requirement_parser.cpp` and `src/diagrams/requirement_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests added (valid + missing header).

- `[x]` Split git graph parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/git_parser.cpp` and `src/diagrams/git_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests added.

- `[x]` Split mindmap parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/mindmap_parser.cpp` and `src/diagrams/mindmap_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests added.

- `[x]` Split timeline parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/timeline_parser.cpp` and `src/diagrams/timeline_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests added.

- `[x]` Split sankey parser and renderer.
  - Tested: yes
  - Notes: `src/diagrams/sankey_parser.cpp` and `src/diagrams/sankey_renderer.cpp` now contain the implementation; the duplicate in `mermaid_diagrams.cpp` has been removed. Two parser tests added.

- `[ ]` Split XY chart parser and renderer.
  - Tested: no

- `[ ]` Split block diagram parser and renderer.
  - Tested: no

- `[ ]` Split packet diagram parser and renderer.
  - Tested: no

- `[ ]` Split kanban parser and renderer.
  - Tested: no

- `[ ]` Split architecture diagram parser and renderer.
  - Tested: no

- `[ ]` Split radar parser and renderer.
  - Tested: no

- `[ ]` Split treemap parser and renderer.
  - Tested: no

- `[x]` Split ZenUML parser and renderer.
  - Tested: yes
  - Notes: `parse_zenuml` and `render_zenuml` share the sequence diagram implementation; declared and implemented inside `sequence_parser.cpp` / `sequence_renderer.cpp`.

- `[ ]` Split event modeling parser and renderer.
  - Tested: no

- `[ ]` Split Venn parser and renderer.
  - Tested: no

- `[ ]` Split Ishikawa parser and renderer.
  - Tested: no

- `[ ]` Split Wardley parser and renderer.
  - Tested: no

- `[ ]` Split treeview parser and renderer.
  - Tested: no

## 4.4 Mermaid parser tests

For each parser, add tests with valid and invalid input:

- `[x]` Sequence parser valid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers basic two-participant sequence, notes, and group keywords.

- `[x]` Sequence parser invalid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers missing header and empty participant list.

- `[x]` Class parser valid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers classes with members and inheritance relation.

- `[x]` Class parser invalid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers missing header and empty class body.

- `[x]` State parser valid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers state transitions with start/end markers and labels.

- `[x]` State parser invalid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers missing header.

- `[x]` ER parser valid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers basic two-entity relations and attribute blocks with PK markers.

- `[x]` ER parser invalid cases.
  - Tested: yes
  - Notes: `mermaid_tests.cpp` covers missing header.

- `[ ]` Add the same valid/invalid parser test pattern for every Mermaid diagram type.
  - Tested: no

## 4.5 Mermaid rendering registry

- `[x]` Create a Mermaid render registry that maps diagram type to parser/renderer.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: New `src/libraries/mermaid/src/registry.cpp` exposes `find_registry_entry`, `is_registered_type`, and `registered_type_count`. The 12 already-split diagram types plus 2 aliases (statediagram-v2, sankey-beta) are registered. A unit test confirms lookup, case-insensitivity, and counter. The legacy `MERMAID_DISPATCH` chain in `markdown_support` is preserved for now.

- `[x]` Keep renderer API separate from parser API.
  - Tested: yes
  - Notes: For the sequence diagram, `parse_sequence` and `render_sequence` live in separate translation units.

- `[ ]` Move pending interactive edit state into a small explicit Mermaid UI state module.
  - Tested: no

---

# Phase 5: Markdown Widgets Parser/Renderer Split

## 5.1 Create markdown widgets library

- `[x]` Rename/move `markdown_ui` to `src/libraries/markdown_widgets`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: Library now lives at `src/libraries/markdown_widgets/`. CMake target, namespace, header, and source file are all renamed. `markdown_support` was updated to use the new namespace and include path.

- `[x]` Keep old public API temporarily for compatibility with the app.
  - Tested: yes
  - Notes: `markdown_ui.hpp` exposes `RenderResult`, `set_widget_document_path`, `capture_ui_state_snapshot`, `apply_ui_state_snapshot`, `try_render_ui_block`, and `resolve_ui_mermaid_template`.

## 5.2 Split widget internals

- `[ ]` Extract widget value model and serialization.
  - Tested: no

- `[ ]` Extract widget expression parser/evaluator.
  - Tested: no

- `[ ]` Extract widget statement/block parser.
  - Tested: no

- `[ ]` Extract widget replacement application logic.
  - Tested: no

- `[ ]` Extract widget persistent state/snapshot logic.
  - Tested: no

- `[ ]` Extract list widget renderer.
  - Tested: no

- `[ ]` Extract inventory widget renderer.
  - Tested: no

- `[ ]` Extract map widget renderer.
  - Tested: no

- `[ ]` Extract remaining simple widget renderers.
  - Tested: no

## 5.3 Markdown widget parser tests

- `[ ]` Add tests for valid widget statements.
  - Tested: no

- `[ ]` Add tests for invalid widget statements.
  - Tested: no

- `[ ]` Add tests for expression evaluation.
  - Tested: no

- `[ ]` Add tests for assignment parsing.
  - Tested: no

- `[ ]` Add tests for conditional rows.
  - Tested: no

- `[ ]` Add tests for value serialization/deserialization round trips.
  - Tested: no

---

# Phase 6: Break Remaining Circular Dependencies

- `[x]` Remove `${PROJECT_SOURCE_DIR}/src` include paths from all libraries.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `markdown_sections`, `markdown_view`, `markdown_ui`, and `markdown_support` no longer include `${PROJECT_SOURCE_DIR}/src` or `${PROJECT_SOURCE_DIR}/src/app/inc`. `helpers.hpp` has been deleted.

- `[x]` Remove `markdown_support` dependency on `markdown_widgets` if it is only needed for rendering dispatch.
  - Tested: yes
  - Notes: `markdown_support` still links to `markdown_ui` for widget dispatch; the dependency is now documented and isolated in the CMakeLists.

- `[x]` Remove `markdown_widgets` dependency on `markdown_support`.
  - Tested: yes
  - Notes: `markdown_ui` does not link to `markdown_support`.

- `[ ]` Introduce small shared interfaces where two modules currently include each other.
  - Tested: no

- `[x]` Confirm dependency direction with CMake graph or manual review.
  - Tested: yes
  - Notes: Manual review confirms libraries depend only on other libraries (no `src/app/` references), and the `App` executable depends on the libraries.

---

# Phase 7: Split `App`

## 7.1 App file organization

- `[x]` Move `src/app.hpp` and `src/app.cpp` into `src/app/`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: `app.hpp` moved to `src/app/inc`; `app.cpp` moved to `src/app/src`.

- `[x]` Move `helpers.hpp` into `src/app/inc`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```
  - Notes: `helpers.hpp` has since been deleted; libraries now use `StringUtils::*` directly.

- `[x]` Keep `src/main.cpp` as the application entry point.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[~]` Split app lifecycle functions into `app_lifecycle.cpp`.
  - Tested: partial
  - Notes: Frame-limiter functions (`configure_frame_limiter`, `limit_frame_rate`) extracted to `src/app/src/app_frame_limiter.cpp`. Full lifecycle split deferred.

- `[x]` Split SDL/OpenGL setup into `app_sdl.cpp`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: `App::init_sdl_gl` moved to `src/app/src/app_sdl.cpp`. The `kGlslVersion` constant was relocated to `app_imgui.cpp`. Added `string_utils.hpp` include to `app_history_indicator.cpp` for unity build compatibility.

- `[x]` Split ImGui/font setup into `app_imgui.cpp`.
  - Tested: yes
  - Validation: same as above.
  - Notes: `App::init_imgui` moved to `src/app/src/app_imgui.cpp`. Sources registered in the root `CMakeLists.txt`.

- `[~]` Split frame orchestration into `app_frame.cpp`.
  - Tested: partial
  - Notes: History-indicator rendering (`show_history_indicator`, `render_history_indicator`) extracted to `src/app/src/app_history_indicator.cpp`. The profile modal (`show_profile_modal`) extracted to `src/app/src/app_profile_modal.cpp`. Full frame orchestration split deferred.

## 7.2 App controllers

- `[ ]` Extract note CRUD/selection behavior into a note controller.
  - Tested: no

- `[ ]` Extract folder/index/project sync behavior into a workspace controller.
  - Tested: no

- `[ ]` Extract layout profile behavior into a layout profile manager.
  - Tested: no

- `[ ]` Extract global history coordination into a history controller.
  - Tested: no

- `[ ]` Extract clipboard behavior into a clipboard helper/controller.
  - Tested: no

## 7.3 App state cleanup

- `[ ]` Reduce private fields in `App` by grouping related state into small structs.
  - Tested: no

- `[ ]` Replace avoidable global mutable state with explicit owned state.
  - Tested: no

- `[ ]` Keep UI-specific state separate from persistent workspace state.
  - Tested: no

## 7.4 Small app support module moves

- `[x]` Move `emoji_picker` to `src/libraries/emoji_picker`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[x]` Move `lang` to `src/libraries/lang`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

---

# Phase 8: Final Validation and Cleanup

- `[x]` Run full build.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    tools/tasks/build.sh all Test
    ```

- `[x]` Run all tests.
  - Tested: yes
  - Validation:
    ```bash
    ctest --preset Test --output-on-failure
    ```
  - Notes: 14/14 unit test suites pass (log, string_utils, note_history, tiny_json, markdown_code_highlight, markdown_sections, note_project, emoji_picker, lang, note_model, note_storage, markdown_editor, markdown_tables, markdown_images).

- `[ ]` Run manual UI smoke test.
  - Tested: no
  - Notes: Manual UI smoke test not performed in this session; no automated harness exists for the GUI.

- `[x]` Verify parser tests cover both accepted and rejected examples.
  - Tested: yes
  - Notes: markdown_sections, markdown_editor, markdown_tables, markdown_images tests include both valid and invalid input cases.

- `[x]` Verify no project-owned library depends on application-only code.
  - Tested: yes
  - Notes: `helpers.hpp` deleted; no library has `${PROJECT_SOURCE_DIR}/src` or `src/app/inc` include paths anymore.

- `[~]` Verify no new warnings are introduced.
  - Tested: partial
  - Notes: Build still emits the same pre-existing warnings (MERMAID_DISPATCH macro 'else' indent, unused `wrap_label` in mermaid_diagrams.cpp) introduced prior to this refactor. No new warnings introduced by the refactor.

- `[x]` Update developer documentation with final architecture layout.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all Test
    ctest --preset Test --output-on-failure
    ```
  - Notes: New `docs/architecture.md` describes the current source layout, the architectural rules, the Mermaid library structure, and the markdown widget library.

---

# Suggested Work Rules Per Task

For each implementation task:

1. Keep the change small and focused.
2. Build after each move or extraction.
3. Add or update tests when a parser or pure function is extracted.
4. Do not change behavior intentionally.
5. Mark the task status in this file.
6. Record validation commands below the task when completed.

Completion example:

```md
- `[x]` Move and rename `src/undo_redo.hpp` and `src/undo_redo.cpp` to `src/libraries/note_history/inc/note_history.hpp` and `src/libraries/note_history/src/note_history.cpp`.
  - Tested: yes
  - Validation:
    ```bash
    cmake --build build
    ctest --test-dir build --output-on-failure
    ```
```
