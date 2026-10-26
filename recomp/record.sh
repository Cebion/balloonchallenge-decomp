#!/usr/bin/env bash
# Record a lockstep scenario by playing: recomp/record.sh NAME [--registered]  -> sessions/play/NAME.json
# Each run starts from a fresh copy of the original game files.
# --registered: test state with the registration flag (DGROUP 010A) forced to 1 right after the startup
# check of BALLOONC.OV2 (0824:AEFE), in the recording and in the golden capture; no upgrade code involved.
set -euo pipefail
cd "$(dirname "$0")"
name=${1:?usage: record.sh NAME [--registered]}
poke=(); [[ ${2:-} == --registered ]] && poke=(--poke 0824:AEFE=257A:010A:0001)
rm -rf "run_rec_$name"; cp -r ../work/dos/c "run_rec_$name"
./build-rc/rc_play --data "run_rec_$name" --record "../sessions/play/$name.json" "${poke[@]}"
echo "saved sessions/play/$name.json"
