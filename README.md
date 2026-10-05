# webdoom

Fork of [abootnet/uzdoom-wasm](https://github.com/abootnet/uzdoom-wasm) (WebAssembly port of [UZDoom](https://github.com/UZDoom/UZDoom)) with tweaks to optimize for Brutal Doom v22b6, plus a persistent boot config for auto-launch behavior.

Running at [doom.xode.cc](https://doom.xode.cc).

The upstream fork is the general-purpose project: picker UI, iframe embedding, many IWADs. This one is narrower — a container you point at a directory of WADs so it boots one game. See the [upstream README](https://github.com/abootnet/uzdoom-wasm#readme) for the features not covered here.

## Deploy

WADs stay out of the image, so mount a directory containing them plus a `boot.json`:

```bash
mkdir -p /srv/doom
cp doom2.wad brutal22test6.pk3 /srv/doom/
cat > /srv/doom/boot.json <<'EOF'
{ "iwad": "doom2.wad", "file": "brutal22test6.pk3" }
EOF

docker run -d --restart unless-stopped -p 8080:80 \
  -v /srv/doom:/srv/private:ro \
  ghcr.io/chriscorbell/webdoom:latest
```

Visiting the address now loads Doom 2 with Brutal Doom, no picker. The first visit downloads the WADs from `/private/` with a progress bar; later visits read them from the browser's IndexedDB.

Without a `boot.json`, you get the upstream picker UI instead — upload an IWAD, drop in mods, press Launch.

### Behind a reverse proxy

Two requirements, both of which break the game completely if missed:

- **Pass the COOP/COEP headers through untouched.** The bundled Caddy sets `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`. Without both arriving intact, `crossOriginIsolated` is false, `SharedArrayBuffer` does not exist, and the engine's threads never start.
- **Serve it over HTTPS.** `SharedArrayBuffer` requires a secure context. A plain-HTTP LAN address or hostname is not one, so the engine fails to start there. Plain HTTP works only on `localhost`.

## Configuration

`boot.json` sets the default launch. The same settings work as URL parameters, and an explicit `?iwad=` in the URL overrides `boot.json` entirely, so shared links keep working on a kiosk deployment.

| Key / param  | Example                     | Meaning                                            |
|--------------|-----------------------------|----------------------------------------------------|
| `iwad`       | `doom2.wad`                 | The IWAD. Required for auto-launch.                |
| `file`       | `brutal22test6.pk3`         | Mods, as a string, comma-separated list, or array. Max 10. |
| `warp`       | `1` / `1,1`                 | Jump straight to a map, skipping the menu.         |
| `skill`      | `4`                         | Difficulty, 1–5.                                    |
| `map`        | `E2M4`                      | Named map, as an alternative to `warp`.            |
| `nomonsters` | `1`                         | No monsters.                                        |
| `fast`       | `1`                         | Fast monsters.                                      |
| `respawn`    | `1`                         | Respawning monsters.                                |
| `cheat`      | `god,idfa`                  | Cheats applied at launch. Allowlisted, argless only. |
| `nomelt`     | `1`                         | Skip the screen-melt transition into the game.     |

So `{ "iwad": "doom2.wad", "file": "brutal22test6.pk3", "warp": 1, "skill": 4 }` drops straight into MAP01 on Ultra-Violence rather than stopping at the menu.

URL strings are never pasted into the engine's argv. Every value is checked against a strict pattern and dropped if it does not match; filenames allow no paths or spaces. Adding a parameter means editing the pattern table in `web/uzdoom-loader.js` — nothing reads arbitrary query strings.

### Side-loaded files

Any file named by `iwad` or `file` that is not already in IndexedDB gets fetched from `/private/<name>`, then saved so later visits skip the download. Each later visit sends a `HEAD` request and downloads the file again only if the server's copy changed, so replacing a mod under the same name reaches returning players. There is no registration step: a name the server does not have returns 404 and falls back to the normal missing-file handling. Anything you drop in the mounted directory is loadable by name.

The Caddyfile serves `/srv/private/` with `X-Robots-Tag: noindex, nofollow`, and makes browsers revalidate it rather than cache it.

### Caching

The Docker build stamps a hash of the engine files onto every asset URL (`uzdoom.wasm?v=…`). Versioned URLs are cached for a year; `index.html` and anything unversioned is revalidated on each visit. A deploy therefore reaches players on their next visit, and a browser can never pair a new `uzdoom.js` with an old `uzdoom.wasm`.

## Building from source

Needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html), CMake 3.20+, Ninja, and Python 3. `build-wasm.sh` finds `emsdk` via `$EMSDK`, `./emsdk/`, or `../emsdk/`, and builds the native `lemon` and `re2c` helpers itself on Linux and macOS.

```bash
git clone https://github.com/chriscorbell/webdoom.git
cd webdoom
git clone https://github.com/emscripten-core/emsdk.git
(cd emsdk && ./emsdk install latest && ./emsdk activate latest)

./build-wasm.sh                       # Release; BUILD_TYPE=Debug for a debug build
python3 web/serve.py                  # serves build-wasm/ with the COOP/COEP headers
# open http://localhost:8080/uzdoom.html
```

`docker build -t webdoom .` produces the deployable image the same way CI does.

## What this fork changes

Relative to [abootnet/uzdoom-wasm](https://github.com/abootnet/uzdoom-wasm):

- **Brutal Doom damage overlays render correctly.** They used to cover the screen in near-opaque red, so they shipped disabled. The cause was the HUD message cap being applied per font: Brutal Doom draws damage with about 20 different graphics over the same pixels, so a cap of 4 each still allowed roughly 80 stacked translucent layers. Capping all of them together fixed it, and they are enabled again.
- **`boot.json`**, so a deployment auto-launches a chosen game with no URL parameters.
- **Side-loading covers mods**, not just IWADs, and needs no lookup table.
- **Builds on Linux and macOS.** Upstream's helper tools only shipped as Windows binaries, so `./build-wasm.sh` previously worked only inside Docker.

## Credits & license

- **License:** GPLv3, inherited from UZDoom. See [`LICENSE`](LICENSE).
- **Attribution:** [`CREDITS.md`](CREDITS.md) has the full chain from id Software through ZDoom, GZDoom, UZDoom, and the WASM port.
- **Upstream UZDoom README:** kept at [`docs/README-upstream.md`](docs/README-upstream.md).

WADs are not included and not distributed here. Bring your own.
