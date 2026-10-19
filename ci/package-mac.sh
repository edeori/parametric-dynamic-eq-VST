#!/usr/bin/env bash
set -euo pipefail
export PARADYNEQ_ENABLE_LICENSING=ON
export CODESIGN_IDENTITY="${CODESIGN_IDENTITY:-Developer ID Application: Márton Ferenczi (Y8272MS92K)}"
export INSTALLER_IDENTITY="${INSTALLER_IDENTITY:-Developer ID Installer: Márton Ferenczi (Y8272MS92K)}"
export NOTARY_PROFILE="${NOTARY_PROFILE:-ci-notary}"
bash ./build.sh
python3 ci/verify-licensed.py build "build/VST3/ParaDyn EQ.vst3/Contents/MacOS/ParaDyn EQ" "build/AU/ParaDyn EQ.component/Contents/MacOS/ParaDyn EQ" "build/Standalone/ParaDyn EQ.app/Contents/MacOS/ParaDyn EQ"
bash ./package.sh --skip-build --notarize
mkdir -p dist
version=$(sed -nE 's/^[[:space:]]*project\(ParaDynEQ VERSION ([^ )]+).*/\1/p' CMakeLists.txt)
cp "build/Release/ParaDyn EQ-${version}.pkg" dist/installer.pkg
