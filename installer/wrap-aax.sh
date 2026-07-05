#!/bin/bash
# ============================================================================
#  FIZZFUEL — AAX PACE sign + Apple Dev ID sign + notarize for Pro Tools
#  ---------------------------------------------------------------------------
#  HUMAN-GATED: this performs PACE signing (needs your iLok/PACE account) and
#  Apple Developer ID signing. Run it yourself after a fresh Release build.
#  Re-run after EVERY rebuild — a fresh build strips the PACE signature and
#  Pro Tools will silently reject an unwrapped AAX.
#
#  Order matters (per Alex Gallo / MuseHub 2026-05-21): PRE-SIGN the unwrapped
#  bundle BEFORE wrapping, or wraptool's notarization manifest misses cdhashes
#  and Gatekeeper rejects with "Unnotarized Developer ID" even though the
#  Apple ticket is valid.
# ============================================================================
set -euo pipefail

WRAP="/Applications/PACEAntiPiracy/Eden/Fusion/Versions/5/bin/wraptool"
CERT="45C26CF1655F48EBC8A21802BDA053514719E1F0"     # Developer ID Application: Miguel Silverio
WCGUID="880A6450-78A7-11F1-B005-005056920FF7"       # FIZZFUEL PACE wrap config (Sign Only)
AAX="$HOME/Desktop/Octane/build/Octane_artefacts/Release/AAX/FIZZFUEL.aaxplugin"
NOTARY_PROFILE="${NOTARY_PROFILE:-AC_NOTARY}"        # xcrun notarytool keychain profile
PACE_ACCOUNT="${PACE_ACCOUNT:?set PACE_ACCOUNT=<your iLok/PACE account id> before running}"

[ -d "$AAX" ] || { echo "❌ AAX bundle not found — build Release first: cmake --build build --config Release"; exit 1; }

echo "▸ 1/5  Pre-signing the UNWRAPPED bundle (Dev ID) so all cdhashes land in the notarization manifest…"
codesign --force --deep --options runtime --timestamp --sign "$CERT" "$AAX"

echo "▸ 2/5  PACE wrap (sign-only config $WCGUID) + Apple sign via wraptool --signid…"
# First run downloads + caches the wrap config and stores the PACE password in the keychain.
"$WRAP" sign \
  --verbose \
  --account "$PACE_ACCOUNT" \
  --wcguid "$WCGUID" \
  --signid "$CERT" \
  --in  "$AAX" \
  --out "$AAX"

echo "▸ 3/5  Re-signing the wrapped bundle (Dev ID, hardened runtime)…"
codesign --force --deep --options runtime --timestamp --sign "$CERT" "$AAX"
codesign --verify --deep --strict --verbose=2 "$AAX"

echo "▸ 4/5  Notarizing + stapling…"
ZIP="/tmp/FIZZFUEL.aaxplugin.zip"
ditto -c -k --keepParent "$AAX" "$ZIP"
xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
xcrun stapler staple "$AAX"

echo "▸ 5/5  Gatekeeper verification (must say: accepted, source=Notarized Developer ID)…"
spctl -a -vv -t install "$AAX" || echo "⚠️  spctl rejected — check the pre-sign step and notarization ticket"

echo ""
echo "✅ Done. Wrapped + signed + notarized:"
echo "   $AAX"
echo "   Install to /Library/Application Support/Avid/Audio/Plug-Ins/ and load in Pro Tools to confirm."
