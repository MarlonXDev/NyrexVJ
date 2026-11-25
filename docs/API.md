# Web interface and REST API

NyrexVJ can be controlled from a web page in a browser, on the same computer or, if you allow it, from a phone or a tablet in the same network. The page talks to a small HTTP server inside the program, and the same server is a documented REST API for scripts. **It is off by default.** Nothing listens until you turn it on.

This page describes version 1 of the API, which came with v0.36.0. The decisions behind it are in [ADR 0029](adr/0029-rest-api.md).

## Turning it on

1. Open Settings, Control, Web interface.
2. Tick *Turn on the web interface*. The page shows `Listening on port 9020` (the default) and, below it, the token.
3. Click *Open web interface*. The address it opens has the form `http://127.0.0.1:9020/#token=...`. The token sits behind the `#`, so the browser never sends it to the server and the page removes it from the address bar at once.

To use it from another device, also tick *Allow other devices on the network* and open `http://<IP address of this computer>:<port>/` there. Then paste the token into the field on the page (*Copy token* on the Settings page copies it, *Show* reveals it). **Anyone on the network who has the token can then control NyrexVJ, and the connection is not encrypted.** Only do this in a network you trust. NyrexVJ does not open a firewall port, does not forward a port and does not announce itself (no mDNS, no UPnP); the operating system may ask you to allow the connection.

The port can be 1024 to 65535. If it is taken, the Settings page shows the error and *Try again*. *New token* replaces the token at once; every browser and script that used the old one is locked out.

## What the page can do

Status line (composition, tempo, frame rate, output size), master level, blackout, tap tempo, the 16 decks (the used ones can be activated, with previous and next), the 8 column triggers, the 4 layers with their 8 clip cells (a click starts the clip), opacity, bypass, solo and clear for each layer, the factory looks and your own looks (load), and the outputs (read only). The page follows the language of the browser: English, German, Spanish, French, Italian, Portuguese, Dutch and Japanese. It works on a phone (large buttons, no sideways scrolling) and polls about twice a second, slower when the tab is in the background.

## The API

Everything is under `/api/v1/`. Every address needs the header `Authorization: Bearer <token>`. Requests with a body need `Content-Type: application/json`. Replies are JSON. An error looks like `{"error":{"code":"cell_empty","message":"This cell has no clip"}}`.

| Method and address | What it does |
| --- | --- |
| `GET /api/v1` | Name, API version and the list of endpoints |
| `GET /api/v1/status` | Program and API version, composition name, tempo, beats per bar, active deck, blackout, master level, frame rate, output size, audio active |
| `GET /api/v1/controls[?prefix=layer/1]` | All control ids with kind and value (0 to 1) |
| `GET /api/v1/controls/{id}` | One control, for example `master/level` or `layer/2/fx/1/param/3` |
| `PUT` or `POST /api/v1/controls/{id}` | Set a control: `{"value": 0.5}` or `{"value": true}`. A trigger needs no body. The value must be 0 to 1 |
| `GET /api/v1/layers`, `GET /api/v1/layers/{1..4}` | Opacity, bypass, solo, speed, scale, running column, the 8 clips and the 4 effect slots |
| `POST /api/v1/layers/{n}/clips/{1..8}/trigger` | Start the clip in that cell (409 `cell_empty` if there is none) |
| `POST /api/v1/layers/{n}/clear` | Stop the layer |
| `POST /api/v1/columns/{1..8}/trigger` | Trigger a column |
| `GET /api/v1/decks` | The 16 decks with name, `used` and `active` |
| `POST /api/v1/decks/{1..16}/activate`, `POST /api/v1/decks/next`, `.../previous` | Switch the deck (409 `deck_not_used` for an empty deck) |
| `GET /api/v1/clips`, `GET /api/v1/effects` | The clip and effect library |
| `GET /api/v1/presets` | `factory` and `user` preset names |
| `POST /api/v1/presets/load` | `{"source":"factory"\|"user","name":"..."}` |
| `GET /api/v1/outputs` | The four outputs: name, enabled, open, fullscreen, monitor, NDI |

The control ids are the ones of OSC and MIDI mapping (Settings, Control lists them), and a write takes the same way as an OSC message or a cue: it goes through the control system on the main thread. There is no second control logic. `HEAD`, `OPTIONS`, `DELETE` and other methods answer 405.

