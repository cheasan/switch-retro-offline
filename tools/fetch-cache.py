#!/usr/bin/env python3
"""
fetch-cache.py - pre-cache RetroAchievements data for ra-offline.

Run this on the LAPTOP while ONLINE. It logs into retroachievements.org,
downloads the achievement patch data for the games you specify, and lays
out the SD card directory that the ra-offline sysmodule reads offline:

    <base>/account.json               offline identity + score
    <base>/cache/hashes.txt           ROM-hash -> game id mapping
    <base>/cache/<gameid>/patch.json  verbatim dorequest patch response

After running it, copy <base> to the SD card as /switch/raoffline.
(With --base sd/switch/raoffline the repo's sd/ staging dir is ready as-is.)

IMPORTANT about hashes: the hash RetroArch sends is rcheevos' per-console
hash, which is NOT always the plain MD5 of the ROM file. The reliable way
to get it: launch the game in RetroArch with achievements enabled while
online, then read the hash from the RA log line "cheevos hash: <md5>"
(or --rom computes plain MD5, correct for many systems but not all).

Examples:
    python3 tools/fetch-cache.py --user SAM --password secret --game 1234 --hash deadbeef...:1234
    python3 tools/fetch-cache.py --user SAM --token TOK --rom ~/Games/smw.sfc:1234
"""
import argparse
import hashlib
import json
import os
import sys
import urllib.parse
import urllib.request

DOREQ = "https://retroachievements.org/dorequest.php"


def doreq(params):
    data = urllib.parse.urlencode(params).encode()
    req = urllib.request.Request(DOREQ, data=data, headers={
        "User-Agent": "ra-offline-fetch/0.1"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode("utf-8", "replace"))


def file_md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def load_hashes(path):
    out = {}
    if os.path.exists(path):
        for line in open(path):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 2:
                out[parts[0].lower()] = int(parts[1])
    return out


def save_hashes(path, hashes):
    lines = ["# ra-offline ROM hash cache: <cheevos-md5> <gameid>"]
    lines += [f"{md5} {gid}" for md5, gid in sorted(hashes.items())]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--user", required=True, help="RA username")
    auth = ap.add_mutually_exclusive_group(required=True)
    auth.add_argument("--password", help="RA password (exchanged once for a token)")
    auth.add_argument("--token", help="existing RA web token (from account settings)")
    ap.add_argument("--game", action="append", default=[], type=int,
                    help="game id to cache (repeatable)")
    ap.add_argument("--hash", action="append", default=[],
                    help="ROM hash mapping MD5:GAMEID (repeatable; preferred)")
    ap.add_argument("--rom", action="append", default=[],
                    help="FILE:GAMEID - cache plain-MD5 of a rom file (not always rcheevos' hash)")
    ap.add_argument("--base", default="sd/switch/raoffline",
                    help="output dir (default: sd/switch/raoffline)")
    args = ap.parse_args()

    cache_dir = os.path.join(args.base, "cache")
    os.makedirs(cache_dir, exist_ok=True)

    # 1. login
    print(f"logging in as {args.user} ...")
    if args.token:
        login = doreq({"r": "login2", "u": args.user, "t": args.token})
    else:
        login = doreq({"r": "login2", "u": args.user, "p": args.password})
    if not login.get("Success"):
        sys.exit(f"login failed: {login.get('Error', login)}")
    token = login.get("Token")
    print(f"  ok (token {token[:4]}..., score {login.get('Score')})")

    with open(os.path.join(args.base, "account.json"), "w") as f:
        json.dump({"User": login.get("User", args.user),
                   "Token": token,
                   "Score": login.get("Score", 0),
                   "SoftcoreScore": login.get("SoftcoreScore", 0)}, f, indent=1)

    # 2. patches
    for gid in args.game:
        print(f"fetching patch for game {gid} ...")
        patch = doreq({"r": "patch", "u": args.user, "t": token, "g": gid})
        if not patch.get("Success"):
            print(f"  WARNING: patch for {gid} failed: {patch.get('Error')}")
            continue
        n = len(patch.get("PatchData", {}).get("Achievements", []))
        gdir = os.path.join(cache_dir, str(gid))
        os.makedirs(gdir, exist_ok=True)
        with open(os.path.join(gdir, "patch.json"), "w") as f:
            json.dump(patch, f)
        print(f"  ok: {n} achievements -> {gdir}/patch.json")

    # 3. hash mappings
    hashes = load_hashes(os.path.join(cache_dir, "hashes.txt"))
    for spec in args.hash:
        try:
            md5, gid = spec.split(":", 1)
            md5 = md5.strip().lower()
            int(gid)
        except ValueError:
            sys.exit(f"bad --hash {spec!r}, expected MD5:GAMEID")
        hashes[md5] = int(gid)
        print(f"hash {md5} -> {gid}")
    for spec in args.rom:
        try:
            path, gid = spec.rsplit(":", 1)
            int(gid)
        except ValueError:
            sys.exit(f"bad --rom {spec!r}, expected FILE:GAMEID")
        md5 = file_md5(path)
        print(f"rom {os.path.basename(path)} md5 {md5} -> {gid}"
              f"  (plain md5; NOT always rcheevos' hash - prefer --hash)")
        hashes[md5] = int(gid)
    save_hashes(os.path.join(cache_dir, "hashes.txt"), hashes)

    print(f"\ndone. copy '{args.base}' to the SD card as /switch/raoffline")


if __name__ == "__main__":
    main()
