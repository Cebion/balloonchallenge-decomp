#!/usr/bin/env bash
# Golden capture (DOSBox-X), port run and comparison for recorded sessions: tools/verify_sessions.sh NAME...
# Results: work/ls/play_NAME{,_port}/ and work/ls/play_NAME.result
set -uo pipefail
cd "$(dirname "$0")/.."
for n in "$@"; do
  s=sessions/play/$n.json; g=work/ls/play_$n; p=work/ls/play_${n}_port
  python3 -c "import json; d=json.load(open('$s')); d['breaks']=['19AE:59D3','0824:22FC','1932:005A','0824:B143','0824:B5CF']; json.dump(d, open('$s','w'))"
  rm -rf "$g"
  python3 tools/lockstep.py golden "$s" "$g" > "$g.golden.log" 2>&1
  python3 tools/lockstep.py port "$s" "$p" --golden "$g" > "$p.log" 2>&1
  python3 tools/lockstep.py compare "$g" "$p" > "work/ls/play_$n.result" 2>&1
  echo "== $n: $(grep -c ': equal' work/ls/play_$n.result) equal; $(grep -E 'DIFFERENT|compared|speaker' work/ls/play_$n.result | head -3 | tr '\n' ' ')"
done
