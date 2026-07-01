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

- `[ ]` Project-owned libraries live under `src/libraries/`.
  - Tested: no
  - Notes: Keep third-party/vendor code in `externals/`.

- `[ ]` Libraries must not depend on `src/app/` or application-only headers.
  - Tested: no
  - Notes: Dependencies should point from app to libraries, not the reverse.

- `[ ]` Public APIs are exposed through small headers in each library.
  - Tested: no
  - Notes: Tests should include only public headers when possible.

- `[ ]` Parsers must not depend on ImGui, SDL, OpenGL, or rendering code.
  - Tested: no
  - Notes: Parser libraries should be unit-test friendly.

- `[ ]` Renderers may depend on ImGui, but should receive parsed models instead of raw parsing state.
  - Tested: no

- `[ ]` No functionality changes without explicit approval.
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

- `[ ]` Record current executable smoke test/manual launch steps.
  - Tested: no
  - Notes: Needed because UI refactors may not be fully covered by unit tests.

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

- `[ ]` Add a project-owned `notepp_options` interface target.
  - Tested: no

- `[ ]` Move project warnings and C++ standard settings to target-based CMake usage.
  - Tested: no

- `[ ]` Ensure vendor/external targets do not inherit project warning policy unnecessarily.
  - Tested: no


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

- `[ ]` Create `src/libraries/note_model`.
  - Tested: no

- `[ ]` Move `App::NoteMeta` into `note_model/note_meta.hpp`.
  - Tested: no

- `[ ]` Move `App::FolderMeta` into `note_model/folder_meta.hpp`.
  - Tested: no

- `[ ]` Move layout model structs into `note_model/layout_profile.hpp` or `note_layout`.
  - Tested: no

- `[ ]` Update `App` to use the extracted model types.
  - Tested: no

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

- `[ ]` Keep UI folder picker code isolated from pure project path/config logic.
  - Tested: no

- `[ ]` Add tests for config path handling and recent-project serialization where possible.
  - Tested: no

## 2.4 Note storage library

- `[ ]` Create `src/libraries/note_storage`.
  - Tested: no

- `[ ]` Extract note path/title helpers from `App` where behavior is pure.
  - Tested: no

- `[ ]` Extract note content cache from `App`.
  - Tested: no

- `[ ]` Add tests for cache update, invalidation, and reload behavior.
  - Tested: no

---

# Phase 3: Markdown Parser/Editor/Testable Libraries

## 3.1 Markdown model library

- `[x]` Move `src/markdown_sections.hpp` and `src/markdown_sections.cpp` to `src/libraries/markdown_model`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[ ]` Remove application-only dependencies from markdown model code.
  - Tested: no

- `[ ]` Add tests for section splitting and heading detection.
  - Tested: no

## 3.2 Markdown editor library

- `[ ]` Create `src/libraries/markdown_editor`.
  - Tested: no

- `[ ]` Move formatting state and text operations from `markdown_support` into `markdown_editor`.
  - Tested: no

- `[ ]` Public API should cover checklist insert, table insert, quote, wrap, color wrap, word bounds, line bounds.
  - Tested: no

- `[ ]` Add valid-operation tests for editor transformations.
  - Tested: no

- `[ ]` Add edge-case tests for empty text, invalid cursor positions, reversed selections, and multiline selections.
  - Tested: no

## 3.3 Markdown tables library

- `[ ]` Create `src/libraries/markdown_tables`.
  - Tested: no

- `[ ]` Move table parsing helpers from `markdown_support` and `markdown_view` into one shared library.
  - Tested: no

- `[ ]` Public API should parse table blocks without ImGui dependency.
  - Tested: no

- `[ ]` Add tests for valid markdown tables.
  - Tested: no

- `[ ]` Add tests for invalid table separators, inconsistent columns, escaped pipes, and empty cells.
  - Tested: no

## 3.4 Markdown preview library

- `[x]` Move `src/markdown_view.hpp` and `src/markdown_view.cpp` to `src/libraries/markdown_preview`.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[ ]` Keep rendering API small and explicit.
  - Tested: no

- `[ ]` Move preview state persistence out of generic markdown support.
  - Tested: no

## 3.5 Markdown images library

- `[ ]` Create `src/libraries/markdown_images`.
  - Tested: no

- `[ ]` Extract image path resolution and cache code from markdown preview.
  - Tested: no

- `[ ]` Extract URL image download code if it remains needed.
  - Tested: no

- `[ ]` Keep OpenGL texture ownership clear and RAII-safe where practical.
  - Tested: no

---

# Phase 4: Mermaid Parser/Renderer Split

## 4.1 Create Mermaid library structure

- `[ ]` Create `src/libraries/mermaid`.
  - Tested: no

- `[ ]` Move `src/mermaid_diagrams.hpp`, `src/mermaid_diagrams.cpp`, `src/mermaid.hpp`, and `src/mermaid.cpp` under `src/libraries/mermaid`.
  - Tested: no

