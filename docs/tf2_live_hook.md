# TF2 live GC hook (first pass, UNVERIFIED against a real TF2 client)

This documents the `tf2_gc` target: a second, separate GC dylib that wires
milestone 1's TF2 item-schema/inventory parser (`tf2_gc/`) into the same
hook/threading/job-id plumbing `csgo_gc` uses for CS:GO/CS2. It has never
been run against an actual TF2 process — see "Known gaps" below before
relying on it.

## What it does

- Reuses `steam_hook.cpp`, `gc_shared.cpp`, `gc_message.cpp`, `game_profile.cpp`,
  `config.cpp`, `appid.cpp`, `keyvalue.cpp`, `main.cpp`, `platform_*.cpp`
  from `csgo_gc/` **unmodified** (as source references from
  `tf2_gc_hook/CMakeLists.txt`). This is the generic GCSDK layer: job/message
  framing, the `CMsgClientHello`/`CMsgClientWelcome`/`CMsgSOCacheSubscribed`
  handshake, and `funchook` interface hooking. That wire protocol and its
  message IDs (`k_EMsgGCClientHello` = 4006 etc.) are identical across every
  Valve GC game, not CS:GO-specific, so no changes were needed there beyond
  the indirection described below.
- Provides TF2-specific business logic in `tf2_gc_hook/gc_client_tf2.{h,cpp}`
  (`ClientGCTF2`): on `CMsgClientHello`, replies with a `CMsgClientWelcome`
  whose SO cache (`CMsgSOCacheSubscribed`, SO type 1 = economy items — also
  a Valve-wide constant, see `gc_const_tf2.h`) is built from
  `tf2_gc/tf2_items_game.txt` + `tf2_gc/tf2_inventory.txt` via the milestone-1
  parser (`tf2_gc/item_schema.cpp`, `tf2_gc/inventory.cpp`, reused directly).
- Unusual particle effects are encoded as attribute def_index 134
  ("attach particle effect", publicly documented on the TF2 Wiki) with the
  particle system id stored as a float bit-pattern in `CSOEconItemAttribute.value`
  — same convention CS:GO uses for its own attributes.

## What it deliberately does NOT do

- No crafting, trading, case opening/unlocking, store, or MvM/Halloween
  messages. Only the client-hello -> backpack-SO-cache path is implemented.
- No in-match cosmetic visibility to other players or game servers.
  `NetworkingClientTF2`/`NetworkingServerTF2` and `ServerGCTF2`
  (`tf2_gc_hook/networking_*_tf2.h`, `gc_server_tf2.h`) are all no-op stand-ins
  that exist only so `steam_hook.cpp`'s generic `GCWrapper<...>` types resolve;
  a listen/dedicated server hook will attach but do nothing.
- Windows launcher support was added after checking a real TF2 install:
  it ships game-named executables (`tf.exe`/`tf_win64.exe`, next to
  `bin/`, `tf/`, `platform/`), same convention as `csgo.exe`, using the
  same generic Source-1 launcher path `launcher_win.cpp` already had for
  CS:GO -- not the CS2-specific `CS2_LAUNCHER` branch (that branch's
  Wine-specific tier0/steam_api64 preload hack is CS2/Source2-only and
  doesn't apply here). `bin/x64/launcher.dll` exists in a real TF2 install,
  confirming the default `GC_GAME_BIN_DIR` (`x64`) needs no CS2-style
  override.

## How CS:GO/CS2 stayed untouched

`steam_hook.cpp` originally hardcoded the concrete types `ClientGC`/`ServerGC`/
`NetworkingClient`/`NetworkingServer`. Since it's the same physical file
reused for both targets, those 4 type names were replaced with
`ActiveClientGC`/`ActiveServerGC`/`ActiveNetworkingClient`/`ActiveNetworkingServer`,
resolved by two new headers:

- `csgo_gc/gc_active.h` (replaces the `gc_client.h`/`gc_server.h` includes)
- `csgo_gc/networking_active.h` (replaces the `networking_client.h`/
  `networking_server.h` includes, which have to come after the Steam headers)

Both `#if defined(TF2_GC_BUILD)` to the TF2 stand-ins, `#else` to the
original CS:GO/CS2 classes. The `csgo_gc` target never defines
`TF2_GC_BUILD`, so it expands to exactly the original includes/types —
verified by building both targets and diffing warnings (identical).

`config.cpp` and `appid.cpp` hardcoded `"csgo_gc/config.txt"` and
`"csgo/steam.inf"` respectively; both were parameterized via
`GC_MODULE_NAME`/`GC_GAME_DIR` compile definitions (default unchanged for
`csgo_gc`; `tf2_gc_hook/CMakeLists.txt` sets them to `"tf2_gc"`/`"tf"`).

`launcher_unix.cpp` hardcoded the GC dylib path as `"csgo_gc/..."`;
parameterized via `GC_MODULE_NAME` the same way, defaulting to `"csgo_gc"`.
The new `tf` launcher target (`launcher/CMakeLists.txt`) sets it to `"tf2_gc"`.

## Known gaps / what to check on first real launch

- **`GameProfile` interface strings are guesses.** `g_profileTF2`
  (`csgo_gc/game_profile.cpp`) reuses CS:GO's `GAMEEVENTSMANAGER002`/
  `VEngineClient014` interface version strings on the assumption TF2's
  engine build is close enough (both Source 1, similar vintage). If hooking
  silently fails to find these interfaces on launch, dump the actual
  strings out of TF2's `engine.dylib`/`.so`/`.dll` and fix this struct.
- **`AppId::IsOriginal()` still hardcodes `== 730`** (CS:GO/CS2's shared
  appid). For TF2 (appid 440) this will simply always be false, which only
  gates some CS:GO-specific server-browser/stats spoofing paths in
  `steam_hook.cpp` — should be harmless no-ops for TF2, but unverified.
- **Deployment layout**, verified against a real TF2 install (game root has
  `bin/`, `hl2/`, `platform/`, `tf/`, `tf.exe`, `tf_win64.exe`,
  `steam_appid.txt`; no macOS/arm64 client exists anymore, see below):
  - copy `examples/tf2_items_game.txt` -> `tf2_gc/tf2_items_game.txt`
  - copy `examples/tf2_unusual_inventory.txt` -> `tf2_gc/tf2_inventory.txt`
  - create `tf2_gc/config.txt` with `"game" "tf2"` and `"appid_override" "440"`
    (see `csgo_gc/config.txt` for the format `GCConfig` parses)
  - back up `tf.exe`/`tf_win64.exe` (or `tf_linux64` on Linux) and replace
    with the built `tf`/`tf_win64` binary from the same release zip
- **No macOS testing is possible.** Valve pulled TF2's macOS binaries in
  April 2024 and later delisted macOS as a supported platform entirely; the
  `tf` launcher target still builds on macOS in CI for consistency, but
  there's no real TF2 client left to hook there. Test on Windows or Linux
  (Linux got a 64-bit client/server executable in the same April 2024 update).
- Never tested against a live TF2 client/GC connection. Compiles, links, and
  the CS:GO/CS2 build was confirmed unaffected (see git history for this
  change), but the actual hook attach + handshake round-trip is unverified.
