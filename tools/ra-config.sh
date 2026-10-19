#!/usr/bin/env bash
# ra-config.sh - point RetroArch's achievements at the ra-offline proxy.
#
# RetroArch (verified against master cheevos/cheevos.c) reads
#   cheevos_custom_host = "<scheme>://<host>[:<port>]"
# from retroarch.cfg and uses it as "<host>/dorequest.php"; empty means
# the real retroachievements.org.
#
# Usage:
#   ra-config.sh <path-to-retroarch.cfg>              # proxy on
#   ra-config.sh <path-to-retroarch.cfg> --url URL    # custom proxy URL
#   ra-config.sh <path-to-retroarch.cfg> --restore    # back to real server
#   ra-config.sh <path-to-retroarch.cfg> --show       # current setting
#
# On the Switch the file is  <sdcard>:/retroarch/retroarch.cfg
# (point this script at the mounted SD, or run it on-device via a
# retroarch-adjacent homebrew shell). RetroArch must be restarted after
# changing the file - it reads retroarch.cfg once at boot.
set -u

URL_DEFAULT="http://127.0.0.1:8080"
CFG=""
URL="$URL_DEFAULT"
MODE="set"

while [ $# -gt 0 ]; do
    case "$1" in
        --url)    URL="$2"; shift 2 ;;
        --restore) MODE="restore"; shift ;;
        --show)   MODE="show"; shift ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *)  CFG="$1"; shift ;;
    esac
done

if [ -z "$CFG" ]; then
    echo "error: no retroarch.cfg path given (see --help)" >&2
    exit 1
fi
if [ "$MODE" != "restore" ] && [ ! -f "$CFG" ]; then
    echo "error: $CFG not found" >&2
    exit 1
fi

case "$MODE" in
    show)
        cur=$(grep '^cheevos_custom_host' "$CFG" || true)
        if [ -z "$cur" ]; then
            echo "cheevos_custom_host: <unset - real retroachievements.org>"
        else
            echo "$cur"
        fi
        ;;
    restore)
        sed -i '/^cheevos_custom_host/d' "$CFG"
        echo "restored: achievements go to the real retroachievements.org"
        ;;
    set)
        cp "$CFG" "$CFG.ra-offline.bak"
        if grep -q '^cheevos_custom_host' "$CFG"; then
            sed -i "s|^cheevos_custom_host.*|cheevos_custom_host = \"$URL\"|" "$CFG"
        else
            printf '\n# ra-offline: achievements go through the local proxy\ncheevos_custom_host = "%s"\n' "$URL" >> "$CFG"
        fi
        echo "set: $(grep '^cheevos_custom_host' "$CFG")"
        echo "backup: $CFG.ra-offline.bak"
        echo "remember: restart RetroArch so it re-reads the config"
        ;;
esac
