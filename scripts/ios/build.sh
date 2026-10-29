#!/bin/bash
# SpeedBreaker. GPL-3.0-or-later (see COPYING).
#
# scripts/ios/build.sh [install]: SpeedBreaker.app for iPad and iPhone
# (arm64, iOS 17 or later), in build/ios/xcode/Build/Products/Release-iphoneos.
#   1. CMake and Ninja compile the game and the runtime for iOS into static
#      libraries (build/ios).
#   2. xcodebuild links them with SDL3, glslang, FFmpeg-XMA and MoltenVK
#      (static, no Vulkan loader) into the app and signs it (automatic
#      signing: Xcode's account for the team, and the device registered).
#   3. "install": devicectl installs it on the connected device.
# Settings, from the environment:
#   IOS_DEPS            the iOS dependencies, made by build_deps.sh (README.md):
#                       IOS_DEPS/prefix (SDL3, glslang, FFmpeg-XMA) and
#                       IOS_DEPS/MoltenVK
#   DEVELOPMENT_TEAM    the Apple developer team that signs it
#   IOS_BUNDLE_ID       the app's bundle identifier (unique to the team)
#   IOS_DEVICE          the device to install on (`xcrun devicectl list
#                       devices`; the only connected one by default)
#   IOS_UNSIGNED=1      an app nobody signed, for make_ipa.sh (an IPA that
#                       players sideload): no team, no account, no profile;
#                       in build/ios/xcode-unsigned. IOS_BUNDLE_ID is then
#                       optional (a generic one), and "install" refused.
#   SB_GAME             mw (the default: NFS: Most Wanted, ppc/, build/ios)
#                       or carbon (NFS: Carbon, ppc-carbon/, build/ios-carbon):
#                       "SpeedBreaker Carbon" (SpeedBreaker/Info-carbon.plist)
#                       with its own icon (AppIcon-carbon), its own version
#                       (SB_VERSION_CARBON) and, to install beside Most
#                       Wanted's, its own IOS_BUNDLE_ID (the generic one for
#                       IOS_UNSIGNED=1 is io.github.speedbreakerproject.
#                       SpeedBreakerCarbon). The app inside is SpeedBreaker.app
#                       for both.
# No game data: the game installs from the player's disc image on the device.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export DEVELOPER_DIR=${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}
: "${IOS_DEPS:?set IOS_DEPS: the folder scripts/ios/build_deps.sh made (scripts/ios/README.md)}"
UNSIGNED=${IOS_UNSIGNED:-0}
GAME=${SB_GAME:-mw}
case $GAME in
    mw) BUILD=$ROOT/build/ios; GENERIC_ID=io.github.speedbreakerproject.SpeedBreaker; GAME_SETTINGS=() ;;
    carbon) BUILD=$ROOT/build/ios-carbon; GENERIC_ID=io.github.speedbreakerproject.SpeedBreakerCarbon
        GAME_SETTINGS=(INFOPLIST_FILE=SpeedBreaker/Info-carbon.plist ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon-carbon) ;;
    *) echo "build.sh: SB_GAME=$GAME: mw or carbon" >&2; exit 2 ;;
esac
if [ "$UNSIGNED" = 1 ]; then
    [ "${1:-}" = install ] && { echo "build.sh: IOS_UNSIGNED=1 makes an app nobody signed, which no device installs: make_ipa.sh packages it for sideloading" >&2; exit 2; }
    # Sideloading tools give it an identifier of the player's own anyway.
    IOS_BUNDLE_ID=${IOS_BUNDLE_ID:-$GENERIC_ID}
else
    : "${DEVELOPMENT_TEAM:?set DEVELOPMENT_TEAM: your Apple developer team ID (scripts/ios/README.md)}"
    : "${IOS_BUNDLE_ID:?set IOS_BUNDLE_ID: a bundle identifier of your own, such as com.yourname.SpeedBreaker}"
fi
PREFIX=$IOS_DEPS/prefix
MOLTENVK=$IOS_DEPS/MoltenVK
JOBS=${JOBS:-6}

if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSB_GAME="$GAME" \
        -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
        -DSDL3_DIR="$PREFIX/lib/cmake/SDL3" -Dglslang_DIR="$PREFIX/lib/cmake/glslang" \
        -DFFMPEG_XMA="$PREFIX" -DMOLTENVK_DIR="$MOLTENVK"
fi
# A build folder is one game's (a folder made before SB_GAME existed is Most Wanted's).
BUILT_GAME=$(sed -n 's/^SB_GAME:STRING=//p' "$BUILD/CMakeCache.txt")
[ "${BUILT_GAME:-mw}" = "$GAME" ] || { echo "build.sh: $BUILD is SB_GAME=$BUILT_GAME's, not $GAME's" >&2; exit 1; }
ninja -C "$BUILD" -j"$JOBS"