- `[ ]` Update CMake target from current mixed `mermaid` target to clearer Mermaid targets.
  - Tested: no

## 4.2 Define parser result API

- `[ ]` Add a common parser result type.
  - Tested: no
  - Example direction:
    ```cpp
    namespace notepp::mermaid {
    struct ParseError {
      size_t line = 0;
      size_t column = 0;
      std::string message;
    };

    template <class Diagram>
    struct ParseResult {
      Diagram diagram;
      std::vector<ParseError> errors;
      bool ok() const noexcept { return errors.empty(); }
    };
    }
    ```

- `[ ]` Ensure parser APIs do not depend on ImGui.
  - Tested: no

- `[ ]` Existing bool parser functions may remain temporarily as compatibility wrappers.
  - Tested: no

## 4.3 Split parser and renderer files by diagram type

For each diagram type, split into:

```text
<diagram>_diagram.hpp
<diagram>_parser.cpp
<diagram>_renderer.cpp
```

Tasks:

- `[ ]` Split sequence diagram parser and renderer.
  - Tested: no

- `[ ]` Split class diagram parser and renderer.
  - Tested: no

- `[ ]` Split state diagram parser and renderer.
  - Tested: no

- `[ ]` Split ER diagram parser and renderer.
  - Tested: no

- `[ ]` Split journey diagram parser and renderer.
  - Tested: no

- `[ ]` Split gantt diagram parser and renderer.
  - Tested: no

- `[ ]` Split quadrant diagram parser and renderer.
  - Tested: no

- `[ ]` Split requirement diagram parser and renderer.
  - Tested: no

- `[ ]` Split git graph parser and renderer.
  - Tested: no

- `[ ]` Split mindmap parser and renderer.
  - Tested: no

- `[ ]` Split timeline parser and renderer.
  - Tested: no

- `[ ]` Split sankey parser and renderer.
  - Tested: no

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

- `[ ]` Split ZenUML parser and renderer.
  - Tested: no

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

- `[ ]` Sequence parser valid cases.
  - Tested: no

- `[ ]` Sequence parser invalid cases.
  - Tested: no

- `[ ]` Class parser valid cases.
  - Tested: no

- `[ ]` Class parser invalid cases.
  - Tested: no

- `[ ]` State parser valid cases.
  - Tested: no

- `[ ]` State parser invalid cases.
  - Tested: no

- `[ ]` ER parser valid cases.
  - Tested: no

- `[ ]` ER parser invalid cases.
  - Tested: no

- `[ ]` Add the same valid/invalid parser test pattern for every Mermaid diagram type.
  - Tested: no

## 4.5 Mermaid rendering registry

- `[ ]` Create a Mermaid render registry that maps diagram type to parser/renderer.
  - Tested: no

- `[ ]` Keep renderer API separate from parser API.
  - Tested: no

- `[ ]` Move pending interactive edit state into a small explicit Mermaid UI state module.
  - Tested: no

---

# Phase 5: Markdown Widgets Parser/Renderer Split

## 5.1 Create markdown widgets library

- `[ ]` Rename/move `markdown_ui` to `src/libraries/markdown_widgets`.
  - Tested: no

- `[ ]` Keep old public API temporarily for compatibility with the app.
  - Tested: no

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

- `[ ]` Remove `${PROJECT_SOURCE_DIR}/src` include paths from all libraries.
  - Tested: no

- `[ ]` Remove `markdown_support` dependency on `markdown_widgets` if it is only needed for rendering dispatch.
  - Tested: no

- `[ ]` Remove `markdown_widgets` dependency on `markdown_support`.
  - Tested: no

- `[ ]` Introduce small shared interfaces where two modules currently include each other.
  - Tested: no

- `[ ]` Confirm dependency direction with CMake graph or manual review.
  - Tested: no

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
  - Notes: Temporary private include paths were added for libraries that still use `helpers.hpp`; this dependency should be cleaned when splitting code by responsibility.

- `[x]` Keep `src/main.cpp` as the application entry point.
  - Tested: yes
  - Validation:
    ```bash
    tools/tasks/build.sh all develop_gui
    ```

- `[ ]` Split app lifecycle functions into `app_lifecycle.cpp`.
  - Tested: no

- `[ ]` Split SDL/OpenGL setup into `app_sdl.cpp`.
  - Tested: no

- `[ ]` Split ImGui/font setup into `app_imgui.cpp`.
  - Tested: no

- `[ ]` Split frame orchestration into `app_frame.cpp`.
  - Tested: no

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

- `[ ]` Run full build.
  - Tested: no

- `[ ]` Run all tests.
  - Tested: no

- `[ ]` Run manual UI smoke test.
  - Tested: no

- `[ ]` Verify parser tests cover both accepted and rejected examples.
  - Tested: no

- `[ ]` Verify no project-owned library depends on application-only code.
  - Tested: no

- `[ ]` Verify no new warnings are introduced.
  - Tested: no

- `[ ]` Update developer documentation with final architecture layout.
  - Tested: no

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
