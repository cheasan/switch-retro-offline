#!/bin/bash
# Automated desktop tests for ra-offline Phase 2+3 (no Switch needed).
# Starts the host build of the loopback server and drives it with curl.
set -u
cd "$(dirname "$0")/.."

PORT=8080
BASE=http://127.0.0.1:$PORT
PASS=0
FAIL=0

expect() { # name expected_substring actual
    if echo "$3" | grep -qF "$2"; then
        PASS=$((PASS+1)); echo "ok   - $1"
    else
        FAIL=$((FAIL+1)); echo "FAIL - $1"; echo "       wanted: $2"; echo "       got:    $3"
    fi
}

expect_eq() { # name expected exact
    if [ "$2" = "$3" ]; then
        PASS=$((PASS+1)); echo "ok   - $1"
    else
        FAIL=$((FAIL+1)); echo "FAIL - $1"; echo "       wanted: [$2]"; echo "       got:    [$3]"
    fi
}

rm -f test/root/queue.json
make host-test >/dev/null || exit 1
(setsid ./build-host/ra-test > /tmp/opencode/ra-test-server.log 2>&1 &)
sleep 0.7

# Phase 2 sanity
expect "ping"            "pong"                 "$(curl -s $BASE/ping)"
expect "health phase 4"  '"phase":4'            "$(curl -s $BASE/health)"
expect "404"             "not found"            "$(curl -s $BASE/nope)"

# Phase 3: login
expect "login identity"  '"User":"sam"'         "$(curl -s -X POST "$BASE/dorequest.php?r=login2")"
expect "login token"     '"Token":"testtoken123"' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=login')"
expect "login score"     '"Score":1234'         "$(curl -s -X POST "$BASE/dorequest.php?r=login2")"

# Phase 3: game identification
expect "known hash -> 1234" '"GameID":1234'     "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=gameid&m=deadbeefdeadbeefdeadbeefdeadbeef')"
expect "uppercase hash ok"  '"GameID":5678'     "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=gameid&m=00112233445566778899AABBCCDDEEFF')"
expect "unknown hash"       '"Success":false'   "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=gameid&m=00000000000000000000000000000000')"

# Phase 3: patch serving
expect "patch verbatim title" '"ra-offline Test Game"' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=patch&g=1234')"
expect "patch has achievement 111" '"Test Achievement"' "$(curl -s -X POST "$BASE/dorequest.php?r=patch&g=1234")"
expect "patch missing"     '"not cached offline"' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=patch&g=9999')"

# Phase 3: unlock queueing
expect "award ack"         '"AwardedAchievementID":111' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=awardachievement&u=sam&t=testtoken123&a=111&h=1&m=abc123&v=cafe')"
expect "award ack 2"       '"AwardedAchievementID":112' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=awardachievement&u=sam&t=testtoken123&a=112&h=1&m=def456&v=beef')"

# queue file must contain both, verbatim params
Q=$(cat test/root/queue.json 2>/dev/null)
expect "queue has a=111"   'a=111&h=1'     "$Q"
expect "queue has a=112"   'a=112&h=1'     "$Q"
expect "queue is JSONL"    'awardachievement&u=sam&t=testtoken123' "$Q"
LINES=$(grep -c . test/root/queue.json 2>/dev/null || echo 0)
expect "queue has 2 lines" "2" "$LINES"

# generic endpoints
expect "startsession"   '"Success":true' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=startsession&g=1234')"
expect "ping"           '"Success":true' "$(curl -s -X POST "$BASE/dorequest.php" -d 'r=ping&g=1234')"

# ---------------------------------------------------------------------
# Phase 4: sync to the real server (faked here)
# ---------------------------------------------------------------------
FAKE_LOG=/tmp/opencode/fake_ra.log
: > "$FAKE_LOG"
RAOFFLINE_UPSTREAM=127.0.0.1:9090 python3 test/fake_ra_server.py &
FAKE_PID=$!
sleep 0.5

# drain the two unlocks queued by the phase-3 section above
expect "sync: drain phase-3 queue" '"synced":2' "$(curl -s "$BASE/sync-now")"

# queue two unlocks while "offline", then tell the module to sync now
curl -s -X POST "$BASE/dorequest.php" -d 'r=awardachievement&u=sam&t=testtoken123&a=111&h=1&m=m111&v=v111' >/dev/null
curl -s -X POST "$BASE/dorequest.php" -d 'r=awardachievement&u=sam&t=testtoken123&a=112&h=1&m=m112&v=v112' >/dev/null
expect "sync: both sent"  '"synced":2' "$(curl -s "$BASE/sync-now")"
expect "sync: none left"  '"left":0'   "$(curl -s "$BASE/sync-now")"
expect "fake got a=111"   'a=111&h=1'  "$(cat $FAKE_LOG)"
expect "fake got a=112"   'a=112&h=1'  "$(cat $FAKE_LOG)"
expect_eq "queue file emptied" "ok" "$(test ! -s test/root/queue.json && echo ok)"

# failing server: queue must survive
kill $FAKE_PID 2>/dev/null; wait $FAKE_PID 2>/dev/null
curl -s -X POST "$BASE/dorequest.php" -d 'r=awardachievement&u=sam&t=testtoken123&a=113&h=1&m=m113&v=v113' >/dev/null
expect "sync: transport fail keeps queue" '"failed":1' "$(curl -s "$BASE/sync-now")"
expect "queue kept a=113"                 'a=113&h=1'  "$(cat test/root/queue.json 2>/dev/null)"

# server rejecting payloads: retried next pass, still kept
FAKE_FAIL_FIRST=1 RAOFFLINE_UPSTREAM=127.0.0.1:9090 python3 test/fake_ra_server.py &
FAKE_PID=$!
sleep 0.5
expect "sync: server rejects -> failed" '"failed":1' "$(curl -s "$BASE/sync-now")"
expect "queue still has a=113"          'a=113&h=1'  "$(cat test/root/queue.json 2>/dev/null)"
expect "sync: retry succeeds"           '"synced":1' "$(curl -s "$BASE/sync-now")"
expect_eq "queue finally empty" "ok" "$(test ! -s test/root/queue.json && echo ok)"
kill $FAKE_PID 2>/dev/null; wait $FAKE_PID 2>/dev/null

pkill -x ra-test
echo
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ]
