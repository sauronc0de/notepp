# Notepp command API

Notepp exposes one newline-delimited JSON command protocol to the GUI command
finder and local clients. The GUI owns the project and processes IPC at a frame
boundary. `notepp-cli` is an IPC client and requires Notepp to be running with
the selected project open.

A request has the form:

```json
{"command":"note.list","args":{}}
```

Responses contain `success: true` and a `result`, or `success: false` with a
structured `error` (`code` and `message`). The legacy `ok`, `method`, and
`arguments` spellings remain accepted for compatibility.

## Note selectors and paths

Commands that operate on an existing note require **exactly one** selector:

- `id`: stable note ID.
- `path`: normalized, project-root-relative path such as
  `notes/inbox/template.md`.
- `name` (or `title`): exact unique note title. Ambiguous titles are rejected;
  use `id` or `path` instead.

Absolute paths, `..` components, symlink escapes, and paths outside the
project's notes root are rejected. This project-root base is also used by
widget commands; it is not relative to the current note.

## Commands

- `app.capabilities`
- `note.list`, `note.get`, `note.create`
- `note.header.list`, `note.header.get`, `note.header.create`
- `note.line.create`
- `note.color.set`
- `note.variable.get`, `note.variable.set`

`note.line.create` appends one non-empty source line to the end of a selected
heading section, before the next heading of the same or higher level:

```json
{"command":"note.line.create","args":{"path":"notes/inbox/template.md","heading":"Tasks","line":"- [ ] Review"}}
```

A missing heading returns `not_found`. Duplicate exact heading titles return
`ambiguous_heading` and leave the file unchanged. `note.header.get` and parent
selection for `note.header.create` follow the same no-ambiguity convention.

Variable commands use the GUI's render-owned persistence adapter and preserve
local-first, global-fallback lookup. A headless API without that adapter returns
`adapter_unavailable`.

## Friendly CLI

Both legacy and note-prefixed variable groups are supported:

```text
notepp-cli --project ROOT note line create --note notes/inbox/template.md --heading Tasks --line "- [ ] Review"
notepp-cli --project ROOT variable set --note NOTE --name status --value '"done"'
notepp-cli --project ROOT note variable set --note notes/inbox/template.md --name status --value '"done"'
```

`--note` is interpreted as a project-root-relative path when it contains a path
separator or file extension; otherwise it is an ID. Use protocol JSON when an
exact `name` selector is required.

## Cross-note widget variables

A UI expression can read the evaluated local and inherited global variables of
another project note as an object:

```text
get_variables(projects/notepp/inbox/template.md).status
get_variables("notes/inbox/template.md").status
```

The path is resolved below the current project root. Absolute paths, missing
notes, and `..` escapes are rejected. Local declarations in the target note
override inherited `.globals.md` declarations.
