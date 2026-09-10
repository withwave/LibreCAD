#!/bin/bash -e
#
# Build, bundle, and codesign LibreCAD.app on macOS (Apple Silicon / Intel).
#
# This script does NOT modify any upstream files. It builds with the standard
# CMake/Ninja flow and assembles the .app bundle as post-processing, mirroring
# the ghostty/wavetty release philosophy (zero merge-conflict risk on rebase).
#
# Requirements (Homebrew): qt, boost, freetype, ninja, cmake
#
# Usage:
#   scripts/build-mac-signed.sh
#       -> build + bundle in build/package.noindex + ad-hoc sign
#
#   CODESIGN_IDENTITY="Developer ID Application: MODIN COMPANY (8AC9KUZJ5P)" \
#       scripts/build-mac-signed.sh
#       -> build + bundle + Developer ID sign (hardened runtime + timestamp)
#
#   CODESIGN_IDENTITY="..." scripts/build-mac-signed.sh --dmg
#       -> also produce a signed LibreCAD.dmg
#
#   CODESIGN_IDENTITY="..." NOTARY_PROFILE="modin-notary" \
#       scripts/build-mac-signed.sh --dmg --notarize
#       -> submit the DMG to Apple notarization and staple the ticket
#
# List signing identities with:  security find-identity -v -p codesigning

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$SRC_DIR"

QT_PREFIX="$(brew --prefix qt)"
BUILD_DIR="${BUILD_DIR:-build}"
# Keep development bundles out of Spotlight application searches.
APP="$BUILD_DIR/package.noindex/LibreCAD.app"
DMG="LibreCAD.dmg"
VERSION="$(git describe --always 2>/dev/null || echo 2.2.2)"
IDENTITY="${CODESIGN_IDENTITY:--}"          # default: ad-hoc '-'
BUNDLE_ID="${BUNDLE_ID:-org.librecad.LibreCAD}"

MAKE_DMG=0
NOTARIZE=0
for arg in "$@"; do
    case "$arg" in
        --dmg)      MAKE_DMG=1 ;;
        --notarize) NOTARIZE=1 ;;
    esac
done

echo "==> Qt:        $QT_PREFIX"
echo "==> Version:   $VERSION"
echo "==> Identity:  $IDENTITY"
echo "==> Bundle ID: $BUNDLE_ID"

# 1. Configure + build ------------------------------------------------------
cmake -G Ninja -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX;$(brew --prefix boost);$(brew --prefix freetype)"
ninja -C "$BUILD_DIR"

# 2. Assemble the .app bundle ----------------------------------------------
echo "==> Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BUILD_DIR/librecad" "$APP/Contents/MacOS/librecad"
cp librecad/res/images/librecad.icns "$APP/Contents/Resources/librecad.icns"
# RS_System searches Contents/Resources for CAD fonts and support files.
# Qt's macdeployqt only deploys Qt dependencies; without the LFF fonts,
# RS_MText::update() cannot create glyphs (including dimension labels).
for resource in fonts patterns library; do
    cp -R "librecad/support/$resource" "$APP/Contents/Resources/"
done
mkdir -p "$APP/Contents/Resources/qm"
cp "$BUILD_DIR"/librecad_*.qm "$APP/Contents/Resources/qm/"
# Fail packaging before signing if the fallback font is missing or empty.
test -s "$APP/Contents/Resources/fonts/standard.lff"
sed -e "s/@ICON@/librecad.icns/" \
    -e "s/@FULL_VERSION@/$VERSION/g" \
    -e "s/@TYPEINFO@/????/" \
    -e "s/@EXECUTABLE@/librecad/" \
    -e "s#@BUNDLEIDENTIFIER@#$BUNDLE_ID#" \
    librecad/src/Info.plist.app > "$APP/Contents/Info.plist"

# 3. Bundle Qt frameworks + plugins into the .app --------------------------
echo "==> Running macdeployqt"
# Deploy the desktop plugins explicitly. Auto-discovery also picks up Qt's
# virtual keyboard and PDF image reader, whose optional QML/PDF frameworks
# may remain unresolved in Homebrew installations and load a second Qt.
PLUGIN_ARGS=()
for plugin in "$QT_PREFIX"/share/qt/plugins/platforms/libqcocoa.dylib \
              "$QT_PREFIX"/share/qt/plugins/styles/libqmacstyle.dylib \
              "$QT_PREFIX"/share/qt/plugins/iconengines/libqsvgicon.dylib \
              "$QT_PREFIX"/share/qt/plugins/imageformats/*.dylib \
              "$QT_PREFIX"/share/qt/plugins/tls/*.dylib; do
    [ -f "$plugin" ] || continue
    [ "$(basename "$plugin")" = "libqpdf.dylib" ] && continue
    plugin_dir="$APP/Contents/PlugIns/$(basename "$(dirname "$plugin")")"
    mkdir -p "$plugin_dir"
    cp "$plugin" "$plugin_dir/"
    PLUGIN_ARGS+=("-executable=$plugin_dir/$(basename "$plugin")")
done
"$QT_PREFIX/bin/macdeployqt" "$APP" -verbose=1 -no-codesign -no-plugins \
    -libpath="$QT_PREFIX/lib" -libpath="$(brew --prefix)/lib" "${PLUGIN_ARGS[@]}"

# 4. Codesign ---------------------------------------------------------------
echo "==> Codesigning"
if [ "$IDENTITY" = "-" ]; then
    codesign --force --deep --sign - "$APP"
else
    codesign --force --deep --options runtime --timestamp \
        --sign "$IDENTITY" "$APP"
fi
codesign --verify --deep --strict --verbose=2 "$APP"
echo "==> Signed $APP:"
codesign -dv "$APP" 2>&1 | grep -E "Identifier|Authority|TeamIdentifier|flags" || true

# 5. Optional DMG -----------------------------------------------------------
if [ "$MAKE_DMG" = "1" ]; then
    echo "==> Creating $DMG"
    rm -f "$DMG"
    hdiutil create -volname "LibreCAD" -srcfolder "$APP" -ov -format UDZO "$DMG"
    if [ "$IDENTITY" != "-" ]; then
        codesign --force --timestamp --sign "$IDENTITY" "$DMG"
    fi
fi

# 6. Optional notarization --------------------------------------------------
if [ "$NOTARIZE" = "1" ]; then
    : "${NOTARY_PROFILE:?set NOTARY_PROFILE (xcrun notarytool store-credentials ...)}"
    [ "$MAKE_DMG" = "1" ] || { echo "notarization requires --dmg"; exit 1; }
    echo "==> Submitting $DMG to Apple notarization"
    xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$DMG"
fi

echo "==> Done."
[ -d "$APP" ] && du -sh "$APP"
[ "$MAKE_DMG" = "1" ] && ls -lh "$DMG"
exit 0
