#!/usr/bin/env bash
# Regenerate everything derived from the user's executable and the reference capture, then build.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p gen ../work
KB=recompiler
# The reference machine at the unpacked entry: the committed copy has the program area blanked; the
# shareware EXE's own unpackers rebuild it (identical to the DOSBox-X capture).
REF=../work/reference_entry.bin
python3 "$KB/unpack_reference.py" ../original/BALLOONC.EXE ../reference/mem_entry.bin "$REF" --load-seg 0x0824 --entry 21E5:0944 > /dev/null
python3 "$KB/make_bootenv.py" "$REF" gen/bootenv.cpp --psp 0x0814 --first-mcb 0x0450 \
  --regs "ax=0 bx=0 cx=0 dx=F000 si=2A3F di=0800 bp=0 sp=0800 ds=0814 es=0814 ss=2A3F cs=21E5 ip=0944 flags=3206" \
  --vga-log ../reference/VGA.TXT --break-log ../reference/BREAK.TXT
python3 "$KB/recompile.py" ../work/unpacked/BALLOONC.real.exe gen --load-seg 0x0824 --ref-mem "$REF" --entries entries.txt \
  | python3 -c "import json,sys; d=json.load(sys.stdin); print('blocks', d['blocks'], 'unsupported', d['unsupported'])"
# Main-body regions and module procedures (lifted drafts, verified with --verify-native; not in git)
python3 ../tools/gen_regions.py 01C4 0700 053C > /dev/null
python3 ../tools/gen_regions.py 0700 CCC0 0800 > /dev/null
python3 ../tools/gen_modules.py > /dev/null
cmake -S . -B build-rc > /dev/null
cmake --build build-rc -j > build.log 2>&1 || { grep -E "error" build.log | head -20; exit 1; }
echo build ok
