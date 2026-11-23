#!/bin/bash
# Quiet-box contract gate run with its record (coordinator tool; see tools/README.md).
# Usage: tools/run_gate.sh ORACLE ORACLE_TREE START_HH:MM GIVE_UP_HH:MM
# Waits for START and the derived quiet condition (advisor, Wave 0 gate):
#  (a) no build/test/compiler/linker/Xvfb process of anyone; (b) no foreign job
#  matching GATE_FOREIGN (default the Paris-talk thread's /tmp/toy- jobs);
#  (c) 1-minute load <= GATE_MAX_LOAD (default 2.8: the load regime in which the
#  watchdog bounds were measured, (M) X3 and B4 runs at 2.3-2.8 [AC]).
# Gives up at GIVE_UP without running. Runs `make contract-gate` exactly once.
set -u
[ $# -eq 4 ] || { echo "usage: $0 ORACLE ORACLE_TREE START_HH:MM GIVE_UP_HH:MM" >&2; exit 2; }
ORACLE=$1 ORACLE_TREE=$2
cd "$(dirname "$0")/.." || exit 9
mkdir -p build/gate
OUT=build/gate/record.txt
BUILD_RE='make|gcc|clang|cc1|ld|ld.bfd|ld.gold|collect2|Xvfb|clang-18|gcc-13'
FOREIGN=${GATE_FOREIGN:-/tmp/toy-}
MAX_LOAD=${GATE_MAX_LOAD:-2.8}
start_at=$(date -d "$3" +%s) give_up=$(date -d "$4" +%s)
builds() { pgrep -x "$BUILD_RE" | wc -l; }
foreign() { pgrep -f "$FOREIGN" | grep -vx "$$" | wc -l; }
while :; do
  now=$(date +%s) load=$(cut -d' ' -f1 /proc/loadavg)
  if [ "$now" -ge "$give_up" ]; then
    echo "GAVE UP at $(date) load $load builds $(builds) foreign $(foreign): not started" | tee -a "$OUT"; exit 8
  fi
  if [ "$now" -ge "$start_at" ] && [ "$(builds)" -eq 0 ] && [ "$(foreign)" -eq 0 ] && \
     awk -v l="$load" -v m="$MAX_LOAD" 'BEGIN{exit !(l <= m)}'; then break; fi
  sleep 15
done
digests() { for f in "$@"; do [ -f "$f" ] && echo "  $(sha256sum "$f")"; done; }
{
  echo "== contract gate run"
  echo "start: $(date '+%F %T %Z')"
  echo "uptime start: $(uptime)"
  echo "condition (a) build/test processes: $(builds)"
  echo "condition (b) foreign jobs ($FOREIGN): $(foreign)"
  echo "condition (c) 1-min load: $(cut -d' ' -f1 /proc/loadavg) (<= $MAX_LOAD)"
  echo "top processes at start:"; ps -eo pid,ni,pcpu,etime,comm --sort=-pcpu | head -12
  echo "BAT0: $(cat /sys/class/power_supply/BAT0/status) $(cat /sys/class/power_supply/BAT0/capacity)%"
  echo "commit: $(git rev-parse HEAD) (status: $(git status --short | wc -l) changed paths)"
  echo "manifest sha256: $(sha256sum tests/contract/expectations.json | cut -d' ' -f1)"
  echo "oracle sha256:"; digests "$ORACLE"
} | tee -a "$OUT"
t0=$(date +%s)
make contract-gate ORACLE_TREE="$ORACLE_TREE" ORACLE="$ORACLE" > build/gate/make.log 2>&1
rc=$?
t1=$(date +%s)
{
  echo "runner/make exit: $rc"
  echo "wall: $((t1 - t0)) s"
  echo "end: $(date '+%F %T %Z')"
  echo "uptime end: $(uptime)"
  echo "inspector build sha256:"; digests "$ORACLE_TREE"/build/contract-oracle/*
  grep -E "contract gate|gate record|outcomes" build/gate/make.log | tail -5
} | tee -a "$OUT"
exit $rc
