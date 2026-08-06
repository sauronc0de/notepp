# Notes App

## Introduction

![title](assets/icon/notepp.ico)

![Version](https://img.shields.io/badge/version-0.0.9-green)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-orange)

A **C++ desktop note-taking app** powered by Dear ImGui. Notes are stored as plain **Markdown files**, organized in folders, and enriched with an interactive UI block system, drawings, and rich formatting.

---

## Getting Started / Demo

At build time, this root README is embedded in the application. On a first run with no notes, it becomes the default in-app `demo` note. The checked-in `data/demo.md` mirrors it for the development demo.

---

## Table of Contents

- [Getting Started / Demo](#getting-started--demo)
1. [Interface Overview](#1-interface-overview)
2. [Folders & Notes](#2-folders--notes)
3. [Editing & Markdown](#3-editing--markdown)
   - [Emoji Picker](#emoji-picker)
4. [HTML Compatibility](#4-html-compatibility)
5. [Toolbar Reference](#5-toolbar-reference)
6. [Text Color Syntax](#6-text-color-syntax)
7. [Drawing Tools](#7-drawing-tools)
8. [Images & Fonts](#8-images--fonts)
9. [Search](#9-search)
10. [Detached Windows](#10-detached-windows)
11. [Undo / Redo](#11-undo--redo)
12. [Keyboard Shortcuts](#12-keyboard-shortcuts)
13. [UI Block System](#13-ui-block-system)
    - [Variables](#131-variables)
    - [Widgets](#132-widgets) (`text`, `int`, `slider`, `bar`, `checkbox`, `enum`, `multicheck`, `list`, `inventory`, `button`, `map`)
    - [Expressions & Operators](#133-expressions--operators)
    - [Built-in Functions](#134-built-in-functions)
    - [Conditionals](#135-conditionals)
    - [Label Color Syntax](#136-label-color-syntax)
    - [Global Variables](#137-global-variables)
    - [Full Examples](#138-full-examples)
14. [Reactive Diagrams (ui-mermaid)](#14-reactive-diagrams-ui-mermaid)
15. [Mermaid Diagrams](#15-mermaid-diagrams)
    - [Kanban Board](#1518-kanban-board)
    - [Linked-note cards](#linked-note-cards)
    - [Mermaid event commands](#mermaid-event-commands)
    - [Diagram families](#15-mermaid-diagrams) — flowcharts, sequence/class/state diagrams, charts, planning, and architecture
16. [Embedded Terminal](#16-embedded-terminal)

---

## 1. Interface Overview

```
┌──────────────────────────────────────────────────────┐
│  Toolbar  (formatting, drawing, layout controls)     │
├──────────────┬───────────────────────────────────────┤
│              │                                       │
│   Sidebar    │         Editor / Preview              │
│              │                                       │
│  • Folders   │   Write markdown on the left,        │
│  • Notes     │   rendered preview on the right.     │
│  • Images    │                                       │
│  • Fonts     │                                       │
│              │                                       │
└──────────────┴───────────────────────────────────────┘
```

- **Sidebar** — left panel: folder tree, note list, image browser, font browser.
- **Editor** — center/right: markdown editor with live preview.
- **Toolbar** — top: all formatting, drawing, and layout buttons.
- **Floating windows** — notes can be detached into independent draggable panels.

---

## 2. Folders & Notes

### Creating

| Action | How |
|--------|-----|
| New folder | Right-click in the sidebar folder area → **New Folder** |
| New note | Right-click a folder → **New Note**, or use the `+` button |
| Rename | Right-click the folder or note → **Rename**, or select and press **F2** |
| Delete | Right-click → **Delete** |

### Organizing

- **Colors** — right-click a note → **Color** to assign a custom RGB color; folders have their own accent color.
- **Copy / Paste notes** — right-click → **Copy**, then right-click destination folder → **Paste**.
- **Hidden notes** — notes can be toggled hidden from the context menu.

### Selecting multiple notes

Hold **Ctrl + Click** to select several notes in the sidebar for batch operations.

---

## 3. Editing & Markdown

Each note is a `.md` file. The editor supports full UTF-8 text including emoji ✅.

### Supported Markdown

| Syntax | Result |
|--------|--------|
| `# Heading 1` … `###### Heading 6` | Collapsible section headers |
| `**bold**` | **bold** |
| `*italic*` | *italic* |
| `~~strike~~` | ~~strikethrough~~ |
| `` `code` `` | inline code |
| ```` ```lang … ``` ```` | syntax-highlighted code block |
| `- item` / `1. item` | bullet / numbered list |
| `- [ ] task` / `- [x] done` | interactive task checkboxes |
| `[text](url)` | clickable link |
| `![alt](path)` | embedded image |
| `> quote` | blockquote |
| `| col | col |` | table |

### Emoji Picker 

Press **Ctrl+.** while editing to insert emoji.

| Platform | Behaviour |
|----------|-----------|
| **Linux** | Opens a built-in floating panel with ~380 emoji |
| **Windows** | Triggers the native OS emoji panel (Win+.) |

**Built-in picker (Linux):**

- **Search bar** — type any keyword (e.g. `fire`, `cat`, `heart`) to filter across all categories.
- **Category tabs** — click a tab icon to browse Smileys, People, Animals, Food, Travel, Objects, Nature, or Symbols.
- Click any emoji to insert it at the cursor and close the picker.
- Press **Ctrl+.** again or click **×** to close without selecting.

---

### Markdown Preview

Click the **Preview** toggle in the toolbar to switch to rendered view. Section headers become collapsible — click them to expand or collapse. All section states are saved between sessions.

---

## 4. HTML Compatibility

Notes support a subset of HTML inline tags alongside standard Markdown. These can be mixed freely with Markdown syntax.

### Supported Tags

| Tag | Example | Result |
|-----|---------|--------|
| `<br>` | `line one<br>line two` | Hard line break |
| `<kbd>` | `<kbd>Ctrl</kbd>+<kbd>Z</kbd>` | Keyboard key chip |
| `<mark>` | `<mark>highlighted text</mark>` | Yellow highlight |
| `<sup>` | `x<sup>2</sup>` | Superscript |
| `<sub>` | `H<sub>2</sub>O` | Subscript |
| `<img>` | `<img src="logo.png" width="120">` | Image with custom size |
| `<u>` | `<u>underlined</u>` | Underline |
| `<hr>` | `<hr>` | Horizontal rule |

### `<kbd>` — Keyboard keys

Render keyboard shortcuts with a styled key cap:

```markdown
Press <kbd>Ctrl</kbd>+<kbd>S</kbd> to save.
Use <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Y</kbd> for undo/redo.
```

### `<mark>` — Highlight

Draw attention to important text:

```markdown
Remember: <mark>always commit before merging</mark>.
```

### `<sup>` / `<sub>` — Scripts

Superscript for exponents and footnotes, subscript for formulas:

```markdown
E = mc<sup>2</sup>
H<sub>2</sub>O is water.
Area = πr<sup>2</sup>
```

### `<img>` — Images with custom dimensions

Standard Markdown images auto-scale to the note width. Use `<img>` when you need a specific size:

```markdown
<img src="diagram.png" width="400">
<img src="icon.png" width="32" height="32">
<img src="https://example.com/banner.png" width="600">
```

Both `width` and `height` are optional. If only one is given, the other scales proportionally. Remote URLs follow the same background-download behaviour as Markdown images.

### Inline image rows

Multiple small images (height ≤ 48 px) on adjacent lines automatically flow inline, making badge rows easy to write:

```markdown
![Version](https://img.shields.io/badge/version-0.0.2-green)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-orange)
```

A **single newline** between images keeps them on the same row. A **blank line** starts a new paragraph on a separate line.

---

## 5. Toolbar Reference

### Edit mode toolbar

| Icon | Button | Action |
|------|--------|--------|
| ![bold](assets/icons/bold.png) | **Bold** | Wraps selection in `**…**` |
| ![italic](assets/icons/italic.png) | **Italic** | Wraps selection in `*…*` |
| ![strike](assets/icons/strike.png) | **Strikethrough** | Wraps selection in `~~…~~` |
| ![note](assets/icons/note.png) | **Note Quote** | Inserts a styled blockquote |
| ![color](assets/icons/color-brush.png) | **Color Text** | Opens color picker, wraps selection in `[color=#RRGGBB]…[/color]` |
| ![todo](assets/icons/to-do-list.png) | **Task List** | Inserts `- [ ] ` task item |
| ![table](assets/icons/table.png) | **Table** | Opens dialog — choose rows & columns, inserts Markdown table |
| ![widgets](assets/icons/widgets.png) | **UI Block** | Inserts a new `ui` code fence |
| ![find](assets/icons/find.png) | **Find** | Opens search panel (Ctrl+F) |

### Drawing / Preview mode toolbar

| Icon | Button | Action |
|------|--------|--------|
| ![cursor](assets/icons/cursor.png) | **Mouse mode** | Standard cursor, no drawing |
| ![pencil](assets/icons/pencil.png) | **Draw mode** | Free-hand drawing |
| ![erase](assets/icons/erase.png) | **Erase mode** | Erase drawn strokes |
| ![delete](assets/icons/delete-bin.png) | **Clear drawing** | Removes all strokes in this folder |
| ![show](assets/icons/show.png) / ![hide](assets/icons/hide.png) | **Show/Hide drawing** | Toggle drawing layer visibility |
| ![show-grid](assets/icons/show-grid.png) / ![hide-grid](assets/icons/hide-grid.png) | **Show/Hide grid** | Toggle alignment grid overlay |

### Layout toolbar

| Icon | Button | Action |
|------|--------|--------|
| ![lock](assets/icons/lock.png) / ![unlock](assets/icons/unlock.png) | **Lock / Unlock** | Lock note window positions to prevent accidental moves |
| ![focus](assets/icons/focus.png) | **Reset positions** | Resets all floating note windows to default layout |
| ![detach](assets/icons/detach.png) | **Detach windows** | Toggle floating detached note windows on/off |
| ![refresh](assets/icons/refresh.png) | **Refresh directory** | Rescans the current folder for new files |

---

## 6. Text Color Syntax

Apply color to any text inside a note (editor or UI block labels):

```
[color=#RRGGBB]colored text[/color]
[color=#RRGGBBAA]text with alpha[/color]
```

**Examples:**

```markdown
[color=#FF4444]Red alert![/color]
[color=#00CC88]Success message[/color]
[color=#3399FF80]Translucent blue[/color]
```

The color picker in the toolbar generates this syntax automatically for selected text.

---

## 7. Drawing Tools

Drawings are stored **per folder** (shared across all notes in the folder) and overlaid on the note view.

### Workflow

1. Switch to **Preview** mode (drawings are visible in preview).
2. Click the **Draw** ![pencil](assets/icons/pencil.png) button.
3. Draw freely with your mouse or stylus.
4. Use **Erase** ![erase](assets/icons/erase.png) to remove strokes.
5. Pick a color from the preset palette or open the custom color picker.

### Controls

| Control | Action |
|---------|--------|
| Draw button + drag | Create a stroke |
| Erase button + drag | Erase strokes under cursor |
| Color preset buttons | Switch stroke color (Red, Orange, Green, Blue, Purple, White) |
| Custom color picker | Pick any RGB color |
| Stroke thickness | Adjustable slider (default 2.2) |
| Ctrl+Z | Undo last stroke |
| Ctrl+Y | Redo stroke |
| Clear drawing | Delete all strokes in the folder |

---

## 8. Images & Fonts

### Images

- **Supported formats:** PNG, JPG, JPEG, GIF, BMP, WebP
- **Embed in markdown:** `![alt text](filename.png)` — path relative to the note folder
- **Image browser:** The sidebar lists all images in the current folder
- **Drag & drop:** Drag an image from the sidebar onto the note text to insert it
- **Right-click image in sidebar → Reveal in File Explorer**

### Right-click an image in preview

Right-clicking any rendered image opens a context menu with two actions:

| Action | Description |
|--------|-------------|
| **Copy image text** | Copies the original source line (`![alt](path)` or `<img …>`) to the clipboard, ready to paste into another note |
| **Edit size…** | Opens a dialog to set width and height |

**Edit size dialog:**

- **Width / Height** — pixel values for the rendered size.
- **Proportional** checkbox — when checked, height is computed automatically from the aspect ratio and only `width=` is stored. Uncheck to set both dimensions independently.
- Clicking **Apply** replaces the image reference with an `<img>` tag using the chosen dimensions. Markdown syntax (`![](…)`) is automatically converted to HTML in the process.

### Fonts

- **Supported:** TTF and OTF files
- **Add a font:** Drop a font file into the folder, or use the font browser
- **Apply to a note:** Drag a font from the sidebar onto the note
- **Font size:** Ctrl+Scroll while hovering a note to resize text
- **Per-note:** Each note keeps its own font and size independently

---

## 9. Search

Open current-note search with **Ctrl+F** or the ![find](assets/icons/find.png) toolbar button. Use **Ctrl+Shift+F** to search the full project.

- **Ctrl+F** searches the active note's content
- **Ctrl+Shift+F** searches note titles, file paths, and full note content across the project
- Results show the matching line with a snippet preview
- Click a result to jump directly to that note

---

## 10. Detached Windows

Click ![detach](assets/icons/detach.png) to enable floating note windows. Each note opens in its own movable panel.

| Feature | How |
|---------|-----|
| Enable/disable | Detach button in toolbar |
| Move windows | Drag the title bar |
| Lock positions | ![lock](assets/icons/lock.png) Lock button — prevents accidental moves |
| Reset layout | ![focus](assets/icons/focus.png) Reset positions button |
| Always on top | Right-click a floating note window title bar |

---

## 11. Undo / Redo

A unified history tracks text edits, note selection, layout changes, preview state, and drawings.

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+Shift+Z | Redo (alternative) |

Undo groups character-level changes into word-granular chunks for convenient reversal.

---

## 12. Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y / Ctrl+Shift+Z | Redo |
| Ctrl+N | Create a new note in the active folder |
| Ctrl+W | Hide the focused note |
| Ctrl+Shift+W | Hide visible notes in the active folder |
| Ctrl+Tab / Ctrl+Shift+Tab | Focus next / previous visible note |
| Ctrl+E | Quick Open note or heading |
| Ctrl+F | Search the active note |
| Ctrl+Shift+F | Search the full project |
| Ctrl+Shift+P | Open Command Finder |
| Ctrl+ñ | Show / hide the embedded terminal |
| Ctrl+Shift+ñ | Show the terminal and open a new terminal tab |
| Ctrl+Tab / Ctrl+Shift+Tab | Focus next / previous terminal tab when visible |
| Ctrl+Enter | Preview: edit active note; Edit: open Editor Actions |
| Esc | Return from Edit mode to Preview |
| F6 | Leave terminal focus and return to the active note or sidebar |
| Ctrl+. | Open emoji picker (Linux) / native emoji panel (Windows) |
| Ctrl+Scroll | Change font size (hover over note) |
| Ctrl+Click | Multi-select notes in sidebar |
| F2 | Rename selected note or folder |
| Double-click word | Select entire word |
| Right-click | Context menus everywhere |

---

## 13. UI Block System

Embed **interactive widgets** directly inside any note using a fenced code block with the `ui` language tag.

````markdown
>```ui
>variableName(initialValue)
>widget(variable, "Label", width, ...options)
>```
````

Widget state is **persistent** — values survive edits, closing, and reopening. Changing the markdown source resets only the blocks that actually changed.

Lines starting with `//` are treated as comments and ignored.

---

### 13.1 Variables

Declare variables on their own line: `name(expression)`

```
count(10)
name("Alice")
active(true)
items(["sword", "shield", "potion"])
```

**Computed variables** reference other variables:

```
count(5)
doubled(count * 2)
label("Total: ")
```

**Types supported:** number (integer or float), string, boolean, array, object.

---

### 13.2 Widgets

Every widget goes on its own line (or multiple widgets on the same line, separated by spaces — they render inline on the same row).

---

#### `text` — Display or edit text

**Display (read-only):**
```
text("static string")
text(variable)
```

**Input field (editable):**
```
text(variable, "Label", width)
text(variable, "Label", width, "Tooltip text")
```

| Arg | Description |
|-----|-------------|
| variable | Variable to display/edit |
| `"Label"` | Label shown next to field (supports `[color=…]`) |
| width | Width in pixels |
| `"Tooltip"` | Optional tooltip on hover |

**Example:**
```
name("World")
text(name, "Name", 160, "Enter your name")
text("Hello, ") text(name) text("!")
```

---

#### `int` — Integer input

```
int(variable, "Label", width)
int(variable, "Label", width, true)
```

The optional 4th argument enables step buttons (+/-).

**Example:**
```
score(0)
int(score, "Score", 100, true)
```

---

#### `slider` — Numeric slider

```
slider(variable, "Label", width, min, max)
```

| Arg | Description |
|-----|-------------|
| variable | Numeric variable |
| `"Label"` | Label |
| width | Width in pixels |
| min | Minimum value |
| max | Maximum value |

**Example:**
```
volume(50)
slider(volume, "Volume", 200, 0, 100)
```

---

#### `bar` — Read-only progress bar

```
bar(variable, "Label", width, min, max)
bar(variable, "Label", width, min, max, "#RRGGBB")
```

| Arg | Description |
|-----|-------------|
| variable | Numeric variable (display only — never editable) |
| `"Label"` | Label |
| width | Width in pixels |
| min | Minimum value |
| max | Maximum value |
| `"#RRGGBB"` | Optional fill color (hex, default green). Supports alpha: `#RRGGBBAA` |

**Example:**
```
hp(75)
mp(40)
bar(hp, "HP", 200, 0, 100, "#e03030")
bar(mp, "MP", 200, 0, 100, "#3060e0")
```

---

#### `checkbox` — Boolean toggle

```
checkbox(variable, "Label")
```

**Example:**
```
enabled(true)
checkbox(enabled, "Enable feature")
```

---

#### `enum` — Dropdown selector

```
enum(variable, "Label", width, ["Option1", "Option2", "Option3"])
```

Stores the **selected string** into the variable.

**Example:**
```
mode("Home")
enum(mode, "Context", 140, ["Home", "Work", "Ideas", "Archive"])
```

---

#### `multicheck` — Multi-select checkboxes

```
multicheck(variable, "Label", width, ["Option1", "Option2", "Option3"])
```

Stores the **selected items as an array**.

**Example:**
```
tags(["important"])
multicheck(tags, "Tags", 200, ["important", "urgent", "later", "daily"])
```

---

#### `list` — Scrollable item list

```
list(variable, "Label", width, selectable)
```

`variable` must be an **array of objects**, each with a `name` field (and optionally a `tooltip` field).

```
items([
  {name:"Sword",   tooltip:"Basic weapon"},
  {name:"Shield",  tooltip:"Blocks attacks"},
  {name:"Potion",  tooltip:"Restores health"}
])
list(items, "Inventory", 220, true)
```

`selectable: true` enables click-to-select and drag-to-reorder.

---

#### `inventory` — Icon grid

```
inventory(variable, "Label", width, rows, cols[, cell_size, popup_width, selection_mode, selected_cells])
```

`variable` is an **object** containing an `items` array of slot objects. `rows` and `cols` belong to the `inventory()` invocation, not the data object. The optional arguments set the cell size, hover popup width, selection mode (`0` none, `1` single, `2` multi), and selected cells (`"x,y;x,y;..."`).

Each slot supports:

| Field | Type | Description |
|-------|------|-------------|
| `title` / `name` | string | Item title shown on hover (`title` takes precedence) |
| `image` | string | Image path (relative, absolute, or filename from `assets/icons/`) |
| `tooltip` | string | Hover tooltip text |
| `quantity` | number or string | Badge value shown in the slot corner |
| `color` | string `#RRGGBB` or `#RRGGBBAA` | Corner mark color |
| `enabled` | bool | Grayed-out when `false` |
| `x`, `y` | number | Required non-negative zero-based coordinates for each visible authored slot; legacy `position` is also accepted |

**Example:**
```
backpack({
  items:[
    {title:"Sword", image:"sword.png", tooltip:"Iron sword", quantity:1, x:0, y:0},
    {name:"Coin", image:"coin.png", tooltip:"Gold coin", quantity:42, color:"#FFD700", x:1, y:0},
    {name:"Potion", image:"potion.png", tooltip:"Health pot", enabled:false, x:2, y:0}
  ]
})
inventory(backpack, "Backpack", 300, 2, 4, 48, 320, 1, "1,0")
```

---

#### `button` — Action button

```
button("Label", width, variable=value)
button("Label", width, command("terminal command"))
```

Clicking the button either **assigns a value to a variable** or sends a command to Notepp's embedded terminal. A command argument may be any expression that evaluates to a string. The terminal opens automatically, creates a shell session in the notes data directory when needed, and shows the command output. Commands execute only when the button is clicked.

Multiple assignments separated by commas are not currently supported — use one assignment per button. Only use command buttons in notes you trust, because they execute with your user permissions.

**Example:**
```
count(0)
int(count, "Count", 80, true)
button("Reset", 80, count=0)
button("Max",   80, count=100)
button("List files", 100, command("ls"))
```

---

---

#### `map` — Interactive annotated map

Displays an image as a zoomable, pannable map canvas. Supports named markers with hover popups, free-hand stroke annotations, and persistent view state — all stored inside the bound variable.

```
map(variable, width, height)
```

| Arg | Description |
|-----|-------------|
| variable | Object variable that holds the full map state |
| width | Widget width in pixels |
| height | Widget height in pixels |

##### Variable fields

| Field | Type | Description |
|-------|------|-------------|
| `image` | string | Path to the map background image (relative to note folder) |
| `zoom` | number | Zoom level — `1` shows the full image, `2` shows 2× detail (min 1, max 50) |
| `offset_x` | number | Horizontal pan offset (fraction of image width; 0 = centered) |
| `offset_y` | number | Vertical pan offset (fraction of image height; 0 = centered) |
| `markers` | array | Array of marker objects (see below) |
| `strokes` | array | Array of drawn stroke objects (see below) |

##### Marker fields

Each entry in `markers` is an object:

| Field | Type | Description |
|-------|------|-------------|
| `x` | number | Horizontal position on the image (0 = left edge, 1 = right edge) |
| `y` | number | Vertical position on the image (0 = top edge, 1 = bottom edge) |
| `color` | string `#RRGGBB` | Circle color (default gold when omitted) |
| `title` | string | Title shown in hover popup and as the initial letter on the pin |
| `image` | string | Image path shown at the top of the hover popup (200×200 px) |
| `description` | string | Markdown text shown in the hover popup body |

##### Stroke fields (drawn annotations)

Each entry in `strokes` is written automatically by the draw tool — you rarely need to edit these by hand:

| Field | Type | Description |
|-------|------|-------------|
| `pts` | string | Space-separated `x,y` pairs in image coordinates, e.g. `"0.12,0.34 0.13,0.35 …"` |
| `color` | string `#RRGGBB` | Stroke color |
| `t` | number | Stroke thickness (UV fraction; scales with zoom so the line stays proportional to the image) |

##### Mouse & keyboard interactions

| Interaction | Effect |
|-------------|--------|
| **Scroll wheel** | Zoom in / out centered on the cursor |
| **Left-click + drag** on canvas | Pan the map view |
| **Left-click + drag** on a marker | Move the marker to a new position (released = saved) |
| **Hover** over marker | Show popup with image, title, and description |
| **Right-click** on marker | Open marker editor (title, image, description, color, position) |
| **Right-click** on canvas | Open context menu (image path, zoom controls, draw mode, add marker) |
| **Escape / click outside** | Close editor popup and discard unsaved changes |
| **Enter** in marker editor | Apply changes (same as clicking Apply) |

##### Context menu — right-click on canvas

- **Image path** — set or change the background image.
- **Zoom +** / **Zoom −** / **Reset view** — adjust zoom and pan programmatically.
- **Draw mode** (checkbox) — enables free-hand stroke drawing; left-click and drag to paint. While active, panning is disabled.
  - **Stroke color** — color picker for new strokes.
  - **Size px** — brush width in screen pixels at the current zoom (strokes scale with the image when you zoom).
  - **Undo last stroke** — removes the most recently drawn stroke.
  - **Clear strokes** — removes all strokes from the map.
- **Erase mode** (checkbox) — hold left mouse button and sweep the eraser circle over strokes to remove them. Mutually exclusive with Draw mode.
  - **Eraser px** — eraser circle radius in screen pixels.
- **Add marker here** — opens the marker editor pre-filled with the click position.

##### Marker editor fields

| Field | Description |
|-------|-------------|
| Title | Short name; first character shown on the pin circle |
| Image | Path to an image shown in the hover popup |
| Description | Markdown body text shown in the hover popup |
| X / Y | Precise image-space position (0–1); editable as floats |
| Color | Color picker (click the swatch to open the wheel; preset buttons for quick picks) |
| Delete | Removes the marker |

**Example:**

````markdown
```ui
dungeon_map({
  image: "dungeon.jpg",
  zoom: 1.0,
  offset_x: 0.0,
  offset_y: 0.0,
  markers: [
    {
      x: 0.25, y: 0.40,
      color: "#FF6B6B",
      title: "Boss Room",
      description: "**Lair of the Dragon**\n\nLevel 15 encounter. Treasure hoard nearby."
    },
    {
      x: 0.70, y: 0.20,
      color: "#57A7FF",
      title: "Safe Zone",
      image: "camp.png",
      description: "Rest here to recover HP."
    }
  ],
  strokes: []
})
map(dungeon_map, 700, 450)
```
````

The markers, strokes, zoom, and pan offset are all **persisted** in the note. Zoom and pan changes are written after the gesture ends (on mouse release for panning; after a brief settle period for scroll-wheel zoom) to avoid layout flicker.

---

### 13.3 Expressions & Operators

Expressions are used in variable declarations and `if()` conditions.

| Operator | Description | Example |
|----------|-------------|---------|
| `+` `-` `*` `/` | Arithmetic | `count * 2 + 1` |
| `==` `!=` | Equality | `mode == "Work"` |
| `>` `<` `>=` `<=` | Comparison | `score >= 100` |
| `&&` | Logical AND | `active && score > 0` |
| `\|\|` | Logical OR | `a == 1 \|\| b == 2` |
| `!` | Logical NOT | `!empty(name)` |
| `-value` | Negation | `-count` |
| `array[i]` | Array index (0-based, negative counts from end) | `tags[0]`, `items[-1]` |

**Truthiness rules:**
- Number: `true` if non-zero
- String: `true` if non-empty
- Array/Object: `true` if non-empty
- Bool: as-is

---

### 13.4 Built-in Functions

| Function | Signature | Description |
|----------|-----------|-------------|
| `len` | `len(str\|array)` | Length of a string or array |
| `contains` | `contains(str, substr)` or `contains(array, value)` | Substring check or array membership |
| `starts_with` | `starts_with(str, prefix)` | True if string starts with prefix |
| `ends_with` | `ends_with(str, suffix)` | True if string ends with suffix |
| `empty` | `empty(value)` | True if value is falsy / empty |

**Examples:**
```
if(len(notes) > 0) {
  text("Has notes")
}
if(contains(tags, "urgent")) {
  text("[color=#FF4444]URGENT[/color]")
}
if(starts_with(name, "Dr.")) {
  text("Doctor detected")
}
if(!empty(description) && len(description) <= 200) {
  text(description)
}
```

---

### 13.5 Conditionals

Show or hide any group of widgets based on a condition:

```
if(condition) {
  widget(...)
  widget(...)
}
```

- The `{` must be on the **same line** as `if(...)`.
- The closing `}` must be on its **own line**.
- Conditions use the full expression syntax from [13.3](#133-expressions--operators).
- Conditionals can reference any declared variable.

**Conditional assignments** — assign a value to a variable when a condition is true:

```
lvl(0)
xp(300)
if(xp >= 300) {
  lvl = lvl + 2
}
text(lvl)
```

Compound assignment shorthand: `lvl += 2` equals `lvl = lvl + 2`. Supported: `+=` `-=` `*=` `/=`.

> Assignments inside `if` blocks are **persistent** — every frame the condition is true, the new value is written back to the note. Use this for conditional state transitions, and `button()` for one-shot changes.

**Ternary expressions** — choose a value inline without a full `if` block:

```
variable = condition ? expressionTrue : expressionFalse
```

Works inside any expression, including declarations:

```
save_charisma(charisma_mod + (has_prof ? proficiency_bonus : 0))
label(score > 90 ? "Excellent" : "Average")
```

Ternaries can be nested and are right-associative: `a ? b : c ? d : e` evaluates as `a ? b : (c ? d : e)`.

**Examples:**
```
level(1)
slider(level, "Level", 160, 1, 10)

if(level >= 5) {
  text("[color=#FFD700]Gold rank unlocked[/color]")
}
if(level >= 10) {
  text("[color=#FF4444]MAX LEVEL[/color]")
}
```

```
mode("off")
enum(mode, "Mode", 120, ["off", "easy", "hard"])

if(mode == "easy") {
  slider(health, "Health", 140, 1, 100)
}
if(mode == "hard") {
  text("[color=#FF0000]No health bar — good luck[/color]")
}
```

---

### 13.6 Label Color Syntax

Widget labels support inline color formatting:

```
text("[color=#00CC88]Status[/color]", 120)
checkbox(done, "[color=#AAAAAA]Done[/color]")
```

Format: `[color=#RRGGBB]text[/color]` or `[color=#RRGGBBAA]text[/color]` for alpha.

---

### 13.7 Global Variables

Place a `.globals.md` file in any folder to declare variables shared across all notes in that folder. Only `ui` blocks inside it are read.

````markdown
```ui
campaign("Curse of Strahd")
party_level(5)
gold(120)
```
````

Any note in the same folder (or a subfolder) can then reference `campaign`, `party_level`, or `gold` directly in its own UI blocks without re-declaring them. Variables declared in the current note override globals with the same name.

**Load order** (closest scope wins):

1. Root-level `.globals.md`
2. Parent folder `.globals.md`
3. Current folder `.globals.md`
4. Current note UI block

`.globals.md` is optional — if the file doesn't exist, nothing changes. Create one via **Insert UI widget → Global variable**, which creates the file in the current note's folder and opens it for editing.

---

### 13.8 Full Examples

#### Simple counter

````markdown
```ui
count(0)
int(count, "Counter", 90, true)
button("Reset", 80, count=0)
```
````

---

#### Character sheet

````markdown
```ui
name("Hero")
class_("Warrior")
hp(75)
mp(40)
level(1)
alive(true)

text(name,   "Name",  140) text(class_, "Class", 120)
bar(hp,      "HP",    160, 0, 100, "#e03030")
bar(mp,      "MP",    160, 0, 100, "#3060e0")
slider(hp,   "HP",    160, 0, 100)
slider(mp,   "MP",    160, 0, 100)
int(level,   "Level",  70, true)
checkbox(alive, "Alive")

if(!alive) {
  text("[color=#FF4444]CHARACTER IS DEAD[/color]")
}
if(level >= 10) {
  text("[color=#FFD700]Max level reached![/color]")
}
```
````

---

#### Task tracker

````markdown
```ui
tasks([
  {name:"Write report",   tooltip:"Due Friday"},
  {name:"Review PR",      tooltip:"Urgent"},
  {name:"Update docs",    tooltip:"Low priority"},
  {name:"Deploy staging", tooltip:"After tests"}
])
filter("all")
enum(filter, "Show", 110, ["all", "done", "pending"])
list(tasks, "Tasks", 260, true)
```
````

---

#### Inventory grid

````markdown
```ui
inventory_data({
  items:[
    {name:"Potion", image:"potion.png", tooltip:"Consumable item", quantity:3, color:"#57A7FF", x:0, y:0},
    {tooltip:"Disabled example cell", color:"#FFB347", enabled:false, x:1, y:0}
  ]
})
inventory(inventory_data, "Inventory", 220, 2, 2)
```
````

---

#### Conditional form

````markdown
```ui
role("user")
enum(role, "Role", 130, ["user", "admin", "guest"])

if(role == "admin") {
  text("[color=#FF8800]Admin panel[/color]")
  checkbox(debug_mode, "Debug mode")
  slider(log_level, "Log level", 140, 0, 5)
}
if(role == "guest") {
  text("[color=#888888]Read-only access[/color]")
}
```
````

---

#### Annotated map

````markdown
```ui
world_map({
  image: "world.jpg",
  zoom: 1.0,
  offset_x: 0.0,
  offset_y: 0.0,
  markers: [
    {
      x: 0.48, y: 0.35,
      color: "#FFB347",
      title: "HQ",
      description: "Headquarters — main operations base."
    },
    {
      x: 0.15, y: 0.55,
      color: "#58C472",
      title: "Depot A",
      image: "depot.png",
      description: "Western supply depot."
    }
  ],
  strokes: []
})
map(world_map, 680, 420)
```
````

---

## 14. Reactive Diagrams (ui-mermaid)

A `ui-mermaid` fenced block is a Mermaid diagram template where `${expr}` placeholders are resolved against the current UI variable state before rendering. Any widget, button, slider, or computed value — including variables from `.globals.md` — will update the diagram live as values change.

````markdown
```ui-mermaid
xychart-beta
  title "Monthly Sales"
  x-axis [Jan, Feb, Mar]
  y-axis 0 --> 20000
  bar ${sales}
```
````

When `sales` is `[5000, 8000, 12000]` the placeholder expands to `[5000, 8000, 12000]` and the chart renders with those values. Buttons or sliders that modify `sales` will redraw the chart on the next frame.

### Expression syntax

Any expression the UI block system supports is valid inside `${ }`:

| Placeholder | Expands to |
|-------------|-----------|
| `${gold}` | current value of variable `gold` |
| `${sales[0]}` | first element of array `sales` |
| `${party_level * 10}` | arithmetic on a variable |
| `${sales}` | full array literal `[v1, v2, v3]` |

### Example — buttons that update a chart

**`.globals.md`** (shared across folder):

````markdown
```ui
sales([5000, 8000, 12000])

button("Boost January", 140, sales[0]=15000)
button("Boost March",   140, sales[2]=18000)
```
````

**Any note in the folder:**

````markdown
```ui-mermaid
xychart-beta
  title "Monthly Sales"
  x-axis [Jan, Feb, Mar]
  y-axis 0 --> 20000
  bar ${sales}
```
````

Clicking "Boost January" updates `sales[0]` to `15000` and the chart redraws immediately.

### Rules

- Only `ui` blocks and `.globals.md` variables are in scope — no other markdown state.
- If a `${expr}` fails to evaluate, the placeholder is left as-is in the diagram source.
- All Mermaid diagram types are supported.

---

## 15. Mermaid Diagrams

Embed diagrams in any note with a `mermaid` fenced code block, or use the diagram keyword directly as the fence language. All diagram types listed below are **fully rendered** as interactive graphics inside the note preview. Lines starting with `//` are treated as comments and ignored.

````markdown
```mermaid
flowchart LR
  A --> B --> C
```
````

---

### 15.1 Flowchart / Graph

**Keywords:** `flowchart`, `graph`

**Directions:** `TB` / `TD` (top-bottom, default) · `BT` · `LR` · `RL`

**Node shapes:** `id[Label]` rectangle · `id(Label)` rounded · `id{Label}` diamond

**Edge types:** `-->` arrow · `<-->` bidirectional · `<--` reverse · `-.->` dotted · `---` line · `==>` thick

**Edge labels:** `A -->|text| B`

````markdown
```mermaid
flowchart LR
  A[Request] --> B{Authenticated?}
  B -->|yes| C(Process)
  B -->|no| D[Reject]
  C --> E[Response]
```
````

````markdown
```mermaid
graph LR
  A --> B
  B <--> C
  C -.-> D
  D --- E
  E ==> F
```
````

---

### 15.2 Pie Chart

**Keyword:** `pie`  · `title` is optional.

````markdown
```mermaid
pie title Browser Share
  "Chrome"  : 65
  "Firefox" : 20
  "Safari"  : 10
  "Other"   : 5
```
````

---

### 15.3 Sequence Diagram

**Keyword:** `sequenceDiagram`

Supported: `participant`, `actor`, `->>` / `-->` / `-->>` arrows, `Note over`, `activate` / `deactivate`, `loop` / `alt` / `opt` / `par` groups.

````markdown
```mermaid
sequenceDiagram
  participant A as Alice
  participant B as Bob
  A->>B: Hello Bob
  activate B
  B-->>A: Hi Alice!
  deactivate B
  Note over A,B: End of chat
```
````

---

### 15.4 Class Diagram

**Keyword:** `classDiagram`

````markdown
```mermaid
classDiagram
  class Animal {
    +String name
    +eat() void
  }
  class Dog {
    +bark() void
  }
  Animal <|-- Dog
  Animal *-- Heart
```
````

---

### 15.5 State Diagram

**Keywords:** `stateDiagram`, `stateDiagram-v2`  · `[*]` = start / end state.

````markdown
```mermaid
stateDiagram-v2
  [*] --> Idle
  Idle --> Active : start
  Active --> Idle : stop
  Active --> [*] : shutdown
```
````

---

### 15.6 Entity Relationship Diagram

**Keyword:** `erDiagram`

````markdown
```mermaid
erDiagram
  CUSTOMER {
    int id PK
    string name
    string email
  }
  ORDER {
    int id PK
    date placed
  }
  CUSTOMER ||--o{ ORDER : places
```
````

---

### 15.7 User Journey

**Keyword:** `journey`

````markdown
```mermaid
journey
  title My Working Day
  section Morning
    Make tea: 5: Me
    Check email: 3: Me, Cat
  section Afternoon
    Code review: 4: Me
    Deploy: 2: Me
```
````

---

### 15.8 Gantt Chart

**Keyword:** `gantt`  · Duration units: `d` (days), `w` (weeks), `h` (hours).

````markdown
```mermaid
gantt
  title Project Plan
  section Design
    Wireframes :a1, 0, 3d
    Mockups    :a2, after a1, 4d
  section Development
    Backend  :crit, b1, after a2, 7d
    Frontend :b2, after a2, 5d
  section QA
    Testing  :milestone, after b1, 2d
```
````

---

### 15.9 Quadrant Chart

**Keyword:** `quadrantChart`

````markdown
```mermaid
quadrantChart
  title Feature Prioritization
  x-axis Low Effort --> High Effort
  y-axis Low Impact --> High Impact
  quadrant-1 Quick Wins
  quadrant-2 Major Projects
  quadrant-3 Fill-ins
  quadrant-4 Hard Slogs
  Auth redesign: [0.7, 0.8]
  Dark mode: [0.2, 0.6]
  Onboarding: [0.5, 0.9]
  Log viewer: [0.8, 0.3]
```
````

---

### 15.10 Requirement Diagram

**Keyword:** `requirementDiagram`

````markdown
```mermaid
requirementDiagram
  requirement auth_req {
    id: REQ-1
    text: Users must authenticate
    risk: high
    verifymethod: test
  }
  element login_page {
    type: UI
    docref: docs/login
  }
  login_page - satisfies -> auth_req
```
````

---

### 15.11 Git Graph

**Keyword:** `gitgraph`

````markdown
```mermaid
gitgraph
  commit id: "Initial"
  branch develop
  commit id: "Feature A"
  commit id: "Feature B"
  checkout main
  merge develop
  commit id: "Release" tag: "v1.0"
```
````

---

### 15.12 Mindmap

**Keyword:** `mindmap`  · Indentation defines hierarchy. Shape wrappers: `((text))` circle · `(text)` rounded · `[text]` square · `{{text}}` hexagon.

````markdown
```mermaid
mindmap
  root((Project))
    Planning
      Requirements
      Timeline
    Development
      Backend
      Frontend
    Testing
      Unit tests
      E2E tests
```
````

---

### 15.13 Timeline

**Keyword:** `timeline`  · Format: `period : event1 : event2`

````markdown
```mermaid
timeline
  title History of Social Media
  2002 : LinkedIn
  2004 : Facebook
       : Google
  2005 : YouTube
  2006 : Twitter
  2010 : Instagram
```
````

---

### 15.14 Sankey Diagram

**Keywords:** `sankey-beta`, `sankey`  · CSV format: `source,target,value`

````markdown
```mermaid
sankey-beta
source,target,value
Revenue,Salaries,40
Revenue,Infrastructure,25
Revenue,Marketing,20
Revenue,Profit,15
```
````

---

### 15.15 XY Chart

**Keywords:** `xychart-beta`, `xychart`

````markdown
```mermaid
xychart-beta
  title "Monthly Sales"
  x-axis [Jan, Feb, Mar, Apr, May, Jun]
  y-axis 0 --> 20000
  bar  [4000, 8000, 12000, 6000, 15000, 18000]
  line [3000, 7500, 11000, 5500, 14000, 17000]
```
````

---

### 15.16 Block Diagram

**Keywords:** `block-beta`, `block`

````markdown
```mermaid
block-beta
  columns 3
  A[Client]
  B[API Gateway]
  C[Database]
  A --> B
  B --> C
```
````

---

### 15.17 Packet Diagram

**Keywords:** `packet-beta`, `packet`  · Format: `start-end: "Field Name"`

Field range notations:

| Notation | Meaning |
|---|---|
| `0-15: "Name"` | bits 0 through 15 |
| `0+16: "Name"` | start bit 0, length 16 |
| `7: "Name"` | single bit at position 7 |

**Configuration** — place an init directive before the diagram keyword:

```
%%{init: {'packet': {'bitWidth': 20, 'rowHeight': 40, 'bitsPerRow': 32, 'showBits': true, 'paddingX': 8, 'paddingY': 6}}}%%
```

| Property | Type | Default | Description |
|---|---|---|---|
| `bitWidth` | number | `20` | Pixels per bit column |
| `rowHeight` | number | `40` | Height of each field row in pixels |
| `bitsPerRow` | number | `32` | Number of bits displayed per row (enables multi-row layout) |
| `showBits` | boolean | `true` | Show or hide the bit-number header |
| `paddingX` | number | `8` | Horizontal outer padding in pixels |
| `paddingY` | number | `6` | Vertical padding between rows in pixels |

Labels that are too wide for their field cell are rendered in a legend strip below the diagram. Fields that wrap across rows (when `bitsPerRow` < total bits) are split into per-row segments automatically.

**Interactions:**

| Interaction | Effect |
|-------------|--------|
| **Hover** over a field | Tooltip shows field name, bit range, and bit count |
| **Drag** a field left / right | Swaps the field with its neighbour; colors travel with the field |
| **Right-click** a field | Context menu: Rename, Expand +1 bit, Shrink −1 bit, Add field before, Delete |
| **Right-click** on blank canvas | Config popup: edit all layout properties (bitWidth, rowHeight, bitsPerRow, showBits, paddingX, paddingY, showLegend) |

All edits write back to the note source immediately.

````markdown
```mermaid
%%{init: {'packet': {'bitsPerRow': 16}}}%%
packet-beta
  title IPv4 Header (first 32 bits)
  0-3: "Version"
  4-7: "IHL"
  8-13: "DSCP"
  14-15: "ECN"
  16-31: "Total Length"
```
````

````markdown
```mermaid
packet-beta
  title UDP Packet
  0-15: "Source Port"
  16-31: "Destination Port"
  32-47: "Length"
  48-63: "Checksum"
```
````

---

### 15.18 Kanban Board

**Keyword:** `kanban`  · Columns are top-level items; cards are indented one level deeper.

Card syntax: `id[Label]` or `id[Label]: description text`

A small colored dot in the bottom-right corner of a card indicates it has a description.

**Interactions:**

| Interaction | Effect |
|-------------|--------|
| **Hover** over a card | Tooltip shows the description (or the label if no description is set) |
| **Drag** a card | Move it to any position within its column or into a different column; a colored insertion bar shows the drop target |
| **Right-click** a card | Edit popup: change the label and description (**OK** / **Enter** to confirm, **Delete** to remove the card) |
| **Right-click** empty column area | Context menu with **Add card** option |

All edits write back to the note source immediately.

````markdown
```mermaid
kanban
  todo[To Do]
    t1[Write tests]: Write unit tests for the new API endpoints
    t2[Update docs]: Update README with new features
  inprogress[In Progress]
    t3[Implement feature]: Core logic implementation
  done[Done]
    t4[Code review]
    t5[Deploy to staging]
```
````

---

#### Linked-note cards

A card label can be a standard Markdown note reference. The reference is kept as plain Markdown in the editor and becomes an interactive card in preview mode:

````markdown
```mermaid
kanban
  todo[To Do]
    c1[Read the guide](notepp-cli.md)
    c2[Agent commands](notepp-cli.md#notepp-agent-commands)
    c3[](notepp-cli.md#notepp-agent-commands)
```
````

- A non-empty reference label is displayed as the card label.
- An empty label displays the referenced note name or heading title.
- Hovering a linked card shows a preview of the referenced note or heading instead of the normal card description.
- In preview mode, drag a note from the **Explorer** onto a Kanban column to automatically create an empty-label reference such as `[](notepp-cli.md)`.
- Moving or editing cards preserves the Markdown reference in the note source.

#### Mermaid event commands

Give a Mermaid diagram a Notepp ID with a comment. Then declare one or more event handlers in any fenced `ui` block in the same note:

````markdown
```mermaid
%% notepp-id: tasks
kanban
  todo[To Do]
    task1[Write documentation]
  progress[In Progress]
  done[Done]
```

```ui
kanban_on_enter(
  "tasks",
  "done",
  command("echo Task completed")
)
```
````

`kanban_on_enter("diagram-id", "column-id", command("..."))` runs whenever a card is dropped into the matching column, including reordering within that column. Multiple matching handlers run in source order. Commands use Notepp's embedded terminal behavior and are sent to the terminal after the current Kanban interaction completes.

---

### 15.19 Architecture Diagram

**Keywords:** `architecture-beta`, `architecture`

````markdown
```mermaid
architecture-beta
  group cloud(server)[Cloud]
    service db(database)[Database] in cloud
    service api(server)[API Server] in cloud
    service cache(server)[Cache] in cloud
  db:L -- R:api
  api:L -- R:cache
```
````

---

### 15.20 Radar Chart

**Keywords:** `radar-beta`, `radar`  · Simple syntax: `Axis: value` per line.

````markdown
```mermaid
radar-beta
  title Developer Skills
  max 100
  Frontend: 75
  Backend: 90
  Database: 70
  DevOps: 60
  Testing: 80
```
````

Multi-curve syntax:

````markdown
```mermaid
radar-beta
  title Team Comparison
  axis ["Speed", "Quality", "Cost", "Scope", "Risk"]
  Alice {
    data [80, 90, 70, 85, 60]
  }
  Bob {
    data [70, 75, 85, 65, 80]
  }
```
````

---

### 15.21 Treemap

**Keywords:** `treemap-beta`, `treemap`  · Indentation = hierarchy; leaf nodes have a numeric value.

````markdown
```mermaid
treemap-beta
  title Team Budget
  Engineering
    Frontend: 30
    Backend: 40
    DevOps: 20
  Marketing: 35
  Operations: 25
```
````

---

### 15.22 ZenUML

**Keyword:** `zenuml`  · Uses `Object.method(Target)` call syntax.

````markdown
```mermaid
zenuml
  @Client
  @Server
  @DB
  Client.request(Server)
  Server.query(DB)
  DB.result(Server)
  Server.response(Client)
```
````

---

### 15.23 Event Modeling

**Keyword:** `eventmodeling`

````markdown
```mermaid
eventmodeling
  title Order System
  command "Place Order"
  event "Order Placed"
  readmodel "Order List"
  policy "Notify on Order"
  processor "Fulfillment"
```
````

---

### 15.24 Venn Diagram

**Keyword:** `venn`

````markdown
```mermaid
venn
  title Programming Skills
  A "Python"
  B "JavaScript"
  C "Rust"
  A&B "Full-Stack"
  A&C "Systems+Script"
  A&B&C "Polyglot"
```
````

---

### 15.25 Ishikawa (Fishbone) Diagram

**Keyword:** `ishikawa`

````markdown
```mermaid
ishikawa
  effect "Bug in Production"
  category "Code"
    cause Untested edge case
    cause Missing validation
  category "Process"
    cause No code review
    cause Rushed release
  category "Environment"
    cause Config mismatch
```
````

---

### 15.26 Wardley Map

**Keyword:** `wardley`  · Components placed at `[visibility, evolution]` (both 0–1).

````markdown
```mermaid
wardley
  title Tea Shop
  component "Cup of Tea" [0.95, 0.50]
  component "Tea Leaves" [0.80, 0.60]
  component "Water"      [0.95, 0.90]
  component "Kettle"     [0.70, 0.75]
  "Cup of Tea" -> "Tea Leaves"
  "Cup of Tea" -> "Water"
  "Water" -> "Kettle"
```
````

---

### 15.27 TreeView

**Keyword:** `treeview`  · Pure indentation hierarchy.

````markdown
```mermaid
treeview
  src
    components
      Button
      Modal
    pages
      Home
      Settings
  tests
    unit
    e2e
```
````

---

## Data Files

Notes are stored as plain Markdown files — easy to version-control or edit externally. The checked-in demo examples are:

| File | Contents |
|------|----------|
| `data/demo.md` | Mirror of the root README demo note |
| `data/inv.md` | Inventory widget example |
| `data/json.md` | UI block JSON example |
| `config/notes_index.json` | Folder and note metadata |
| `config/imgui_layout.ini` | ImGui window layout |
| `config/layout_profiles.json` | Layout profiles |
| `config/markdown_preview_state.json` | Collapsed/expanded section states |

### Cross-device Git Sync

Notepp can use the installed system Git to synchronize an existing project repository. Git Sync is disabled by default; enable it from the Explorer context menu. When enabled, Notepp performs a fast-forward-only pull before loading a project and saves, commits, and pushes on normal close. Opening waits only for bounded Git command timeouts; closing keeps the window responsive while its bounded Git worker finishes. Failures never prevent subsequent local editing or final exit.

See the [Git Sync documentation](https://github.com/sauronc0de/notepp/blob/main/docs/git_sync.md) for setup, safety guarantees, statuses, and recovery instructions.

### Command API and `notepp-cli`

Notepp exposes the same structured Command API to the GUI, local scripts, and
AI agents. Press **Ctrl+Shift+P** to discover commands such as listing notes,
creating notes, reading headers, changing colors, and reading or updating UI
variables. Commands that need parameters open a form; read-only results are
shown in the embedded terminal.

The command-line client communicates with a running Notepp instance through
local IPC. It does not edit Notepp files directly. Run:

```bash
notepp-cli --help
```

For example:

```bash
notepp-cli --project "/path/to/your/project" --json app capabilities
notepp-cli --project "/path/to/your/project" --json note list
notepp-cli --project "/path/to/your/project" --json note create \\
  --folder General --name "A new note" --content "# A new note"
```

The complete protocol, command arguments, JSON envelopes, error codes, and
raw JSON examples are available in the public
[Command API documentation](https://github.com/sauronc0de/notepp/blob/main/docs/command_api.md).

---

## 16. Embedded Terminal

Open the terminal with **Ctrl+ñ**, or open the Command Finder with
**Ctrl+Shift+P** and run **Open / Hide Terminal**. **Ctrl+ñ** toggles its
visibility without stopping the shell. **Ctrl+Shift+ñ** opens a new terminal tab
and makes the terminal visible. The shell starts in the project notes root, so
`ls`, `git status`, `grep`, and any other tool you launch operate on the same
files the app edits.

```
┌─────────────────────────────────────────────────────────────────────┐
│ Terminal                                                            │
├─────────────────────────────────────────────────────────────────────┤
│ user@host:~/notes$ ls                                               │
│ readme.md    Inbox/    Archive/                                     │
│ user@host:~/notes$ git status                                       │
│ On branch main                                                      │
│ nothing to commit, working tree clean                               │
│ user@host:~/notes$ _                                                │
└─────────────────────────────────────────────────────────────────────┘
```

**Backed by:**

- a real OS-level PTY (`forkpty` on Linux/macOS, ConPTY on Windows)
- a vt100/xterm-compatible terminal emulator ([libvterm](https://github.com/neovim/libvterm))

That means anything that works in a normal terminal works here: `pi`,
`git`, `vim`, `htop`, `less`, `ls --color`, `tmux` …

**Behavior:**

- The shell is **not** sandboxed — full access by design.
- Hiding the terminal keeps the shell alive so it reappears instantly. Typing
  `exit` closes the shell and removes its tab; closing the app terminates all
  shells cleanly.
- The terminal is an opaque bottom overlay over the notes. Its horizontal
  position and width are fixed; drag its top edge to resize it vertically. The
  height is saved with the workspace and restored when reopened.
- The terminal keeps scrollback. Hover it and use the mouse wheel to scroll
  through output larger than the visible area.
- Drag across terminal cells to select text. Use **Ctrl+Shift+C** to copy the
  selection, **Ctrl+Shift+V** or **Shift+Insert** to paste.
- Standard shortcuts (Ctrl+C, Ctrl+D, Ctrl+L, arrows, Tab, Enter, Backspace)
  flow through to the shell as expected. Use **Ctrl+Tab** and
  **Ctrl+Shift+Tab** to switch terminal tabs.
- While the terminal window is focused, app shortcuts are delivered to the
  terminal instead of the app. Press **F6** to leave terminal focus, then use
  Command Finder or another app shortcut.

This is the surface used to run coding agents like `pi` from inside the
app, so the agent can read and write notes with full knowledge of the
folder structure and the app's format.

---

*Built with C++23 · Dear ImGui · SDL2 · OpenGL*

*Created by*  
![title](docs/img/sauroncode_reduced.png)

---
