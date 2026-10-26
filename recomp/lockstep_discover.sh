#!/usr/bin/env bash
# Discovery in lockstep mode: run a key session, add the missing target, re-translate, rebuild; repeat.
#   lockstep_discover.sh BDA_KEYS STOP_KEYS STOPS
set -uo pipefail
cd "$(dirname "$0")"
for i in $(seq 1 100); do
  rm -f missing.txt; rm -rf ls_port; mkdir ls_port
  out=$(./build-rc/rc_headless --data run --ms 900000 --bda-keys "$1" --break 19AE:59D3,0824:22FC,1932:005A,0824:B143,0824:B5CF --stop-keys "$2" \
        --dump-dir ls_port --stops "$3" --missing missing.txt --png ls_port/end.png | tail -3)
  echo "[$i] $out"
  [[ -s missing.txt ]] || break
  cat missing.txt >> entries.txt; sort -u entries.txt -o entries.txt
  ./regen.sh > /dev/null || exit 1
done
