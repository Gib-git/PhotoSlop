#!/usr/bin/env bash
# Builds a .deb from the AppDir that linuxdeploy made (used by the Release workflow).
# Usage: packaging/linux/build-deb.sh <AppDir> <version> <output.deb>
set -euo pipefail
appdir=$1
version=$2
out=$3
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT

mkdir -p "$root/opt/photoslop" "$root/usr/bin" "$root/usr/share/applications" \
         "$root/usr/share/icons/hicolor/scalable/apps" "$root/usr/share/mime/packages" "$root/DEBIAN"
cp -a "$appdir/." "$root/opt/photoslop/"
# A wrapper rather than a symlink: AppRun finds its libraries relative to its own path.
cat > "$root/usr/bin/photoslop" <<'SH'
#!/bin/sh
exec /opt/photoslop/AppRun "$@"
SH
chmod 755 "$root/usr/bin/photoslop"
here=$(dirname "$0")
sed 's/^Exec=.*/Exec=photoslop %F/' "$here/photoslop.desktop" > "$root/usr/share/applications/photoslop.desktop"
cp "$here/../../resources/icons/app.svg" "$root/usr/share/icons/hicolor/scalable/apps/photoslop.svg"
cp "$here/photoslop-mime.xml" "$root/usr/share/mime/packages/photoslop.xml"

size=$(du -sk "$root" | cut -f1)
cat > "$root/DEBIAN/control" <<CONTROL
Package: photoslop
Version: $version
Section: graphics
Priority: optional
Architecture: amd64
Installed-Size: $size
Depends: libc6, libgl1, libegl1, libfontconfig1, libfreetype6, libx11-6, libx11-xcb1, libxcb1, libxkbcommon0, libxkbcommon-x11-0, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render-util0, libxcb-shape0, libxcb-xinerama0, libxcb-xkb1, libdbus-1-3
Maintainer: PhotoSlop <noreply@photoslop.invalid>
Homepage: https://github.com/
Description: Layered raster image editor
 PhotoSlop is a cross-platform raster image editor whose layout, behaviour
 and keyboard shortcuts feel familiar to anyone who has used a traditional
 image editor. It opens and saves layered Photoshop (.psd) files.
CONTROL
cat > "$root/DEBIAN/postinst" <<'SH'
#!/bin/sh
set -e
command -v update-mime-database >/dev/null && update-mime-database /usr/share/mime || true
command -v update-desktop-database >/dev/null && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t /usr/share/icons/hicolor || true
SH
cp "$root/DEBIAN/postinst" "$root/DEBIAN/postrm"
chmod 755 "$root/DEBIAN/postinst" "$root/DEBIAN/postrm"
dpkg-deb --build --root-owner-group "$root" "$out"
