#!/bin/bash
# OCTANE macOS installer — productbuild wizard w/ branded sidebar (CA standard, 2026-05-23).
# Run AFTER the release build is signed (and AAX PACE-wrapped + re-signed).
# Usage: ./installer/build-installer.sh [demo]
set -euo pipefail

cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^project(Octane VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
MODE="${1:-paid}"

if [ "$MODE" = "demo" ]; then
  ART="build-demo/Octane_artefacts/Release"
  OUT="dist/OCTANE-v${VERSION}-Demo-Installer.pkg"
else
  ART="build/Octane_artefacts/Release"
  OUT="dist/OCTANE-v${VERSION}-Installer.pkg"
fi

[ -f installer/Resources/sidebar.png ] || { echo "MISSING installer/Resources/sidebar.png (plugin GUI portrait crop ~620x418)"; exit 1; }

mkdir -p dist

pkgbuild --component "$ART/VST3/OCTANE.vst3" \
         --install-location "/Library/Audio/Plug-Ins/VST3" \
         --identifier "com.carbonatedaudio.octane.vst3.pkg" \
         --version "$VERSION" \
         dist/OCTANE-VST3.pkg

pkgbuild --component "$ART/AU/OCTANE.component" \
         --install-location "/Library/Audio/Plug-Ins/Components" \
         --identifier "com.carbonatedaudio.octane.au.pkg" \
         --version "$VERSION" \
         dist/OCTANE-AU.pkg

pkgbuild --component "$ART/AAX/OCTANE.aaxplugin" \
         --install-location "/Library/Application Support/Avid/Audio/Plug-Ins" \
         --identifier "com.carbonatedaudio.octane.aax.pkg" \
         --version "$VERSION" \
         dist/OCTANE-AAX.pkg

productbuild --distribution installer/distribution.xml \
             --resources installer/Resources \
             --package-path dist \
             "$OUT"

rm -f dist/OCTANE-VST3.pkg dist/OCTANE-AU.pkg dist/OCTANE-AAX.pkg
echo "Built: $OUT"
echo "Next (human-gated): productsign --sign 'Developer ID Installer: ...' + notarytool submit + stapler staple"
