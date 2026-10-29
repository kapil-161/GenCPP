#!/bin/bash
# Gen2 (DSSAT Genetics Editor) macOS build and deployment script.
# Produces a self-contained, ad-hoc signed build_macos/bin/GeneticsEditor.app.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build_macos"
APP="$BUILD_DIR/bin/GeneticsEditor.app"
QUIET=0
[ "${1:-}" = "quiet" ] && QUIET=1

say() { [ $QUIET = 1 ] || echo "$@"; }
die() { echo "ERROR: $*" >&2; exit 1; }

QT_DIR=""
for d in /opt/homebrew/opt/qt6 /opt/homebrew/opt/qt /usr/local/opt/qt6 /usr/local/opt/qt; do
    [ -x "$d/bin/macdeployqt" ] && { QT_DIR="$d"; break; }
done
[ -n "$QT_DIR" ] || die "Qt6 not found. Install it with: brew install qt"
command -v cmake >/dev/null || die "CMake not found. Install it with: brew install cmake"
BREW_LIB="$(cd "$QT_DIR/.." && cd .. && pwd)/lib"   # e.g. /opt/homebrew/lib
say "Qt: $QT_DIR"

say "Step 1: Configuring..."
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$QT_DIR" -G "Unix Makefiles" >/dev/null

say "Step 2: Building..."
# macdeployqt rewrites the bundle in place, so always relink into a fresh one.
rm -rf "$APP"
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"
[ -d "$APP" ] || die "GeneticsEditor.app was not produced"

say "Step 3: Bundling Qt (macdeployqt)..."
# Homebrew's macdeployqt reports unresolved @rpath errors for optional plugins;
# the fix-ups below make the bundle complete, so its exit status is not trusted.
"$QT_DIR/bin/macdeployqt" "$APP" -always-overwrite -libpath="$BREW_LIB" >/dev/null 2>&1 || true

C="$APP/Contents"
FW="$C/Frameworks"
EXE="$C/MacOS/GeneticsEditor"
mkdir -p "$FW"

say "Step 4: Fixing up the bundle..."
# Plugins that pull in QML/Quick/Pdf/Svg stacks a Widgets editor never uses.
rm -f "$C/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib" \
      "$C/PlugIns/iconengines/libqsvgicon.dylib" \
      "$C/PlugIns/imageformats/libqpdf.dylib"
rmdir "$C/PlugIns/platforminputcontexts" "$C/PlugIns/iconengines" 2>/dev/null || true

macho_files() {
    echo "$EXE"
    find "$FW" "$C/PlugIns" -type f \( -name "*.dylib" -o -path "*.framework/Versions/A/*" \) \
        -not -path "*/Resources/*" -not -path "*/Headers/*" 2>/dev/null
}

# Pull every Qt framework and Homebrew dylib the bundle still needs into it,
# repeating until nothing new is copied (each new library can add dependencies).
for _ in 1 2 3 4 5 6 7 8; do
    changed=0
    while IFS= read -r f; do
        chmod u+w "$f"
        self_id="$(otool -D "$f" 2>/dev/null | tail -n +2)"
        while IFS= read -r dep; do
            [ "$dep" = "$self_id" ] && continue
            case "$dep" in
                @rpath/Qt*.framework/*)
                    name="${dep#@rpath/}"; name="${name%%/*}"
                    if [ ! -d "$FW/$name" ]; then
                        [ -d "$BREW_LIB/$name" ] || die "Cannot find $name in $BREW_LIB"
                        cp -R "$BREW_LIB/$name/" "$FW/$name"
                        rm -rf "$FW/$name/Headers" "$FW/$name/Versions/A/Headers"
                        say "  bundled $name"; changed=1
                    fi ;;
                /opt/homebrew/*|/usr/local/*)
                    if [[ "$dep" == *".framework/"* ]]; then
                        rel="${dep#*/lib/}"
                        new="@rpath/$rel"
                        name="${rel%%/*}"
                        if [ ! -d "$FW/$name" ]; then
                            cp -R "$BREW_LIB/$name/" "$FW/$name"
                            rm -rf "$FW/$name/Headers" "$FW/$name/Versions/A/Headers"
                            say "  bundled $name"
                        fi
                    else
                        base="$(basename "$dep")"
                        new="@executable_path/../Frameworks/$base"
                        if [ ! -f "$FW/$base" ]; then
                            cp "$dep" "$FW/$base"; chmod u+w "$FW/$base"
                            install_name_tool -id "$new" "$FW/$base" 2>/dev/null
                            say "  bundled $base"
                        fi
                    fi
                    install_name_tool -change "$dep" "$new" "$f" 2>/dev/null
                    changed=1 ;;
            esac
        done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
    done < <(macho_files)
    [ $changed = 0 ] && break
done

# Frameworks copied from Homebrew carry absolute install ids; make them @rpath.
for fwdir in "$FW"/Qt*.framework; do
    n="$(basename "$fwdir" .framework)"
    bin="$fwdir/Versions/A/$n"
    [ -f "$bin" ] && { chmod u+w "$bin"; install_name_tool -id "@rpath/$n.framework/Versions/A/$n" "$bin" 2>/dev/null; }
done

# The link step leaves an rpath to Homebrew Qt; point at the bundle instead.
while IFS= read -r rp; do
    case "$rp" in /opt/homebrew/*|/usr/local/*) install_name_tool -delete_rpath "$rp" "$EXE" 2>/dev/null ;; esac
done < <(otool -l "$EXE" | awk '/LC_RPATH/{getline;getline;print $2}')
otool -l "$EXE" | grep -q "@executable_path/../Frameworks" || \
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$EXE" 2>/dev/null

say "Step 5: Signing..."
# install_name_tool invalidates signatures; Apple Silicon kills unsigned code.
codesign --force --deep --sign - "$APP" 2>/dev/null
codesign --verify --deep --strict "$APP" || die "Code signature verification failed"

say "Step 6: Verifying..."
leaks="$(macho_files | while IFS= read -r f; do
    otool -L "$f" | tail -n +2 | awk '{print $1}' | { grep -E '^(/opt/homebrew|/usr/local)/' || true; } | sed "s|^|  ${f#$C/} -> |"
done)"
[ -z "$leaks" ] || die "Bundle still links outside itself:"$'\n'"$leaks"

"$EXE" >/dev/null 2>&1 & pid=$!
sleep 5
if kill -0 $pid 2>/dev/null; then
    kill $pid; wait $pid 2>/dev/null || true
else
    wait $pid && rc=0 || rc=$?
    die "App exited during launch test (status $rc)"
fi

say ""
say "SUCCESS: $APP ($(du -sh "$APP" | cut -f1))"
say "Run it with: open \"$APP\""
