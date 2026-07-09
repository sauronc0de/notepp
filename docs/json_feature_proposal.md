# JSON Feature Proposal

> Status: **draft — awaiting user decision**
> Scope: enable users to read and write JSON-shaped data inside markdown notes, and (optionally) round-trip with external `.json` files.

This document is meant to be read top-to-bottom. Sections are short on purpose so you can skim and pick.

---

## 1. Background — what already exists

The project already has significant JSON and structured-data infrastructure. The new feature should build on top of it, not beside it.

### 1.1 Internal JSON infrastructure

| Library | Path | Purpose | Status |
|---------|------|---------|--------|
| `tiny_json` | `src/libraries/tiny_json/` | Hand-rolled text-level parser (`json_find_string`, `json_find_int`, `json_find_float`, `json_find_bool`, `json_array_objects`, `json_escape`, `json_unescape`, `find_matching`). Used to read fields out of JSON blobs that live inside markdown variables. | Existing, in-house, no external deps. |
| `json_io` | `src/libraries/json_io/` | `nlohmann/json` + `Boost.Hana`-based struct serialization (`loadJsonFile`, `saveJsonFile`, `JSON_IO_STRUCT`, `JSON_IO_FIELDS`). Used by the app for its own config files. | Existing, depends on nlohmann/json (already in the dep graph). |

The app already reads and writes three JSON config files:

- `config/notes_index.json`
- `config/layout_profiles.json`
- `config/markdown_preview_state.json`

So "the app knows JSON" is already true — the question is exposing it to the user.

### 1.2 The UI Block System — the real foundation

The most important existing piece is the **UI Block System** in `src/render/markdown_widgets/`. It already has:

- A complete `Value` AST (`ValueKind::Object / Array / String / Number / Bool`) in `markdown_widgets.cpp`.
- A parser that accepts JSON-like literals: `{name:"X", count:3, items:[...], active:true}`.
- A persistence model: **edits made through widgets are written back to the `.md` file** (this is what `list`, `inventory`, `map`, etc. do today).
- An expression language with array indexing, ternaries, conditionals, and built-ins (`len`, `contains`, `empty`).

In other words, the user can already write structured data, render it, edit it through widgets, and have it round-trip into the same markdown file. The literals are *JSON-shaped* but use a relaxed syntax (unquoted keys, optional quotes around string values, comments allowed).

**Implication:** the smallest, safest JSON feature is to add *new widgets* and *a new fence type* on top of this existing model — not a parallel system.

---

## 2. User stories

What we want the user to be able to do, in priority order:

1. **Track structured data inside a note** — contacts, tasks, books, inventory — with an editable, visual widget, and have the data live in the same `.md` file (same behavior as `list`/`inventory`/`map` today).
2. **Browse a JSON snippet visually** — paste a JSON block in a note, toggle a tree view, expand/collapse, copy to clipboard.
3. **Round-trip with an external `.json` file** — the note acts as a UI over a JSON file the user owns, and edits flow back to that file.
4. **Paste strict JSON without editing it** — copy JSON from another tool, drop it in, and have it work as a value in the UI Block System.

---

## 3. Four options

Options are listed from smallest/safest (1) to largest/most powerful (4). They are **stackable** — a natural roadmap is 1+2 first, then 3, then 4.

### 3.1 Option 1 — New widgets on existing UI block values

**What it adds:** four new widgets, each taking any object/array variable and rendering a different view.

- `tree(variable, "Title")` — collapsible tree view of any object/array.
- `table(variable, "Title", ["col1", "col2", ...])` — render an array of objects as an editable table; add/remove rows; edit cells in place.
- `form(variable, "Title")` — auto-generate input fields for every key in an object; save changes back to the variable.
- `json(variable, "Title")` — pretty-printed JSON view with **Copy** and **Save as `.json`…** buttons.

**Example user note:**

