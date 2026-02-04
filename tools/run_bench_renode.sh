#!/usr/bin/env bash
set -eu

# Wrapper to run the bench Renode script and ensure automatic recovery if Renode hangs.
# Usage: ./tools/run_bench_renode.sh

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RESC="$REPO_ROOT/lib/umi/bench/target/stm32f4/renode/bench_stm32f4.resc"
LOG="$REPO_ROOT/build/bench_stm32f4_uart.log"
CONSOLE_LOG="$REPO_ROOT/build/renode_console.log"
TIMEOUT_SECONDS=120

cd "$REPO_ROOT"

# cleanup
rm -f "$LOG" "$CONSOLE_LOG"

# Start Renode in background
renode --console --disable-xwt -e "include @$RESC" > "$CONSOLE_LOG" 2>&1 &
RENODE_PID=$!

echo "Started Renode (pid=$RENODE_PID), waiting for bench output..."

FOUND=0
for i in $(seq 1 $TIMEOUT_SECONDS); do
    if [ -f "$LOG" ] && grep -q "=== Done ===" "$LOG" 2>/dev/null; then
        FOUND=1
        echo "Detected '=== Done ===' in $LOG"
        break
    fi
    sleep 1
done

# Ensure Renode exits: try graceful then force
if kill -0 "$RENODE_PID" 2>/dev/null; then
    echo "Stopping Renode (pid=$RENODE_PID)"
    kill "$RENODE_PID" || true
    # wait up to 5s
    for j in 1 2 3 4 5; do
        if ! kill -0 "$RENODE_PID" 2>/dev/null; then
            break
        fi
        sleep 1
    done
    if kill -0 "$RENODE_PID" 2>/dev/null; then
        echo "Renode still running; forcing kill"
        kill -9 "$RENODE_PID" || true
    fi
else
    echo "Renode already exited"
fi

echo "RENODE_EXITED"

echo "--- RENODE CONSOLE LOG (tail 200) ---"
tail -n 200 "$CONSOLE_LOG" || true

echo "--- BENCH UART LOG START ---"
if [ -f "$LOG" ]; then
    cat "$LOG"
else
    echo "(no log file found: $LOG)"
fi

echo "--- DONE ---"
