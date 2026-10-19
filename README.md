# ra-offline — SwitchRetroOfflineProxy

An Atmosphere **sysmodule** (background system program) for the Nintendo Switch
that acts as an **offline loopback server for RetroAchievements**: RetroArch
talks to it as if it were retroachievements.org, unlock payloads get queued on
the SD card while offline, and everything syncs to the real server whenever
Wi-Fi comes back.

Plan source: `~/Downloads/switch_sysmodule_development_plan.md`

## Phase tracker

| Phase | Goal | Status |
|-------|------|--------|
| 1 | Sysmodule boilerplate, heap, SD logging, paced loop | code done, compiles — hardware test pending |
| 2 | HTTP loopback server on 127.0.0.1:8080 (bsd sockets + thread) | code done — desktop-tested |
| 3 | RetroArch API mocking, cache dir, unlock queue file | code done — desktop-tested |
| 4 | nifm network detection + background sync | **code done — 32/32 desktop tests pass** — hardware test pending |
| 5 | Helper script to point RetroArch's cheevos URL at 127.0.0.1:8080 | pending |

## Sync (Phase 4)

- Switch: main loop watches `nifm`; on Wi-Fi rising edge (plus a retry
  every 10 min while online) queued unlocks are replayed to
  https://retroachievements.org over the console's own TLS (`ssl:`
  service — no extra libraries).
- Host tests: `test/fake_ra_server.py` stands in for the real server
  (plain HTTP); the module syncs via `$RAOFFLINE_UPSTREAM`.
- Safety: transport failures and server rejections KEEP the payload
  queued (retry next pass); awards that arrive mid-sync are never
  clobbered by the queue rewrite.

## Pre-caching games (online, from the laptop)

```sh
# preferred: hash taken from RetroArch's own log ("cheevos hash: <md5>")
python3 tools/fetch-cache.py --user SAM --password secret \
    --game 1234 --hash deadbeef...:1234

# lays out sd/switch/raoffline/ (account.json, cache/hashes.txt,
# cache/<id>/patch.json). Copy sd/switch/ to the SD card root.
bash test/run-tests.sh    # automated regression suite (20 checks)
```

## Offline protocol map (Phase 3, /dorequest.php)

| r= param | served from |
|---|---|
| `login` / `login2` | account.json (offline identity + score) |
| `gameid&m=<md5>` | cache/hashes.txt lookup |
| `patch&g=<id>` | verbatim cache/<id>/patch.json |
| `startsession`, `ping`, `submitnewticket` | `{"Success":true}` |
| `awardachievement&a=..&h=..` | appended to queue.json, ack'd |

Unlock payloads are stored as JSON Lines with their RAW urlencoded
query/body so Phase 4 can replay them byte-for-byte.

## Desktop testing (no Switch required)

The HTTP server + routes ship in two builds from the same source files:
the sysmodule (devkitA64) and a Linux test binary (system gcc + ASan).

```sh
make host-test                 # build build-host/ra-test (AddressSanitizer)
make host-run                  # run it, then in another terminal:
curl http://127.0.0.1:8080/ping
curl -X POST 'http://127.0.0.1:8080/dorequest.php?r=login'
curl -X POST 'http://127.0.0.1:8080/dorequest.php' -d 'r=login&u=sam'
```

Verified behaviours (Phase 2): GET/POST routing, urlencoded query+body
params with %XX and '+' decoding, 100-continue handling, 404/405/411
(chunked rejected)/413 (body cap), per-connection close, receive timeout.

## Build

Needs devkitPro (devkitA64 + libnx + switch-tools):

```sh
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
make          # produces ra-offline.nsp
make clean    # when needed
```

## Install (SD card)

```
/atmosphere/contents/4300000000000042/exefs.nsp   <- build output ra-offline.nsp
/atmosphere/contents/4300000000000042/toolbox.json
```

- `exefs.nsp` is the built module. Atmosphere's boot2 launches it at boot.
- `toolbox.json` lets Tesla Toolbox / Uberhand see and restart the module.
- The title ID `4300000000000042` lives in the custom-sysmodule range
  (43xxxxxxxxxxxxxx); if it ever collides with another installed module,
  change all three `program_id`/range fields in `ra-offline.json` and the
  `tid` in `toolbox.json`.

## Runtime artifacts (on the Switch SD)

```
/switch/raoffline/sysmodule.log    Phase 1 debug log (uptime, heartbeats, heap)
/switch/raoffline/cache/           Phase 3: pre-cached game trigger JSONs
/switch/raoffline/queue.json       Phase 3: unlock payloads waiting for sync
```

## Memory budget (why the code looks the way it does)

Sysmodules run alongside Nintendo's own OS services, so the plan caps them at
~4-8 MB. Choices made in Phase 1:

- **Fixed static heap** (`HEAP_SIZE 0x400000` = 4 MB) handed to newlib in
  `__libnx_initheap` — the cost is known at build time, no runtime
  allocation games.
- **No heavy frameworks**: no libcurl, no mongoose, no big JSON libs yet.
  Phase 2/3 will use raw bsd sockets and a single-file JSON parser.
- **Paced loop**: the main thread sleeps 100 ms between ticks; it must never
  busy-spin on a core that games need.
- `mallinfo()` is logged at boot and in every heartbeat so heap creep is
  visible in the log from day one.

## References

- Build/npdm boilerplate follows the actively maintained
  [sys-botbase](https://github.com/olliz0r/sys-botbase) sysmodule (same
  toolchain generation, sockets in Phase 2 mirror its TCP server design).
- libnx docs: https://switchbrew.org/wiki/Libnx