```markdown
```ui
contacts([
  {name:"Ana",   role:"Designer",  email:"ana@x.com",   active:true},
  {name:"Bruno", role:"Developer", email:"bruno@x.com", active:true},
  {name:"Cris",  role:"PM",        email:"cris@x.com",  active:false}
])

table(contacts, "Contacts", ["name", "role", "email", "active"])
tree(contacts,  "Contacts (tree view)")
form(contact_edit, "Edit contact")
json(contacts,   "Contacts (JSON)")
```
```

**What the user sees:** an editable table, a collapsible tree, an auto-generated form, and a pretty JSON view with Copy/Save buttons. All edits persist into the same `.md` file like every other widget.

| Pros | Cons |
|------|------|
| Smallest change | Does not produce a standalone `.json` file unless user clicks Save as |
| Reuses the existing `Value` AST — no new model |  |
| Zero new dependencies |  |
| Zero risk of touching `config/*.json` or system paths |  |
| Consistent with how every other widget already works |  |

**Estimated effort:** small (extends one widget file, follows the existing `Statement::Kind` enum pattern).
**Estimated risk:** low.

---

### 3.2 Option 2 — Interactive ` ```json ` fenced code blocks

**What it adds:** a new fenced block type `\`\`\`json` with a small toolbar: **Tree view toggle**, **Copy**, **Save as `.json`…**.

**Example user note:**

````markdown
Here is the latest contacts export:

```json
[
  { "name": "Ana",   "role": "Designer",  "email": "ana@x.com",   "active": true },
  { "name": "Bruno", "role": "Developer", "email": "bruno@x.com", "active": true }
]
```
````

**What the user sees:**

- The snippet is syntax-highlighted using the existing `markdown_code_highlight` machinery.
- A toolbar above the block lets them toggle a tree view (re-uses the `tree` widget from option 1), copy the JSON, or save it to a file.
- The source of truth stays in the note; a `.json` file is only produced when the user explicitly clicks Save as.

| Pros | Cons |
|------|------|
| Natural format for "I have a JSON snippet, let me explore it" | Editing is limited to the tree view unless we also add a JSON-aware text editor |
| Smallest possible user-visible "I have JSON in my note" experience |  |
| Reuses existing `markdown_code_highlight` |  |
| Composes with option 1 (`tree`) |  |

**Estimated effort:** small.
**Estimated risk:** low.

---

### 3.3 Option 3 — External JSON file link

**What it adds:** a widget that loads an external `.json` file into a variable, and a sibling widget that persists it back. The note becomes a UI over a JSON file the user owns.

**Example user note:**

```markdown
```ui
// Load on open, save on close (or debounced)
contacts(jsonfile_load("data/contacts.json"))
jsonfile_save(contacts, "data/contacts.json")
table(contacts, "Contacts", ["name", "role", "email", "active"])
```
```

**What the user gets:** round-trip with an external JSON file. Edits in `table` are written back to that file. The same file can be used from a script, another tool, or another note.

| Pros | Cons |
|------|------|
| Real round-trip with external JSON | Path handling must be careful (relative to what?) |
| Note acts as a "view" — file is the source of truth | Save conflicts if another tool edits the file |
| Great for shared / scripted data | Risk of clobbering `config/*.json` if path is too permissive |
| Composes with option 1 widgets (`table`, `form`, `tree`) | Needs a save policy (debounce vs. on-close vs. explicit button) |

**Open design questions to resolve before implementing:**

- Path resolution: relative to the note folder, a global data dir, or both?
- Save policy: debounced, on-close, or explicit button only?
- Safety: must the path be restricted to the note folder, or can the user pick any path with a confirmation prompt?

**Estimated effort:** medium (parser wiring + save policy + path resolution + safety).
**Estimated risk:** medium.

---

### 3.4 Option 4 — Strict JSON literals in UI blocks

**What it adds:** the UI block parser also accepts strict JSON. Today the literals are relaxed (`{name:"X"}`); with this option, `{"name": "X"}` would also work, so users can paste JSON from external tools directly into a UI block.

