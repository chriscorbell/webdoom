# syntax=docker/dockerfile:1.6
#
# uzdoom-wasm — multi-stage Docker build.
#
#   Stage 1 (builder):  emscripten/emsdk + cmake/ninja → build-wasm/ artifacts
#   Stage 2 (runtime):  caddy:alpine serving /srv with COOP/COEP headers
#
# Build:   docker build -t uzdoom-wasm .
# Run:     docker run --rm -p 8080:80 uzdoom-wasm
# Browse:  http://localhost:8080

# --- Build stage -----------------------------------------------------------
FROM emscripten/emsdk:3.1.70 AS builder

RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake ninja-build zip unzip curl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

# Normalize line endings and ensure the build script is executable. Files
# coming from a Windows host can lose the +x bit through COPY, and editors
# may reintroduce CRLF on save — strip \r and chmod explicitly.
RUN sed -i 's/\r$//' build-wasm.sh \
 && chmod +x build-wasm.sh

# --- Freedoom IWADs --------------------------------------------------------
# The build copies web/assets/freedoom{1,2}.wad next to the page as
# freedoom/*.wad, which the picker's "Use bundled Freedoom" (and
# ?iwad=freedoom1.wad) downloads on demand. Local dev may already have them
# in web/assets/ (gitignored); CI runners get a clean checkout without
# them, so fetch if missing. Pinned to a specific release for
# reproducibility.
ARG FREEDOOM_VERSION=0.13.0
RUN if [ ! -f /src/web/assets/freedoom1.wad ] || [ ! -f /src/web/assets/freedoom2.wad ]; then \
        echo "Fetching Freedoom ${FREEDOOM_VERSION}..." \
     && mkdir -p /src/web/assets \
     && curl -fsSL -o /tmp/freedoom.zip \
          "https://github.com/freedoom/freedoom/releases/download/v${FREEDOOM_VERSION}/freedoom-${FREEDOOM_VERSION}.zip" \
     && unzip -j -o /tmp/freedoom.zip \
          "freedoom-${FREEDOOM_VERSION}/freedoom1.wad" \
          "freedoom-${FREEDOOM_VERSION}/freedoom2.wad" \
          -d /src/web/assets \
     && rm /tmp/freedoom.zip ; \
    else \
        echo "Freedoom WADs already present in web/assets/ — skipping download" ; \
    fi \
 && ls -lh /src/web/assets/freedoom1.wad /src/web/assets/freedoom2.wad

# --- Build ------------------------------------------------------------------
# UZDoom invokes `lemon` (parser generator) and `re2c` (lexer generator) at
# build time to turn .lemon / .re source into C/C++. They must run on the
# BUILD host (Linux, inside this container) — not the TARGET (WASM), and the
# .exe binaries in tools-native/ are Windows-only. build-wasm.sh detects a
# non-Windows host and builds Linux ELF copies itself, so nothing extra is
# needed here.
#
# The emscripten/emsdk base image sets EMSDK=/emsdk, puts emcc on PATH,
# and pre-populates EMSDK_NODE / EMSDK_PYTHON. Our build-wasm.sh picks
# those up via its OS-detection path.
# GIT_DESCRIBE names the build in the engine's version string; .git isn't in
# the build context, so CI passes it in (see docker-publish.yml).
ARG GIT_DESCRIBE
RUN if [ -z "${GIT_DESCRIBE}" ]; then unset GIT_DESCRIBE; fi \
 && BUILD_TYPE=Release EMSDK=/emsdk ./build-wasm.sh

# --- Stage runtime artifacts -----------------------------------------------
# Collect everything the runtime actually serves into /stage so the next
# stage can COPY a single clean directory and not drag in CMake build
# trash (CMakeFiles/, build.ninja, compile_commands.json, etc.).
#
# UZDoom produces FIVE PK3s (not just uzdoom.pk3) — game_support.pk3
# carries iwadinfo.txt, which is what teaches the engine which files
# count as valid IWADs. Miss that and user-uploaded WADs get rejected
# with "Cannot find a game IWAD". brightmaps/lights/widescreen_gfx are
# rendering data. soundfonts/ is the MIDI synth's bank (the web build's
# music stub has no OPL/OPN synth, so fm_banks/ isn't shipped).
RUN mkdir -p /stage/soundfonts /stage/freedoom \
 && cp /src/build-wasm/uzdoom.html              /stage/index.html \
 && cp /src/build-wasm/uzdoom.js                /stage/uzdoom.js \
 && cp /src/build-wasm/uzdoom.wasm              /stage/uzdoom.wasm \
 && cp /src/build-wasm/freedoom/*.wad           /stage/freedoom/ \
 && cp /src/build-wasm/uzdoom-loader.js         /stage/uzdoom-loader.js \
 && cp /src/build-wasm/uzdoom-melt.js           /stage/uzdoom-melt.js \
 && cp /src/build-wasm/uzdoom.pk3               /stage/ \
 && cp /src/build-wasm/game_support.pk3         /stage/ \
 && cp /src/build-wasm/brightmaps.pk3           /stage/ \
 && cp /src/build-wasm/lights.pk3               /stage/ \
 && cp /src/build-wasm/game_widescreen_gfx.pk3  /stage/ \
 && cp /src/build-wasm/soundfonts/uzdoom.sf2    /stage/soundfonts/ \
 && echo "=== /stage contents ===" \
 && ls -laR /stage

# --- Runtime stage ---------------------------------------------------------
FROM caddy:2-alpine

# uzdoom.html got renamed to index.html in the staging step so "/" serves
# the game directly. All internal script refs are relative, so the rename
# is safe.
COPY --from=builder /stage/ /srv/
COPY Caddyfile /etc/caddy/Caddyfile

EXPOSE 80
# caddy:alpine already has CMD ["caddy", "run", "--config", "/etc/caddy/Caddyfile", "--adapter", "caddyfile"]
