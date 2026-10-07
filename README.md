<p align="center">
  <img src="resources/logo.svg" width="260" alt="PhotoSlop logo">
</p>

<p align="center"><i>Its not a bug, it's a feature</i></p>

---

PhotoSlop is a cross-platform raster image editor for macOS, Linux and Windows. Its layout, behaviour and keyboard shortcuts closely follow Adobe Photoshop, so Photoshop users can work in it without relearning anything.

It is written in C++20 with Qt 6 (Widgets). PhotoSlop is not affiliated with Adobe. All icons and artwork are original.

![The PhotoSlop workspace with a layered document and an elliptical selection](docs/screenshots/workspace.png)

## Screenshots

| | |
|---|---|
| ![Home screen](docs/screenshots/home.png) | ![Free Transform with rulers and guides](docs/screenshots/free-transform.png) |
| **Home screen** with New file, Open and recent files | **Free Transform**: rotate and scale a layer, with rulers, guides and exact values in the options bar |
| ![Warp](docs/screenshots/warp.png) | ![Quick Mask](docs/screenshots/quick-mask.png) |
| **Warp** bends a layer with a 4 × 4 control grid | **Quick Mask** shows the selection as a red overlay you can paint on |

## Status: Stage 2 (selections and transforms)

| Area | What works |
|---|---|
| Workspace | Photoshop "Essentials" layout: toolbar on the left, options bar on top, tabbed panel groups on the right, a collapsed icon strip (History, Navigator, Info), document tabs, a per-document status bar, a Home screen, Tab / Shift+Tab to hide panels, F to cycle screen modes |
| Documents | New (preset browser), Open, Open Recent, Save, Save As, Save a Copy, Revert, Export As, Quick Export as PNG, Close / Close All / Close Others, multiple documents, drag-and-drop to open |
| File formats | Native layered `.pslop`; PNG, JPEG, BMP, GIF, TIFF and WebP for import and flattened export |
| Layers | New, duplicate, delete, rename, drag to reorder, visibility (Alt-click to solo), opacity, fill, 27 blend modes, four lock types, Background layer rules, Layer via Copy/Cut, Merge Down, Merge Visible, Flatten, Ctrl/Cmd-click a thumbnail to load its transparency as a selection |
| Tools | Move, Rectangular and Elliptical Marquee, Lasso, Polygonal Lasso and Magnetic Lasso, Quick Selection, Magic Wand, Crop, Eyedropper, Brush, Pencil, Eraser, Gradient (5 types), Paint Bucket, Hand, Zoom |
| Painting | Size, hardness, opacity, flow, blend mode, pen pressure for size and opacity, Shift-click straight lines, brush preset picker. Opacity caps each stroke the same way it does in Photoshop |
| Selections | Feathered 8-bit masks; add, subtract and intersect (Shift / Alt / Shift+Alt); marching ants; All, Deselect, Reselect, Inverse; Modify ▸ Border, Smooth, Expand, Contract, Feather; Grow and Similar (using the Magic Wand tolerance); Transform Selection; drag inside a selection to move its outline; arrow keys to nudge |
| Selection tools | Magic Wand (tolerance, sample size, contiguous, sample all layers); Quick Selection (paint to grow the selection into similar, edge-bounded areas; switches to Add after the first stroke); Magnetic Lasso (follows the strongest edge within Width; Contrast and Frequency options; Backspace removes the last anchor, double-click or Enter closes) |
| Quick Mask | Q toggles it. The selection becomes a red-tinted mask that every painting tool, Fill, Clear and Invert can edit; leaving Quick Mask turns it back into a selection. Entering and leaving are history states |
| Transform | Free Transform and Edit ▸ Transform (Scale, Rotate, Skew, Distort, Perspective, Warp, Rotate 180°/90°, Flip, Again). Corner handles scale proportionally (Shift for free scaling, Alt from the reference point), drag outside to rotate (Shift for 15° steps), Ctrl/Cmd-drag a corner to distort or an edge to skew, Ctrl+Alt+Shift-drag a corner for perspective. The options bar takes exact X/Y, W/H, angle and skew values and the interpolation. Warp bends the layer with a 4 × 4 control grid: drag the points or the surface. Enter commits, Esc or Undo cancels |
| Rulers, guides, grid | Rulers (pixels, inches, cm, mm or percent; right-click to change). Drag a guide out of a ruler (Alt flips its direction), move guides with the Move tool and drag them off the window to delete; New Guide, Clear Guides, Lock Guides; guides are saved in `.pslop` files and undoable. Grid every inch with four subdivisions. Extras (Ctrl+H) hides them all |
| Snapping | Snap (Ctrl+Shift+;) to guides, the grid and document bounds while drawing marquees, cropping, moving layers and selections, and transforming |
| Edit | History panel (50 states), Undo/Redo, Toggle Last State, Cut/Copy/Copy Merged/Paste/Paste in Place, Fill dialog, Clear |
| Image | Image Size, Canvas Size (with anchor), rotate 90°/180°, flip canvas, Crop to selection, Duplicate, Invert, Desaturate |
| Colour | Foreground/background colours, Photoshop-style Color Picker (HSB / RGB / CMYK / hex), Color panel, Swatches panel |

Menu items for later stages are already in the menus, greyed out, with their Photoshop shortcuts shown.

What differs from Photoshop in this stage: Quick Selection uses colour similarity and edge strength rather than a trained model; Warp offers the custom grid only (no Arc, Bulge and other presets); Transform ▸ Again repeats the last transform's matrix rather than its parameters; Snap To ▸ Layers and Slices are not implemented yet.

