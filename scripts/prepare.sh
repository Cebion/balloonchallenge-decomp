#!/usr/bin/env bash
# Prepare a build from the Balloon Challenge v2.0 shareware files, then generate and build the port:
#   scripts/prepare.sh PATH     PATH: ballon20.zip, or a folder with the game files
# Needs: python3 with iced-x86 and unicorn (pip install --user iced-x86 unicorn), deark >= 1.7 (PKLITE and EXEPACK unpacking),
# cmake, a C++17 compiler, SDL2 development files. The game files go to original/ and work/ (both gitignored),
# never into the repository.
set -euo pipefail
src=${1:?usage: scripts/prepare.sh ballon20.zip|FOLDER}
cd "$(dirname "$0")/.."
mkdir -p original work/unpacked
if [ -d "$src" ]; then
  for f in "$src"/*; do cp "$f" original/; done
else
  mkdir -p original/zip && cp "$src" original/zip/ballon20.zip
  python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$src" original
fi
# The exact shareware v2.0 program this port is generated from
expect=aa152b8b064ef6d85f01b276f603e9cb61d748ae615d77e5c8c006b9f1bdf1e8
got=$(sha256sum original/BALLOONC.EXE | cut -d' ' -f1)
[ "$got" = "$expect" ] || { echo "original/BALLOONC.EXE is not Balloon Challenge v2.0 shareware (sha256 $got)" >&2; exit 1; }
# Two packing layers: PKLITE, then EXEPACK
tmp=$(mktemp -d)
deark -q -m pklite -od "$tmp" original/BALLOONC.EXE
mkdir -p "$tmp/x"
deark -q -m exepack -od "$tmp/x" "$tmp"/output.000.exe
cp "$tmp"/x/output.000.exe work/unpacked/BALLOONC.real.exe
rm -rf "$tmp"
expect_unpacked=0a542f79d2472478f8097cbc1016535bc62cfbe5cc46b4e7f4ec5b77df778d64
got=$(sha256sum work/unpacked/BALLOONC.real.exe | cut -d' ' -f1)
[ "$got" = "$expect_unpacked" ] || { echo "unpacked image differs (sha256 $got); check the deark version" >&2; exit 1; }
# Lockstep and recording tools run the game from a copy of its files
mkdir -p work/dos/c && cp original/BALLOON* work/dos/c/
recomp/regen.sh
echo "built: recomp/build-rc/rc_play (play: recomp/build-rc/rc_play --data original)"