```
TOKEN=$(cat ~/.config/NyrexVJ/api_token)
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:9020/api/v1/status
curl -X PUT -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
     -d '{"value":0.4}' http://127.0.0.1:9020/api/v1/controls/master/level
curl -X POST -H "Authorization: Bearer $TOKEN" http://127.0.0.1:9020/api/v1/layers/1/clips/3/trigger
```

Status codes: 200 done, 400 a request that cannot be read or a value out of range, 401 token missing or wrong, 403 the Host or Origin is not allowed, 404 unknown address, control or preset, 405 method not allowed, 408 the client was too slow, 409 the cell or deck is empty, 413 body over 64 KiB, 414 address too long, 415 not JSON, 417 and 501 for features the server does not support (`Expect`, chunked bodies), 422 the preset file is damaged, 429 too many requests or locked out (`Retry-After` says how long), 431 header too large or too many headers, 503 the program did not answer within a second (`busy`) or has no state yet, 505 not HTTP/1.x.

### What the API does not do

By design (see the ADR) these are not in version 1 and wait for a maintainer decision: saving or deleting presets, changing the structure of a composition (assigning clips to cells, effects to slots), recording, uploading files, events or WebSocket, several users with different rights, encryption (TLS), and any form of discovery. Show sync over several computers (Prompt 20) and shared editing (Prompt 33) are separate topics and not part of this interface.

## Security model

The threat is a web page in your browser, or another device on the network, that wants to press buttons in your show.

- **Off by default, local by default.** The server listens on `127.0.0.1` only. `Allow other devices on the network` makes it listen on all addresses and shows a warning. On Windows the port is opened exclusively, so another program cannot share it.
- **Token.** 256 bits from the operating system (`BCryptGenRandom`, `/dev/urandom`; if that fails the interface stays off with a message), sent as a bearer token, compared in constant time. It lives in the file `api_token` in the data folder, not in `settings.json` (people attach that file to bug reports), is written with mode 0600 on Linux through a temporary file and an atomic rename, and is never logged. On Windows the file inherits the rights of your profile folder; nothing more is set. The page itself (HTML, script, style, language file) is served without the token and contains no secret.
- **Browsers.** The `Host` header must be `localhost`, `127.x.x.x` or `[::1]` with the right port (an IPv4 address of another device only when other devices are allowed), which defeats DNS rebinding. `Origin`, if present, must be exactly this server, and `Sec-Fetch-Site` must be `same-origin` or `none`. There are no CORS headers at all, so a page from another site cannot read an answer and a preflight request fails. Sending JSON needs `Content-Type: application/json`, which a plain HTML form cannot do. The page is served with a strict Content-Security-Policy (`default-src 'none'`; script, style, connect and images only from itself, no inline script, no framing), `no-store`, `nosniff`, `X-Frame-Options: DENY` and `no-referrer`.
- **Limits.** 40 requests a second per address (a burst of 80), five wrong tokens in a minute lock the address out for a minute, at most 32 connections and 8 per address, 256 remembered addresses, a header over 8 KiB or 48 headers, a body over 64 KiB, an address over 2048 bytes, JSON nested deeper than 12 levels, a client that sends the header slower than 5 seconds or the request slower than 10 seconds are all refused. The parser is strict: no chunked bodies, no `Transfer-Encoding`, no duplicate `Host` or `Content-Length`, no bare line feeds or NUL bytes, no `.`, `..`, encoded slashes or NUL in the path. Every connection is closed after one answer.
- **Not covered.** There is no encryption; on a shared network someone can read the token. Whoever has the token can do everything the API does, including loading a look in the middle of a show. A program running as you can read the token file. See the list of what was not tested at the end.

## How it stays out of the way of the show

The server runs on one extra thread at background priority and does no work per client thread: a poll based state machine handles all connections. **It never touches the engine, the composition or the control system.** It reads an immutable snapshot that the main thread builds every quarter second and after every command (about 9 µs, measured), and it hands writes to a queue of at most 64 commands. The main thread drains at most 16 commands per frame before it updates the engine and runs them through the same code as OSC. The server waits for the answer for one second at most; after that the request is cancelled (a command that timed out is never run later) and the client gets 503 `busy`. A stuck main thread therefore cannot block the server, and the server cannot block the main thread. File access for presets is on the server thread (the text of a user preset is read there), and parsing happens on the main thread because it reads the global libraries. No network or file operation runs on the render thread. With the interface off, the cost per frame is one boolean check.

## Measurements

