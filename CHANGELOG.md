# Changelog

All notable changes to PhotoSlop are listed here. Version numbers follow [Semantic Versioning](https://semver.org/): while PhotoSlop is below 1.0, each roadmap stage raises the minor version (0.3.0 for Stage 3) and fixes raise the patch version.

The current version is in the `VERSION` file. To release, add notes under **Unreleased**, then run `scripts/bump-version.sh`.

## [Unreleased]

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
