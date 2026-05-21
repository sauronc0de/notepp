# Notes App

![title](assets/icon/notepp.ico)

![Version](https://img.shields.io/badge/version-0.0.1-green)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/C++-00599C?style=flat-square&logo=C%2B%2B&logoColor=white)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-orange)

A **C++ desktop note-taking app** powered by Dear ImGui. Notes are stored as plain **Markdown files**, organized in folders, and enriched with an interactive UI block system, drawings, and rich formatting.

---

## Table of Contents

1. [Interface Overview](#1-interface-overview)
2. [Folders & Notes](#2-folders--notes)
3. [Editing & Markdown](#3-editing--markdown)
4. [Toolbar Reference](#4-toolbar-reference)
5. [Text Color Syntax](#5-text-color-syntax)
6. [Drawing Tools](#6-drawing-tools)
7. [Images & Fonts](#7-images--fonts)
8. [Search](#8-search)
9. [Detached Windows](#9-detached-windows)
10. [Undo / Redo](#10-undo--redo)
11. [Keyboard Shortcuts](#11-keyboard-shortcuts)
12. [UI Block System](#12-ui-block-system)
    - [Variables](#121-variables)
    - [Widgets](#122-widgets)
    - [Expressions & Operators](#123-expressions--operators)
    - [Built-in Functions](#124-built-in-functions)
    - [Conditionals](#125-conditionals)
    - [Label Color Syntax](#126-label-color-syntax)
    - [Full Examples](#127-full-examples)

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
| Rename | Right-click the folder or note → **Rename** |
| Delete | Right-click → **Delete** |

### Organizing

- **Colors** — right-click a note → **Color** to assign a custom RGB color; folders have their own accent color.
- **Copy / Paste notes** — right-click → **Copy**, then right-click destination folder → **Paste**.
- **Hidden notes** — notes can be toggled hidden from the context menu.

### Selecting multiple notes

Hold **Ctrl + Click** to select several notes in the sidebar for batch operations.

---

## 3. Editing & Markdown

Each note is a `.md` file. The editor supports full UTF-8 text including emoji.

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

### Markdown Preview

Click the **Preview** toggle in the toolbar to switch to rendered view. Section headers become collapsible — click them to expand or collapse. All section states are saved between sessions.

---

## 4. Toolbar Reference

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
| ![widgets](assets/icons/widgets.png) | **UI Block** | Inserts a new `UI` code fence |
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

## 5. Text Color Syntax

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

## 6. Drawing Tools

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

## 7. Images & Fonts

### Images

- **Supported formats:** PNG, JPG, JPEG, GIF, BMP, WebP
- **Embed in markdown:** `![alt text](filename.png)` — path relative to the note folder
- **Image browser:** The sidebar lists all images in the current folder
- **Drag & drop:** Drag an image from the sidebar onto the note text to insert it
- **Right-click image in sidebar → Reveal in File Explorer**

### Fonts

- **Supported:** TTF and OTF files
- **Add a font:** Drop a font file into the folder, or use the font browser
- **Apply to a note:** Drag a font from the sidebar onto the note
- **Font size:** Ctrl+Scroll while hovering a note to resize text
- **Per-note:** Each note keeps its own font and size independently

---

## 8. Search

Open with **Ctrl+F** or the ![find](assets/icons/find.png) toolbar button.

- Searches **note titles**, **file paths**, and **full note content**
- Results show the matching line with a snippet preview
- Click a result to jump directly to that note
- Search scope spans the entire project

---

## 9. Detached Windows

Click ![detach](assets/icons/detach.png) to enable floating note windows. Each note opens in its own movable panel.

| Feature | How |
|---------|-----|
| Enable/disable | Detach button in toolbar |
| Move windows | Drag the title bar |
| Lock positions | ![lock](assets/icons/lock.png) Lock button — prevents accidental moves |
| Reset layout | ![focus](assets/icons/focus.png) Reset positions button |
| Always on top | Right-click a floating note window title bar |

---

## 10. Undo / Redo

A unified history tracks text edits, note selection, layout changes, preview state, and drawings.

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+Shift+Z | Redo (alternative) |

Undo groups character-level changes into word-granular chunks for convenient reversal.

---

## 11. Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+Z | Undo |
| Ctrl+Y / Ctrl+Shift+Z | Redo |
| Ctrl+F | Open Find / Search |
| Ctrl+Scroll | Change font size (hover over note) |
| Ctrl+Click | Multi-select notes in sidebar |
| Double-click word | Select entire word |
| Right-click | Context menus everywhere |

---

## 12. UI Block System

Embed **interactive widgets** directly inside any note using a fenced code block with the `UI` language tag.

````markdown
```UI
variableName(initialValue)
widget(variable, "Label", width, ...options)
```
````

Widget state is **persistent** — values survive edits, closing, and reopening. Changing the markdown source resets only the blocks that actually changed.

---

### 12.1 Variables

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

### 12.2 Widgets

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
inventory(variable, "Label", width, rows, cols)
```

`variable` is an **array of slot objects**. Each slot supports:

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Item title shown on hover |
| `image` | string | Image path (relative, absolute, or filename from `assets/icons/`) |
| `tooltip` | string | Hover tooltip text |
| `quantity` | number | Badge number shown in the slot corner |
| `color` | string `#RRGGBB` | Corner mark color |
| `enabled` | bool | Grayed-out when `false` |

**Example:**
```
backpack([
  {name:"Sword",  image:"sword.png",  tooltip:"Iron sword",  quantity:1},
  {name:"Coin",   image:"coin.png",   tooltip:"Gold coin",   quantity:42, color:"#FFD700"},
  {name:"Potion", image:"potion.png", tooltip:"Health pot",  enabled:false}
])
inventory(backpack, "Backpack", 300, 2, 4)
```

---

#### `button` — Action button

```
button("Label", width, variable=value)
```

Clicking the button **assigns a value to a variable**. Multiple assignments separated by commas are not currently supported — use one assignment per button.

**Example:**
```
count(0)
int(count, "Count", 80, true)
button("Reset", 80, count=0)
button("Max",   80, count=100)
```

---

### 12.3 Expressions & Operators

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

**Truthiness rules:**
- Number: `true` if non-zero
- String: `true` if non-empty
- Array/Object: `true` if non-empty
- Bool: as-is

---

### 12.4 Built-in Functions

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

### 12.5 Conditionals

Show or hide any group of widgets based on a condition:

```
if(condition) {
  widget(...)
  widget(...)
}
```

- The `{` must be on the **same line** as `if(...)`.
- The closing `}` must be on its **own line**.
- Conditions use the full expression syntax from [12.3](#123-expressions--operators).
- Conditionals can reference any declared variable.

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

### 12.6 Label Color Syntax

Widget labels support inline color formatting:

```
text("[color=#00CC88]Status[/color]", 120)
checkbox(done, "[color=#AAAAAA]Done[/color]")
```

Format: `[color=#RRGGBB]text[/color]` or `[color=#RRGGBBAA]text[/color]` for alpha.

---

### 12.7 Full Examples

#### Simple counter

````markdown
```UI
count(0)
int(count, "Counter", 90, true)
button("Reset", 80, count=0)
```
````

---

#### Character sheet

````markdown
```UI
name("Hero")
class_("Warrior")
hp(100)
mp(40)
level(1)
alive(true)

text(name,   "Name",  140) text(class_, "Class", 120)
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
```UI
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
```UI
bag([
  {name:"Sword",    image:"sword.png",    tooltip:"Iron sword",    quantity:1},
  {name:"Shield",   image:"shield.png",   tooltip:"Wooden shield", quantity:1},
  {name:"Potion",   image:"potion.png",   tooltip:"Heals 50 HP",   quantity:5, color:"#FF4444"},
  {name:"Gold",     image:"coin.png",     tooltip:"Gold coins",    quantity:120, color:"#FFD700"},
  {name:"Key",      image:"key.png",      tooltip:"Mystery key",   enabled:false},
  {}
])
inventory(bag, "Backpack", 320, 2, 3)
```
````

---

#### Conditional form

````markdown
```UI
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

## Data Files

Notes are stored in plain text — easy to version-control or edit externally.

| File | Contents |
|------|----------|
| `data/notes/<Folder>/<Note>.md` | Note content (Markdown) |
| `data/notes_index.json` | Folder/note metadata, colors, layout |
| `data/drawings_state.txt` | Drawing strokes per folder |
| `data/imgui_layout.ini` | Window positions and UI state |
| `data/markdown_preview_state.json` | Collapsed/expanded section states |
| `data/note_clipboard.json` | Internal copy/paste clipboard |

---

*Built with C++20 · Dear ImGui · SDL2 · OpenGL*

*Created by*  
![title](docs/img/sauroncode_reduced.png)

---