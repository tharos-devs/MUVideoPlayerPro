#!/bin/bash
set -euo pipefail

# Construit MUVideoPlayerPro-<version>.pkg à partir d'un .vst3 déjà buildé
# (et déjà rendu autonome via dylibbundler, cf. le workflow CI -- ce script
# ne fait que l'empaqueter, il ne touche pas aux dylibs).
#
# Usage: build-pkg.sh <chemin-vers-MUVideoPlayerPro.vst3> <version, ex 0.1.0> <chemin-du-.pkg-en-sortie>

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <path-to-MUVideoPlayerPro.vst3> <version> <output.pkg>" >&2
    exit 1
fi

VST3_SRC="$1"
VERSION="$2"
OUTPUT_PKG="$3"

if [ ! -d "$VST3_SRC" ]; then
    echo "Erreur: $VST3_SRC introuvable" >&2
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

PAYLOAD_DIR="$WORK_DIR/payload"
SCRIPTS_DIR="$WORK_DIR/scripts"

mkdir -p "$PAYLOAD_DIR/Library/Audio/Plug-Ins/VST3"
mkdir -p "$SCRIPTS_DIR"

cp -R "$VST3_SRC" "$PAYLOAD_DIR/Library/Audio/Plug-Ins/VST3/"
cp "$SCRIPT_DIR/postinstall" "$SCRIPTS_DIR/postinstall"
chmod 755 "$SCRIPTS_DIR/postinstall"

pkgbuild \
    --root "$PAYLOAD_DIR" \
    --scripts "$SCRIPTS_DIR" \
    --identifier "com.tharos-devs.MUVideoPlayerPro" \
    --version "$VERSION" \
    --install-location / \
    "$OUTPUT_PKG"

echo "Package créé : $OUTPUT_PKG"
