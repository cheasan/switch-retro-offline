<p align="center">
  <img src="assets/logo.svg" width="180" alt="ra-offline logo"/>
</p>

# ra-offline — RetroAchievements offline proxy for the Nintendo Switch

[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

**ra-offline** is a background system module (sysmodule) for Nintendo Switch
[Atmosphere CFW](https://github.com/Atmosphere-NX/Atmosphere) that lets
RetroArch keep earning RetroAchievements **without an internet connection**.

It runs a tiny web server on the Switch itself that pretends to be
retroachievements.org. While you play offline, RetroArch doesn't notice any
difference — achievements you unlock are saved to the SD card. The moment
your Switch sees Wi-Fi again, ra-offline quietly uploads everything you
earned to the real retroachievements.org. Nothing is faked in your final
profile: every unlock is replayed to the real server.

## How it works

```
offline:  RetroArch ──> ra-offline (127.0.0.1:8080) ──> SD card queue
online:   ra-offline  ────────────────────────────────> retroachievements.org (TLS)
```

- RetroArch is pointed at the local proxy with a single config line
  (`cheevos_custom_host`).
- Game data (login, game identification, achievement definitions) is served
  from a cache you pre-download on your computer.
- Unlocks are appended to a queue file and acknowledged to RetroArch
  immediately, so gameplay is never blocked.
- When the Switch comes online, the queue is replayed to the real server
  over the console's own TLS. Failed uploads stay queued and retry later.

## Requirements

- Nintendo Switch running Atmosphere custom firmware
- RetroArch for Switch (standard build) with RetroAchievements enabled and
  logged in
- A [retroachievements.org](https://retroachievements.org) account
- A computer, once, to pre-cache the games you want to play offline

## Setup

### 1 · Pre-cache your games (on your computer, online)

```sh
git clone https://github.com/cheasan/switch-retro-offline
cd switch-retro-offline/tools

# cache one game and link it to your ROM's achievement hash:
python3 fetch-cache.py --user YOUR_RA_NAME --password YOUR_RA_PASSWORD \
    --game 1234 --hash <md5-hash>:1234
```

The hash RetroArch sends is **not** always the plain MD5 of the ROM file.
The reliable way to get it: launch the game in RetroArch once with
achievements enabled (online), then copy it from RetroArch's log — the line
looks like `cheevos hash: <md5>`.

This produces an `ra-offline` folder (account + game cache) — by default at
`sd/switch/raoffline/` inside the repo, ready to copy.

### 2 · Install the sysmodule

Copy to your SD card (merge with the existing `atmosphere` folder):

```
atmosphere/contents/4300000000000042/exefs.nsp     <- from this repo's Releases
atmosphere/contents/4300000000000042/toolbox.json
switch/raoffline/...                               <- from step 1
```

Reboot the console. Atmosphere starts the module at boot; it logs to
`/switch/raoffline/sysmodule.log` if you want to confirm it's alive.

### 3 · Point RetroArch at the proxy

On the SD card, edit `retroarch/retroarch.cfg` (or use the helper script
against a mounted SD):

```sh
bash tools/ra-config.sh /path/to/sd/retroarch/retroarch.cfg
# adds: cheevos_custom_host = "http://127.0.0.1:8080"
```

Restart RetroArch (it reads the config once at boot). To go back to the
real server any time: `ra-config.sh <cfg> --restore`.

### 4 · Play offline

Just play. Unlocks are queued to `/switch/raoffline/queue.json` instantly
and RetroArch shows them as earned. Games that aren't in your cache simply
show "no achievements" — add them later with `fetch-cache.py`. Copy the new
cache files to the SD card and play: no reboot needed.

### 5 · Come back online

Within seconds of the Switch connecting to Wi-Fi, queued unlocks are
uploaded and the queue file empties. Check your profile on
retroachievements.org. Uploads retry automatically, so a flaky connection
never loses anything. Leaderboard times sync the same way.

## Troubleshooting

Read `/switch/raoffline/sysmodule.log` first. Each failed upload names the
step that failed, or quotes the server's error text.

- **"Password is required" or "Missing RetroAchievements account
  information":** RetroArch has no saved password. Recent RetroArch builds
  encrypt it in `retroarch/retroarch-keychain.cfg` with the key in
  `retroarch/retroarch-keychain.key`. Do not delete the key file: RetroArch
  then cannot read the saved password again. Set the proxy first, then log
  in again from RetroArch's Achievements settings.
- **Black screen when a game starts:** the RetroArch menu and its cores
  come from different releases. Keep the menu (`switch/retroarch_switch.nro`)
  and the cores in `retroarch/cores/` from the same download.
- **"Login failed" with nothing in `sysmodule.log`:** RetroArch did not
  reach the proxy. Check that `cheevos_custom_host` is
  `"http://127.0.0.1:8080"` in the `retroarch.cfg` that RetroArch loads.

## Building from source

Install [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the
`switch-dev` package group (devkitA64 + libnx), then:

```sh
make                    # builds ra-offline.nsp
bash test/run-tests.sh  # 37-check desktop test suite (no Switch needed)
```

The HTTP server, protocol mocking, caching and sync logic all run and get
tested on a normal Linux machine — only the thin service glue needs real
hardware.

## Credits

- **Design & direction:** [cheasan](https://github.com/cheasan) (project
  concept, development plan, hardware testing).
- **Implementation:** built end-to-end by an AI coding agent —
  **GLM (Z.ai)** driving [OpenCode](https://opencode.ai) — from the plan
  document through all five phases, including the desktop test suite.

## Disclaimer

This is an independent hobbyist tool. It is not affiliated with, endorsed
by, or connected to the RetroAchievements.org team, the RetroArch team,
Nintendo, or Team Neptune (Atmosphere). Use of a custom firmware sysmodule
is at your own risk; be considerate and honest about how you unlock
achievements — the queue syncs everything you earn, nothing more.

## License

[MIT](LICENSE)