On the build server (Linux, 2 cores, 8 GB, Mesa llvmpipe software OpenGL, no graphics card), 960 x 540, 240 frames per phase paced at 60 frames a second, `NyrexVJ --bench-api 240`. The frame time is render plus `glFinish` on a software renderer that uses both cores, so the absolute numbers say nothing about a graphics card. What counts is the difference between the phases.

| Phase | Frame average | p50 | p95 | p99 | API update per frame |
| --- | --- | --- | --- | --- | --- |
| Off (baseline) | 43.9 ms | 42.1 | 53.5 | 71.0 | 0.8 µs |
| On, idle | 42.5 ms | 41.1 | 49.0 | 58.6 | 5.5 µs |
| On, 3 clients polling and writing (55 requests a second) | 43.4 ms | 41.9 | 51.2 | 58.1 | 23.7 µs |
| On, flood: 6 client threads, 36,454 requests a second | 82.6 ms | 78.5 | 112.0 | 127.0 | 31.4 µs |
| Off again | 41.6 ms | 40.3 | 48.0 | 52.7 | 0.7 µs |

With the interface on and used normally, the frame time did not change beyond the spread between two runs of the baseline (41.6 to 43.9 ms). The longest API update on the main thread in any phase was 0.098 ms. **The flood doubled the frame time. That number mixes two things:** the load generator is six busy threads inside the same process on the same two cores that the software renderer also uses, and at that rate nearly every request must have been refused by the rate limiter (the limit is 40 a second), but the benchmark does not count the status codes. The measurement shows that a flood from the same computer is not free on a two core machine; it does not show what a flood costs on a computer with a graphics card and more cores, and it was not repeated with a load generator in a separate process. Memory and video memory were not measured (the server allocates no GPU resources; the snapshot, the connection table and the pool of at most 64 commands are small and bounded).

## Tests

- `nvj_api_tests` (21,016 checks, part of `ctest`): the HTTP parser, including random input and the smuggling and slow client cases; the security functions; the rate limiter; the router on every endpoint; the hub with threads; and the real server on a loopback socket, including a stalled main thread, a slow client, many connections, restart and stop; the page (no inline script, no forbidden calls, all text keys exist in all 8 languages), the token file and the settings.
- `NyrexVJ --api-selftest` (59 checks, in the Linux CI): the interface through the real engine. It is off by default, turns on, saves the token with private rights, reads and writes controls and sees the effect in the engine (master level, opacity, blackout, clip, clear), loads factory and user presets, refuses a broken one without a change, stops answering with 503 while the main thread stalls, rate limits, locks out, replaces the token and closes the port.
- `NyrexVJ --bench-api [FRAMES]`: the measurement above (`--bench-out` writes JSON).
- `scripts/test_web_ui.mjs`: the page in a real Chrome (46 checks: connecting, no violation of the content security policy, the token leaving the address bar, clicks reaching the program, all eight languages, a phone width, the token form, a cross-origin request being blocked). It needs Node, Chrome and `npm install puppeteer-core` in the folder you start it from, and a running NyrexVJ with the interface on; it changes the state of that program. It is not part of the CI.
- Run once for this release: `nvj_api_tests` under AddressSanitizer and UndefinedBehaviorSanitizer and under ThreadSanitizer without a finding; the whole program with `--api-selftest` under ThreadSanitizer: 59 checks passed, and the four warnings all lie inside Mesa's software renderer (`libgallium`, at engine start and window close), none in the code of the interface.

## What was not tested

**On Windows only the unit tests ran**, in the CI: `nvj_api_tests` passed there (30 of 30 tests), including the real server on loopback sockets with the Winsock code (`tcp_socket_win.cpp`), token generation with `BCryptGenRandom` and the token file. Not run on Windows: `--api-selftest`, the page in a browser, the exclusive port (`SO_EXCLUSIVEADDRUSE`) against a second program, and the rights of the token file. Nothing ran on a real graphics card, next to VRChat or on a show computer, and no phone or tablet was used: the phone layout was checked at 390 pixels wide in desktop Chrome. `Allow other devices` was tested for what is bound (`127.0.0.1` when off) but never with a second computer on a network, and no firewall was involved. Touch behaviour of the sliders is untested. The Chrome check used Chrome only; Firefox and Safari were not tried. Slow or lossy networks were not simulated. The flood measurement is explained above. The token file on Windows has the rights of the profile folder and they were not checked there. Long runs (hours) with a client polling were not done.
