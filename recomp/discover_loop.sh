#!/usr/bin/env bash
# Dynamic discovery: run headless, add the missing target to entries.txt, re-translate, rebuild; repeat.
#   discover_loop.sh MS [KEYS]   (stops when a run ends for any reason other than a missing target)
set -uo pipefail
cd "$(dirname "$0")"
MS=${1:-3000}; KEYS=${2:-}
for i in $(seq 1 200); do
  rm -f missing.txt
  out=$(./build-rc/rc_headless --data run --ms "$MS" --keys "$KEYS" --missing missing.txt --png run_last.png)
  echo "[$i] $out"
  if [[ ! -s missing.txt ]]; then break; fi
  cat missing.txt >> entries.txt
  sort -u entries.txt -o entries.txt
  ./regen.sh > /dev/null || exit 1
done