## Keyboard shortcuts

The shortcuts match Photoshop on every platform. Wherever Windows and Linux use **Ctrl** and **Alt**, macOS uses **⌘ Cmd** and **⌥ Option**. Edit ▸ Keyboard Shortcuts (Ctrl+Alt+Shift+K) lists them all inside the app.

| Action | Windows / Linux | macOS |
|---|---|---|
| New / Open / Save / Save As | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | ⌘N / ⌘O / ⌘S / ⇧⌘S |
| Export As | Ctrl+Alt+Shift+W | ⌥⇧⌘W |
| Undo / Redo / Toggle Last State | Ctrl+Z / Ctrl+Shift+Z / Ctrl+Alt+Z | ⌘Z / ⇧⌘Z / ⌥⌘Z |
| Copy Merged / Paste in Place | Ctrl+Shift+C / Ctrl+Shift+V | ⇧⌘C / ⇧⌘V |
| Fill / Fill with FG / Fill with BG | Shift+F5 / Alt+Backspace / Ctrl+Backspace | ⇧F5 / ⌥⌫ / ⌘⌫ |
| Select All / Deselect / Reselect / Inverse | Ctrl+A / Ctrl+D / Ctrl+Shift+D / Ctrl+Shift+I | ⌘A / ⌘D / ⇧⌘D / ⇧⌘I |
| New Layer / Layer via Copy / via Cut | Ctrl+Shift+N / Ctrl+J / Ctrl+Shift+J | ⇧⌘N / ⌘J / ⇧⌘J |
| Merge Down / Merge Visible | Ctrl+E / Ctrl+Shift+E | ⌘E / ⇧⌘E |
| Bring Forward / Send Backward | Ctrl+] / Ctrl+[ | ⌘] / ⌘[ |
| Image Size / Canvas Size | Ctrl+Alt+I / Ctrl+Alt+C | ⌥⌘I / ⌥⌘C |
| Free Transform / Transform Again | Ctrl+T / Ctrl+Shift+T | ⌘T / ⇧⌘T |
| Quick Mask | Q | Q |
| Feather | Shift+F6 | ⇧F6 |
| Rulers / Grid / Guides | Ctrl+R / Ctrl+' / Ctrl+; | ⌘R / ⌘' / ⌘; |
| Snap / Lock Guides / Extras | Ctrl+Shift+; / Ctrl+Alt+; / Ctrl+H | ⇧⌘; / ⌥⌘; / ⌘H |
| Invert / Desaturate | Ctrl+I / Ctrl+Shift+U | ⌘I / ⇧⌘U |
| Zoom In / Out / Fit / 100% | Ctrl+= / Ctrl+- / Ctrl+0 / Ctrl+1 | ⌘= / ⌘- / ⌘0 / ⌘1 |
| Tools | V M L W C I B E G H Z (Shift+letter cycles a tool group) | same |
| Default colours / Swap colours | D / X | same |
| Brush size / hardness | [ ] / Shift+[ Shift+] | same |
| Tool or layer opacity | 1…9, 0 = 100%, type two digits fast for e.g. 45% | same |
| Temporary Hand / Eyedropper / Move | hold Space / Alt / Ctrl | hold Space / ⌥ / ⌘ |
| Hide panels / hide right panels | Tab / Shift+Tab | same |
| Panels | F6 Color, F7 Layers, F8 Info | same |

## Building

You need CMake 3.21+, Ninja (optional) and Qt 6.5 or newer with the Widgets and Svg modules.

**macOS**
```sh
brew install qt cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
open build/PhotoSlop.app
```

**Ubuntu**
```sh
sudo apt install qt6-base-dev qt6-svg-dev libgl1-mesa-dev cmake ninja-build
cmake -S . -B build -G Ninja
cmake --build build
./build/PhotoSlop
```

**Windows** (from a "Developer Command Prompt for VS", with Qt from the Qt online installer)
```bat
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64
cmake --build build
build\PhotoSlop.exe
```

### Tests
```sh
ctest --test-dir build --output-on-failure
```
`test_core` covers blend maths, undo/redo, selections and Select ▸ Modify, transform and warp resampling, Quick Mask, guides, merging, crop/rotate and file round-trips. `test_ui` drives the real main window with simulated mouse input to check every tool, Free Transform, Quick Mask, rulers, guides and snapping. On a headless machine, run it with `QT_QPA_PLATFORM=offscreen`.

## Project layout

```
src/core/     Document model: layers, blend modes, compositing, selections, transforms and warps, undo commands, document operations
src/io/       File loading and saving (.pslop and flat image formats)
src/tools/    One class per tool, plus the ToolManager (groups, shortcuts, temporary tools)
src/ui/       Main window, canvas view, rulers, view options, toolbox, panels and dialogs
resources/    Original SVG icons and the logo
docs/         README screenshots
tests/        Core unit tests and GUI tests
```

## Roadmap

1. **Core editor**: done.
2. **Selections and transforms**: done (this release).
3. **Adjustments and filters**: Levels, Curves, Hue/Saturation, Color Balance, Gaussian Blur, Unsharp Mask, Add Noise and more, with live preview.
4. **Layer power features**: masks, clipping masks, groups, adjustment layers, layer styles, Type tool, Shape tools, Clone Stamp, Healing, Dodge/Burn.
5. **Pro and platform**: PSD import/export, 16/32-bit and CMYK/Lab modes, GPU canvas, an editable shortcut editor, Preferences, and installers for every platform.

## License

Copyright (C) 2026 gibigbig

PhotoSlop is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.
