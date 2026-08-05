# Notepp command API

Notepp exposes one newline-delimited JSON command protocol to the GUI command
finder and to local clients. The GUI owns the project and processes IPC at a
frame boundary, so commands never mutate notes from an IPC worker thread.

On Unix the endpoint is a per-user, per-project Unix domain socket in `/tmp`.
Windows uses the corresponding named-pipe endpoint through the same client and
server API. `notepp-cli` is an IPC client only: it does not open project files
and requires Notepp to be running.

A request has the form:

```json
{"command":"note.list","args":{}}
```

Responses contain `success: true` and a `result`, or `success: false` with a
structured `error` (`code` and `message`). The legacy `ok` field is also
emitted. Request IDs are echoed by IPC. `app.capabilities` lists the
registered commands:

- `app.capabilities`
- `note.list`, `note.get`, `note.create`
- `note.header.list`, `note.header.get`, `note.header.create`
- `note.color.set`
- `note.variable.get`, `note.variable.set`

Variable commands are executed by the GUI's render-owned persistence adapter,
which reuses the markdown widget declaration parser and persistence rules. A
CLI request is still handled by the GUI over IPC; a direct headless API without
an adapter returns a structured `adapter_unavailable` error.