# The runtime's archive whole (static constructors register things nobody
# references by name); the rest as the linker needs them. SDL finds
# vkGetInstanceProcAddr in the app itself with dlsym, so it must survive
# dead stripping.
LIBS=(
    -force_load "$BUILD/runtime/libSpeedBreaker.a"
    "$BUILD/recomp/libNfsmwRecompLib.a" "$BUILD/runtime/libXenonUtilsLite.a" "$BUILD/runtime/libo1heap.a"
    "$BUILD/runtime/libimgui.a"
    "$PREFIX/lib/libSDL3.a"
    "$PREFIX/lib/libglslang.a" "$PREFIX/lib/libglslang-default-resource-limits.a"
    "$PREFIX/lib/libavcodec.a" "$PREFIX/lib/libavutil.a"
    "$MOLTENVK/MoltenVK/static/MoltenVK.xcframework/ios-arm64/libMoltenVK.a"
    -Wl,-u,_vkGetInstanceProcAddr
    -lc++
)
# Most Wanted's PAL (Spanish) release, when the build has it, after the
# USA's (an if, not $( [ -f ] && echo ): without the file, that substitution
# fails and set -e ended the script there, silently).
if [ -f "$BUILD/recomp/libNfsmwRecompPalEsLib.a" ]; then
    LIBS=("${LIBS[@]:0:3}" "$BUILD/recomp/libNfsmwRecompPalEsLib.a" "${LIBS[@]:3}")
fi
for lib in SPIRV MachineIndependent GenericCodeGen OSDependent; do
    [ -f "$PREFIX/lib/lib$lib.a" ] && LIBS+=("$PREFIX/lib/lib$lib.a")
done
for fw in AVFoundation AudioToolbox CoreAudio CoreBluetooth CoreFoundation CoreGraphics CoreMedia CoreMotion \
          CoreVideo Foundation GameController IOSurface Metal OpenGLES QuartzCore UIKit UniformTypeIdentifiers; do
    LIBS+=(-framework "$fw")
done
LIBS+=(-weak_framework CoreHaptics)

# The app's version is project()'s (the top-level CMakeLists.txt); its build
# number the commit's number in the history, which grows from one release to
# the next.
VERSION=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt")
BUILD_NUMBER=$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)

# Signed for the team (Xcode's automatic signing makes the profile), or for
# make_ipa.sh not at all: then no account is asked anything.
if [ "$UNSIGNED" = 1 ]; then
    DERIVED=$BUILD/xcode-unsigned
    SIGNING=(CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY= DEVELOPMENT_TEAM=)
else
    DERIVED=$BUILD/xcode
    SIGNING=(-allowProvisioningUpdates DEVELOPMENT_TEAM="$DEVELOPMENT_TEAM")
fi
xcodebuild -project "$ROOT/scripts/ios/SpeedBreaker.xcodeproj" -scheme SpeedBreaker -configuration Release \
    -destination generic/platform=iOS -derivedDataPath "$DERIVED" -quiet "${SIGNING[@]}" \
    ${GAME_SETTINGS[@]+"${GAME_SETTINGS[@]}"} PRODUCT_BUNDLE_IDENTIFIER="$IOS_BUNDLE_ID" \
    MARKETING_VERSION="${VERSION:-0.0.0}" CURRENT_PROJECT_VERSION="$BUILD_NUMBER" \
    OTHER_LDFLAGS="${LIBS[*]}" build
APP=$DERIVED/Build/Products/Release-iphoneos/SpeedBreaker.app
# Counted, not grep -q: grep -q stops reading nm's long output at the first
# match, and with pipefail nm's SIGPIPE then fails the pipe (a false alarm).
[ "$(nm -gU "$APP/SpeedBreaker" | grep -c ' _vkGetInstanceProcAddr$')" != 0 ] || { echo "build.sh: vkGetInstanceProcAddr was stripped" >&2; exit 1; }
echo "$APP (v${VERSION:-?}, build $BUILD_NUMBER, $(du -sh "$APP" | cut -f1))"
[ "$GAME" = mw ] || echo "  $(plutil -extract CFBundleDisplayName raw "$APP/Info.plist"), $IOS_BUNDLE_ID"

if [ "${1:-}" = install ]; then
    DEVICE=${IOS_DEVICE:-$(xcrun devicectl list devices 2>/dev/null | grep 'available (paired)' | grep -oE '[0-9A-F]{8}(-[0-9A-F]{4}){3}-[0-9A-F]{12}' | head -1)}
    : "${DEVICE:?no connected device}"
    xcrun devicectl device install app --device "$DEVICE" "$APP"
fi
