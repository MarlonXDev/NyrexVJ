#!/bin/bash
set -u
binary="${1:-build/NyrexVJ}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export NYREXVJ_DATA_DIR="$work/data"
mkdir -p "$NYREXVJ_DATA_DIR"
common=(--no-update-check --no-welcome)

fail() {
    echo "restart test FAILED: $1"
    exit 1
}

"$binary" "${common[@]}" --soak 300 > "$work/first.log" 2>&1 &
pid=$!
lock="$NYREXVJ_DATA_DIR/recovery/session.lock"
for _ in $(seq 1 120); do
    if [ -s "$lock" ] && grep -q "Soak " "$work/first.log"; then
        break
    fi
    kill -0 "$pid" 2> /dev/null || fail "the first run ended on its own: $(tail -3 "$work/first.log")"
    sleep 0.5
done
[ -s "$lock" ] || fail "no session file after 60 seconds"
kill -9 "$pid"
wait "$pid" 2> /dev/null
[ -s "$lock" ] || fail "the session file is gone after the process was killed"

timeout 120 "$binary" "${common[@]}" --frames 30 --screenshot "$work/second.png" > "$work/second.log" 2>&1
code=$?
[ "$code" -eq 0 ] || fail "the second run exited with $code: $(tail -3 "$work/second.log")"
[ -s "$work/second.png" ] || fail "the second run made no picture"
[ -s "$NYREXVJ_DATA_DIR/nyrexvj.previous.log" ] || fail "the log of the killed run was not kept"
grep -q "Soak " "$NYREXVJ_DATA_DIR/nyrexvj.previous.log" || fail "the kept log is not the log of the killed run"
[ ! -e "$lock" ] || fail "the second run did not end its own session cleanly"
echo "restart test passed: killed run, second run exit 0, picture written, log kept, session ended cleanly"
