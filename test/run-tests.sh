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

rm -f test/root/queue.json
make host-test >/dev/null || exit 1
(setsid ./build-host/ra-test > /tmp/opencode/ra-test-server.log 2>&1 &)
sleep 0.7

# Phase 2 sanity
expect "ping"            "pong"                 "$(curl -s $BASE/ping)"
expect "health phase 3"  '"phase":3'            "$(curl -s $BASE/health)"
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

pkill -x ra-test
echo
echo "passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ]