**Example user note (with option 4):**

```markdown
```ui
contacts([
  {"name": "Ana",   "role": "Designer",  "email": "ana@x.com",   "active": true},
  {"name": "Bruno", "role": "Developer", "email": "bruno@x.com", "active": true}
])
list(contacts, "Contacts", 260, true)
```
```

| Pros | Cons |
|------|------|
| Zero-friction interop with external tools | Parser change touches the heart of the UI block system |
| Removes a papercut for power users | Subtle regressions in existing notes are possible |
|  | Does not unlock new use cases on its own — pure quality-of-life |

**Estimated effort:** medium.
**Estimated risk:** medium.

---

## 4. Recommended roadmap

| Iteration | Options | Adds | Effort | Risk |
|-----------|---------|------|--------|------|
| **MVP** | 1 + 2 | `tree`, `table`, `form`, `json` widgets + interactive ` ```json ` fenced blocks | Small | Low |
| **v2** | + 3 | `jsonfile_load` / `jsonfile_save` widgets (external file round-trip) | Medium | Medium |
| **v3** | + 4 | Strict JSON literals in the UI block parser | Medium | Medium |

**Why this order:**

- 1 + 2 give the user real read/write UX for JSON-shaped data inside notes, with **no new file format, no new dependencies, no new paths, and no risk of touching `config/*.json`**.
- 3 is genuinely more work and more risk, but it is the right next step once the widget design is proven.
- 4 is quality-of-life and is best done last, so the parser changes have the most test coverage behind them.

---

## 5. Trade-off summary

| If your main use case is… | You want… |
|---------------------------|-----------|
| "I want to track structured data inside my notes" | **Option 1** (MVP core) |
| "I want to paste a JSON snippet and explore it" | **Option 2** (add to MVP) |
| "My JSON already lives in a file outside the app, I want a UI for it" | **Option 3** (v2) |
| "I copy/paste JSON in and out of my notes all day" | **Option 4** (v3) |

---

## 6. Decision required

Please confirm the answers below so I can produce a tight, safe implementation plan. Each question lists a recommended default.

### 6.1 Scope — which options for the first iteration?

- [ ] **Option 1 only** — new widgets only.
- [ ] **Option 1 + 2 (Recommended)** — new widgets + interactive `json` code blocks.
- [ ] **Option 1 + 2 + 3** — also add external file link.
- [ ] **All four** — also make UI block literals accept strict JSON.

### 6.2 Persistence — where do edits go?

- [ ] **Back into the `.md` note (Recommended)** — same behavior as `list`/`inventory`/`map`.
- [ ] **Back into the `.md` note + offer "Save as `.json`"** — explicit export.
- [ ] **Only to an external `.json` file** — requires option 3.

### 6.3 Edit mode — read-only or read+write?

- [ ] **Read + write (Recommended)** — full editor widgets.
- [ ] **Read-only views + Copy/Export** — browsing only.
- [ ] **Mix: tree read-only, table/form read+write** — balanced.

### 6.4 Constraints — implementation guardrails

- [ ] **Reuse `tiny_json` / `json_io`, no new dependencies (Recommended).**
- [ ] Adding `nlohmann/json` for the tree renderer is OK (it is already a transitive dep).
- [ ] Must not touch the existing UI Block parser (limits what new widgets can do).
- [ ] For option 3: external files must be inside the note folder (safe by default).
- [ ] For option 3: user can pick any path with a confirmation prompt (more powerful, more risk).

---

## 7. What I will deliver once you decide

When you confirm the answers above, I will (as Orchestrator):

1. Produce a small, focused plan (files to touch, functions to add, tests to write).
2. Hand it to the **Code Implementer** for implementation.
3. Hand the diff to the **Reviewer** for a quality check.
4. Verify against `docs/verification.md` before reporting back.

No code will be written until you pick.
