# Changelog

All notable changes to PhotoSlop are listed here. Version numbers follow [Semantic Versioning](https://semver.org/): while PhotoSlop is below 1.0, each roadmap stage raises the minor version (0.3.0 for Stage 3) and fixes raise the patch version.

The current version is in the `VERSION` file. To release, add notes under **Unreleased**, then run `scripts/bump-version.sh`.

## [Unreleased]

Stage 5: pro and platform.

### Added
- Photoshop files: open layered `.psd` and `.psb` files (Bitmap, Grayscale, Duotone, Indexed, RGB, CMYK and Lab; 1, 8, 16 and 32 bits; raw, RLE and ZIP) and save `.psd` with layers, groups, masks, clipping, blend modes, fill, locks, guides, resolution and Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Invert, Posterize and Threshold adjustment layers. A note lists anything that could not be kept.
- Image ▸ Mode: Grayscale, RGB Color, CMYK Color and Lab Color. The Info and Channels panels and the document title follow the mode.
- GPU canvas: the canvas is drawn with OpenGL when a graphics processor is available.
- Edit ▸ Preferences (Ctrl+K): Home screen, scroll-wheel zoom, UI font size, history states, graphics processor, cursors, transparency checkerboard, ruler units, guide and grid colours and spacing, and the recent file list.
- Edit ▸ Keyboard Shortcuts (Ctrl+Alt+Shift+K) can now change shortcuts for menu commands and tools, with conflict handling, Use Default, Reset All and Summarize.
- Window ▸ Workspace: New Workspace, Delete Workspace, switching between saved workspaces, and Reset for the current one.
- Window ▸ Actions (Alt+F9): record, play, stop, sets, step and dialog toggles, and four Default Actions.
- Installers: a Windows setup program with file associations, a `.deb` for Debian and Ubuntu, and app icons and document types for macOS.

### Changed
- `.pslop` files store the colour mode (older files still open).
- Grey PNG and TIFF files open in Grayscale mode.

## [0.4.0] - 2026-10-07

Stage 4: layer power features.

### Added
- Layer groups: New Group, Group Layers (Ctrl+G), Ungroup (Ctrl+Shift+G), nesting, Pass Through and other blend modes, group opacity and masks, collapsible rows, dragging layers into and out of groups, Merge Group.
- Layer masks: Reveal/Hide All, Reveal/Hide Selection, Delete, Apply, Disable, Link and Load Selection; click the mask thumbnail to paint on it with any tool, filter or adjustment.
- Clipping masks (Ctrl+Alt+G).
- Adjustment layers for Brightness/Contrast, Levels, Curves, Hue/Saturation, Color Balance, Black & White, Invert, Posterize and Threshold, from the Layer menu, the Adjustments panel or the Layers panel; Layer Content Options reopens their settings.
- Layer styles: Stroke, Color Overlay, Outer Glow and Drop Shadow with Blending Options and a live preview; Copy, Paste, Clear and Hide All Effects; Rasterize ▸ Layer Style.
- Type tool (T) for editable point text, with font, style, size, anti-aliasing, alignment and colour.
- Shape tools (U): Rectangle (rounded corners), Ellipse, Polygon (star) and Line, as editable shape layers with fill and stroke.
- Clone Stamp (S), Healing Brush and Spot Healing Brush (J), History Brush (Y), Dodge, Burn and Sponge (O), and Blur, Sharpen and Smudge.
- History panel: Set Source for History Brush.
- Layer ▸ Rasterize (Type, Shape, Layer Style, Layer) and Type ▸ Rasterize Type Layer; painting, filters and adjustments on a type or shape layer offer to rasterize it.
- A context-aware Properties panel.
- `.pslop` files store groups, masks, clipping, adjustment, type and shape layers and styles (older files still open).

### Changed
- The compositor is rewritten around the layer tree and renders on all CPU cores.
- Free Transform, canvas rotation, Image Size and Canvas Size keep type and shape layers as vectors.
- The Move tool moves whole groups and linked masks, and Auto-Select can pick the group.

## [0.3.0] - 2026-10-07

Stage 3: adjustments and filters.

### Added
- Image ▸ Adjustments: Levels, Curves, Hue/Saturation, Color Balance, Brightness/Contrast, Black & White, Threshold and Posterize, with histograms and Photoshop's shortcuts. Hold Alt (Option) to reopen Levels, Curves, Hue/Saturation or Color Balance with the last settings.
- Image ▸ Auto Tone, Auto Contrast and Auto Color.
- Filters: Blur, Blur More, Box Blur, Gaussian Blur, Motion Blur, Add Noise, Median, Mosaic, Sharpen, Sharpen More, Unsharp Mask and High Pass.
- Filter ▸ Last Filter (Ctrl+Alt+F) and Edit ▸ Fade (Ctrl+Shift+F).
- Live canvas preview for every adjustment and filter dialog, rendered on all CPU cores in the background.

### Changed
- Invert and Desaturate now use the shared filter engine, so they can be faded.

## [0.2.0] - 2026-10-07

Stage 2: selections and transforms.

### Added
- Selection tools: Magic Wand, Quick Selection and Magnetic Lasso.
- Select ▸ Modify (Border, Smooth, Expand, Contract, Feather), Grow, Similar and Transform Selection.
- Quick Mask mode (Q).
- Free Transform and Edit ▸ Transform: Scale, Rotate, Skew, Distort, Perspective, Warp, Rotate 180°/90°, Flip and Again, with numeric entry in the options bar.
- Rulers, guides and grid, with snapping to guides, the grid and document bounds.
- README screenshots.
- GPL-3.0-or-later licence.
- Release builds for macOS, Windows and Linux.

## [0.1.0] - 2026-10-07

Stage 1: the core editor.

### Added
- Workspace: toolbar, options bar, tabbed panels, document tabs, Home screen and screen modes.
- New, Open, Save, Save As, Export As and Quick Export; the native layered `.pslop` format plus PNG, JPEG, BMP, GIF, TIFF and WebP.
- Layers with 27 blend modes, opacity, fill, locks, merging and flattening.
- Move, Marquee, Lasso, Crop, Eyedropper, Brush, Pencil, Eraser, Gradient, Paint Bucket, Hand and Zoom tools.
- History panel, Image Size, Canvas Size, rotate and flip, Color Picker, Color and Swatches panels.
