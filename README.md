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
| 1 | Sysmodule boilerplate, heap, SD logging, paced loop | **code done — needs first compile + Switch test** |
| 2 | HTTP loopback server on 127.0.0.1:8080 (bsd sockets + thread) | pending |
| 3 | RetroArch API mocking, cache dir, unlock queue file | pending |
| 4 | nifm network detection + background sync to retroachievements.org | pending |
| 5 | Helper script to point RetroArch's cheevos URL at 127.0.0.1:8080 | pending |

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
