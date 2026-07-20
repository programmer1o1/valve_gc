# TF2 live GC hook

This documents the `tf2_gc` target: a second, separate GC dylib that wires
milestone 1's TF2 item-schema/inventory parser (`tf2_gc/`) into the same
hook/threading/job-id plumbing `csgo_gc` uses for CS:GO/CS2.

**Status (2026-07-21): backpack display confirmed working against a real TF2
client.** The hook attaches, the GCSDK handshake completes, and the
injected backpack (hats + a diagnostic stock weapon) shows up in-game —
see "Seventh real launch result" below for the bug that was blocking this
and how it was found. Equip handling (`k_EMsgGCAdjustItemEquippedState`)
was added right after but is **not yet verified live** -- see "Known gaps".

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
- Equipping is implemented: `ClientGCTF2` keeps a live, mutable
  `itemId -> CSOEconItem` map (`m_liveItems`, built once at startup from the
  parsed inventory) and handles `k_EMsgGCAdjustItemEquippedState` the same
  way `csgo_gc/inventory.cpp`'s `Inventory::EquipItem`/`UnequipItem` do:
  unequip whatever else occupies the target (class, slot), then append a
  `CSOEconItemEquipped{new_class, new_slot}` entry to the target item and
  send the change via `k_ESOMsg_UpdateMultiple`. Confirmed via the real
  client source (`econ_item_inventory.cpp`'s
  `CInventoryManager::UpdateInventoryEquippedState`) that this is the exact
  message TF2 sends — no TF2-specific equip message exists, it's shared
  generic econ infrastructure. `BuildBackpackSOCache` now serializes from
  `m_liveItems` (reflecting current equip state) instead of rebuilding items
  fresh from the parsed inventory every time, so equips persist across SO
  cache resends within a session (not across restarts -- see Known gaps).

## What it deliberately does NOT do

- No crafting, trading, case opening/unlocking, store, or MvM/Halloween
  messages. Only backpack display + equipping are implemented.
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

## Confirmed via IDA against a real TF2 install (2026-07-20)

A first real launch attempt (Windows TF2 under CrossOver) hit
`"steamclient64.dll not loaded — cannot install GC hooks"`. Loaded
`tf_win64.exe`, `bin/x64/launcher.dll`, and `bin/x64/steam_api64.dll` into
IDA to find the actual cause instead of guessing:

- `tf_win64.exe`'s `WinMain` is a thin stub: it loads `bin\x64\launcher.dll`
  and calls its exported `LauncherMain`, confirming `bin/x64/` (not a
  CS2-style `win64/`) is correct, and that `tf.exe`/`tf_win64.exe` really are
  TF2's own launcher executables (not a generic `hl2.exe`).
- `launcher.dll`'s real `LauncherMain` takes **zero parameters** in this
  build (Hex-Rays folds away unused args) — our shared `launcher_win.cpp`
  passes it 5 (`bSecure, hInstance, hPrevInstance, lpCmdLine, nShowCmd`).
  Harmless *for this build* since it never reads them, but worth noting if
  a future TF2 update's `launcher.dll` does read args.
- **Root cause of the actual error, confirmed by decompiling
  `steam_api64.dll`:** `SteamAPI_Shutdown()` unconditionally calls
  `FreeLibrary()` on the module handle it loaded for `steamclient64.dll`.
  Windows `LoadLibrary`/`FreeLibrary` is refcounted, so this is harmless
  when something else (normally Steam's own overlay injection when it
  launches a game) already holds a reference — but under Wine/CrossOver,
  where overlay injection frequently doesn't happen, our own probe call was
  the *only* reference, so `ShutdownSteamAPI()`'s `FreeLibrary()` genuinely
  unloaded `steamclient64.dll` right before `InstallSteamClientHooks()`
  checked for it. **Fixed** by no longer calling `ShutdownSteamAPI()` in
  `SteamHookInstall()` (applies to CS:GO/CS2 too — the bug wasn't
  TF2-specific, just more likely to surface under Wine).
- `launcher.dll` itself calls `SteamAPI_InitSafe()` (stricter than
  `SteamAPI_Init()` — requires an extra internal flag), but only if
  `GameOverlayRenderer.dll` isn't already loaded and `SteamGameId` isn't
  already set in the environment — i.e. it skips its own Steam init
  entirely when Steam already bootstrapped the process. Our code doesn't
  replicate this check, but since we no longer tear down our own session,
  this shouldn't matter in practice.

## First real launch result (2026-07-20, after the steamclient64.dll fix)

Hook attached and the GCSDK handshake worked: console showed
`CTFGCClientSystem::PostInitGC`, `CTFGCClientSystem - adding listener`, and
`Connection to game coordinator established.` Ranking loaded correctly, but
the item/economy backend ("Item Server") failed to connect and the loadout
didn't load. Root cause: **the release zip never shipped a `tf2_gc/config.txt`**,
so `GCConfig::AppIdOverride()` fell back to its default of `730` (CS:GO/CS2's
appid) instead of TF2's `440`. `SteamHookInstall()` sets the `SteamAppId`
environment variable from that value *before* the real `launcher.dll` runs its
own Steam init, so the process's Steam session likely got tagged with the
wrong appid for anything appid-specific (item/economy), while appid-agnostic
things (login, ranking) worked fine regardless. Fixed by adding
`examples/tf2_config.txt` (`"game" "tf2"`, `"appid_override" "440"`) and
packaging it as `tf2_gc/config.txt` in CI, same as `csgo_gc/config.txt`.

Not yet confirmed whether this alone fixes the item server / loadout, or
whether `ClientGCTF2`'s SO cache response has an additional problem — next
test will tell.

## Second real launch result (2026-07-20, after shipping tf2_gc/config.txt)

Still failed the same way even with `tf2_gc/config.txt` correctly deployed
(confirmed via `ls`). Got a `tf2_gc/gc_log.txt` this time (the earlier
logging-path fix worked) and it showed the actual bugs directly:

- `ClientGCTF2: failed to load "../../tf2_gc/tf2_items_game.txt" /
  "../../tf2_gc/tf2_inventory.txt", backpack will be empty` — wrong path.
  `config.cpp`'s *fallback* path (`tf2_gc/config.txt`, no `../../` prefix)
  had already succeeded by this point (proven by the correct appid 440
  showing up), meaning the real CWD when our code runs is the **game
  root** — because `tf.exe`/`tf_win64.exe` sit directly in the game root and
  our own stub never `chdir`s (the real `launcher.dll` does that later,
  after we've already run). `ClientGCTF2` only had the wrong "../../" path
  with no fallback. **Fixed**: added the same two-path fallback pattern
  `config.cpp` already uses, un-prefixed path tried first.
- `GameProfile: using CS:GO profile` — despite `config.txt` correctly
  saying `"game" "tf2"` (proven by the correct appid). Root cause, found by
  just reading `config.cpp`: its constructor only ever checked
  `if (game == "cs2")` — there was no `"tf2"` branch at all, so `m_game`
  just stayed at its default value `"csgo"` no matter what the file said.
  **Fixed**: added the missing `else if (game == "tf2")` branch.

Also visible in the log but not yet investigated: `ClientGCTF2::HandleMessage`
logged several unhandled messages right after the welcome —
`k_EMsgGCMOTDRequest`, `k_EMsgGCStoreGetUserData`, and two protobuf messages
IDs not even in our known-message table (logged as `UNKNOWN MESSAGE`,
presumably TF2-specific messages from `tf_gcmessages.proto` that were never
ported here). Unclear yet whether the client tolerates not getting replies
to these or whether one of them is why the session gets dropped — next test
(with the path/profile fixes in) will clarify whether these still matter.

## Third real launch result (2026-07-20, path/profile fixes confirmed, item server still dead)

Both fixes above confirmed working in `gc_log.txt`: `GameProfile: using TF2
profile` and `ClientGCTF2: loaded 7 backpack entries from tf2_gc/tf2_items_game.txt
items / tf2_gc/tf2_inventory.txt particles`, followed by `ClientGCTF2: sending
welcome with 7 backpack items`. Progress — but the item server was still dead
in-game, and the log went completely silent (no more traffic at all, not even
the per-tick `round_mvp` listener retry spam that had been constant up to that
point) right after one of the unhandled messages repeated exactly 3 times:
`ClientGCTF2::HandleMessage: unhandled protobuf message UNKNOWN MESSAGE`,
`type=2147484698` (unmasked: `1050`).

Identified via IDA (`client.dll`): `sub_1806332A0` sends
`GCSDK::CProtoBufMsg<CMsgRequestInventoryRefresh>` with wire value **1050**,
triggered by a UI string `"#NoSteamNoItems_Refresh"` — i.e. this is literally
the backpack UI's own "no items, refresh" action explicitly asking the GC to
resend the inventory. We never answered it, so after retrying 3 times the
client evidently gave up (matching the "item server" going dead and the
otherwise-constant per-tick log traffic stopping entirely).

**Fixed**: added `k_EMsgGCRequestInventoryRefresh = 1050` (not in the
committed generated protobuf headers' enum, same situation as
`k_EMsgGCOpenCrate` in `csgo_gc/gc_client.cpp` — defined locally) and a
handler that just resends the same backpack `CMsgSOCacheSubscribed` via
`k_ESOMsg_CacheSubscribed`, the same message type `ReloadInventory`/etc.
already use in `csgo_gc/gc_client.cpp` for resending an updated SO cache.

Not yet confirmed whether this was the *only* missing piece — the other
unhandled messages (`k_EMsgGCMOTDRequest`, `k_EMsgGCStoreGetUserData`, one
more `UNKNOWN MESSAGE`) are still unhandled and might also matter, but
`CMsgRequestInventoryRefresh` being sent 3 times right as the connection died
was a strong enough signal to fix first and retest.

## Fourth real launch result (2026-07-20, refresh handler confirmed, but still no backpack)

`gc_log.txt` confirmed the new handler fires: `ClientGCTF2:
OnRequestInventoryRefresh, resending 7 backpack items`. But the client
requested a refresh *again* about 2500 lines later, meaning it still doesn't
consider the backpack populated even after receiving 7 items twice — this
pointed away from "missing handler" and toward "the items themselves are
malformed or unrecognized."

Root cause, found by reading TF2's own real `tf/scripts/items/items_game.txt`
(the client validates SO cache items against its own loaded schema): **every
def_index in `tf2_items_game.txt`/`tf2_unusual_inventory.txt` was fabricated
and didn't correspond to any real TF2 item.** E.g. "Team Captain" was
invented as `30713`, which doesn't exist in the schema at all; "Ghastly
Gibus" was invented as `247`, which does exist but is a completely unrelated
quest-condition entry, not an item (the real Ghastly Gibus is `116`). The
client silently discards SO cache items with unrecognized def_index values,
so we were successfully sending "7 backpack items" that the client just
threw away.

Also discovered while fixing this: the real
`attribute_controlled_attached_particles` block has a different structure
than assumed -- nested `other_particles`/`cosmetic_unusual_effects`
subsections, each entry holding a `system` field with an internal particle
name (e.g. `superrare_burning1`), not a flat id->display-name map. This
doesn't affect our own parser (it reads a small hand-authored file in our
own simpler format, not the real one), but it did mean some of the
particle *id* numbers were also wrong (`29`/`34` were invented as "Poison
Clouds"/"Sunbeams" but are really `unusual_storm`/`unusual_bubbles`; `9`,
`10`, `13`, `14` happened to already be correct).

**Fixed**: rebuilt `examples/tf2_items_game.txt` and
`examples/tf2_unusual_inventory.txt` using defindexes (`49` Football Helmet,
`50` Prussian Pickelhaube, `51` Pyro's Beanie, `52` Batter's Helmet, `116`
Ghastly Gibus) and particle ids (`9`/`10`/`13`/`14`) verified directly
against a real TF2 install's `items_game.txt`. Attribute `134` ("attach
particle effect") was already correct.

## Fifth real launch result (2026-07-20, real defindexes still don't show up)

Still no backpack items, even with real, schema-verified defindexes/particle
ids. `CMsgRequestInventoryRefresh` kept recurring on a steady interval rather
than the earlier "3 tries then silence" pattern — in hindsight that may just
be TF2's backpack UI periodically re-syncing regardless of item validity,
not necessarily a rejection signal; that theory was likely wrong.

Went back into `client.dll` looking for schema-validation logic. Ruled out
one lead (`CMsgClientWelcome.game_data`, which `CTFGCClientSystem::OnClientWelcome`
only reads if a "has" bit is set, and we never populate it): the function
that consumes it (`sub_18091AF40`) turned out to be generic string-copy
bookkeeping, not schema/version validation, so leaving `game_data` unset is
harmless.

Found two real, concrete bugs while cross-checking our sent `CSOEconItem`
fields against the real schema:

- **`item.set_level(1)` for everything.** TF2's real `items_game.txt` entry
  for Ghastly Gibus explicitly requires `min_ilevel`/`max_ilevel` = `10`.
  This also matches general TF2 convention: Unusual-quality items are
  always level 10 regardless of the base item. **Fixed**: `BuildEconItem`
  now sends level 10 for any item with a particle effect (Unusual), level 1
  otherwise.
- **`item.set_origin(0)`**, plausibly `k_EItemOriginInvalid` in Valve's
  shared origin enum. `csgo_gc/gc_const_csgo.h` already has a named
  constant for exactly this "locally-injected default item" scenario
  (`ItemOriginBaseItem = 22`). **Fixed**: added the same constant
  (`ItemOriginBaseItemTF2 = 22`) and use it instead of `0`.

Not confirmed to be sufficient -- these are concrete, well-evidenced fixes
(one backed directly by the real schema, one by established precedent
elsewhere in this codebase), not a guarantee. If items still don't show up
after this, the next step is watching TF2's own in-game console (not just
`gc_log.txt`, which only shows our side) at the moment the backpack UI would
normally populate, to see what `client.dll` itself reports.

## Sixth real launch result (2026-07-21, level/origin fixes didn't change anything visible)

Same exact wire pattern as before (6 items sent, same recurring refresh,
same 4 unhandled messages) -- from our side nothing looked different, and
the popup persisted. Went hunting for the exact popup text instead of
continuing to assume: found it in `tf/resource/tf_english.txt` --
`"NoGCNoItems" "LOADOUT NOT AVAILABLE - COULD NOT CONNECT TO ITEM SERVER"`.

Identified the three previously-"UNKNOWN"/unhandled messages via the real
`tf_gcmessages.proto`/`econ_gcmessages.proto` (from `ValveSoftware/source-sdk-2013`,
found by numeric ID rather than guessing): `k_EMsgGCDataCenterPing_Update`
(6528), `k_EMsgGC_TFClientInit` (6536, sent once after `ClientWelcome` with
client version/language, no reply message exists in the proto), and
`k_EMsgGCRespawnPostLoadoutChange` (1029, client -> GC -> game server). All
three are one-way/fire-and-forget with no expected GC reply, ruling them out
as the blocker.

Then found the real client-side inventory source
(`ValveSoftware/source-sdk-2013`'s `game/shared/econ/econ_item_inventory.cpp`,
real C++, not decompiled) and traced the actual bulk-load path, which is
different from the per-item `CPlayerInventory::SOCreated` callback we'd
been staring at in IDA (that one only handles *incremental* updates after
the initial load; for the initial `CMsgClientWelcome`-embedded SO cache, it
does nothing). The real path is `CPlayerInventory::SOCacheSubscribed`,
which iterates the SO cache's item type cache and calls `AddEconItem` for
each item, then fires `SendItemSystemConnectedEvent()` (very likely what
clears the "NoGCNoItems" state) -- but only once, for the local player's
inventory.

`AddEconItem` -> `FilloutItemFromEconItem` -> `CEconItemView::Init()`:
```cpp
m_iItemDefinitionIndex = iDefIndex;
CEconItemDefinition *pData = GetStaticData();
if ( !pData )
{
    // We've got an item that we don't have static data for.
    return;   // m_bInitialized stays false
}
...
m_bInitialized = true;
```
`IsValid()` is just `return m_bInitialized;`. So every item we send lives or
dies on whether `GetStaticData()` (schema lookup by our `def_index`)
succeeds client-side. This is as far as static source analysis can take it
without live debugging -- next step is an empirical isolation test: added
defindex `13` (stock Scattergun, `"baseitem" "1"` in the real schema,
guaranteed loaded by every client) as a plain item alongside the 5 hats in
`tf2_items_game.txt`/`tf2_unusual_inventory.txt`. If the Scattergun shows up
in-game while the hats don't, the problem is hat-specific; if neither shows
up, it's more fundamental (schema not loaded yet, wrong SO cache owner id,
etc).

## Seventh real launch result (2026-07-21): the Scattergun didn't show up either -- found the real bug

Neither the Scattergun nor the hats showed up; the game just showed the
default loadout. Since even a guaranteed-valid, always-loaded item failed,
this ruled out per-item schema lookup problems and pointed at something
upstream of item validation entirely -- most likely the whole SO cache
subscription never reaching the local player's inventory at all.

Fetched the actual GCSDK client-side C++ (not decompiled, from
`nillerusr/source-engine`, a leaked 2017-era Source engine build --
`gcsdk/gcclient.cpp` and `gcsdk/gcclient_sharedobjectcache.cpp`) and found
it: `CGCSOCacheSubscribedJob::BYieldingRunGCJob` looks up the target cache
via `m_pGCClient->FindSOCache( msg.Body().owner(), true )` -- **`owner()`,
not `owner_soid()`**. Checked the real `gcsdk/gcsdk_gcmessages.proto` in the
same repo:

```proto
message CMsgSOCacheSubscribed
{
	...
	optional	fixed64			owner = 1;		// the owner of this cache
	repeated	SubscribedType	objects = 2;
	optional	fixed64			version = 3;
}
```

Every SO-cache-related message in the real proto has this same pattern: an
original `owner` (plain `fixed64` steam id) field at a low field number,
**and nothing else** for `CMsgSOCacheSubscribed` specifically -- `owner_soid`
doesn't exist there at all in the real schema. Checked our own
`protobufs/gcsdk_gcmessages.proto`: it only ever defines `owner_soid`
(`CMsgSOIDOwner`, a newer/different field) on every one of these messages --
`owner` was missing entirely, on all of them. Since this project's protobufs
were reverse-engineered purely from CS:GO/CS2 wire traffic, and those never
seem to send/need the old `owner` field, it was simply never present in this
file. TF2's much older client only reads `owner`, so every SO cache message
we ever sent had owner unset (defaulting to 0) -- meaning our items were
attached to a bogus, unowned cache the local player's `CPlayerInventory`
never looks at, regardless of what item data was inside. This fully explains
"fundamental, item-independent failure, always falls back to default
loadout."

**Fixed**: added the missing `owner` field (matching the real proto's exact
field numbers) to `CMsgSOSingleObject`, `CMsgSOMultipleObjects`,
`CMsgSOCacheSubscribed`, `CMsgSOCacheUnsubscribed`,
`CMsgSOCacheSubscriptionCheck`, and `CMsgSOCacheSubscriptionRefresh` in
`protobufs/gcsdk_gcmessages.proto`, regenerated the `.pb.cc`/`.pb.h` (had to
build a matching-version `protoc` from the exact pinned commit in
`CMakeLists.txt` -- a locally installed newer `protoc` emits an incompatible
new-generation format), and set `message.set_owner(m_steamId)` in
`ClientGCTF2::BuildBackpackSOCache` alongside the existing `owner_soid`.
Purely additive to the shared proto file; CS:GO/CS2 never read or set the
new field, so this shouldn't affect them.

**Retested (2026-07-21): confirmed fixed.** Both the diagnostic Scattergun
and the 5 hats now show up in the backpack in-game, and the "couldn't
connect to item server" popup is gone. This was the real bug -- the earlier
config/path/level/origin fixes were all real, necessary bugs too (config.cpp
genuinely didn't recognize `"tf2"`, the schema/inventory path was genuinely
wrong, level/origin were genuinely hardcoded incorrectly), but none of them
were sufficient on their own because the SO cache was never reaching the
local player's inventory at all until the `owner` field was added.

Not yet working: equipping items (loadout slot assignment). `ClientGCTF2`
only implements backpack population (`ClientHello`/`Welcome` and
`RequestInventoryRefresh`); it doesn't handle whatever message TF2 sends for
equipping (CS:GO's equivalent is `k_EMsgGCAdjustItemEquippedState` /
`AdjustEquipSlots`, building `SOTypeEquipSlot` objects) at all yet. See
`csgo_gc/inventory.cpp`'s `BuildCacheSubscription` for how CS:GO builds
`CSOEconItemEquipped`/equip-slot SO objects as a reference for what TF2
would need.

## Known gaps / what to check on first real launch

- **`GameProfile` interface strings are still guesses.** `g_profileTF2`
  (`csgo_gc/game_profile.cpp`) reuses CS:GO's `GAMEEVENTSMANAGER002`/
  `VEngineClient014` interface version strings on the assumption TF2's
  engine build is close enough (both Source 1, similar vintage). Not yet
  checked against `engine.dll` — if hooking attaches (fixed above) but the
  engine-side features silently no-op, dump the actual interface strings
  out of TF2's `engine.dll` next.
- **`AppId::IsOriginal()` still hardcodes `== 730`** (CS:GO/CS2's shared
  appid). For TF2 (appid 440) this will simply always be false, which only
  gates some CS:GO-specific server-browser/stats spoofing paths in
  `steam_hook.cpp` — should be harmless no-ops for TF2, but unverified.
- **Deployment layout**, verified against a real TF2 install (game root has
  `bin/`, `hl2/`, `platform/`, `tf/`, `tf.exe`, `tf_win64.exe`,
  `steam_appid.txt`; no macOS/arm64 client exists anymore, see below):
  - the release zip now bundles `tf2_gc/config.txt` (from
    `examples/tf2_config.txt`), `tf2_gc/tf2_items_game.txt`, and
    `tf2_gc/tf2_inventory.txt` automatically -- no manual copying needed
  - back up `tf.exe`/`tf_win64.exe` (or `tf_linux64` on Linux) and replace
    with the built `tf`/`tf_win64` binary from the same release zip
- **No macOS testing is possible.** Valve pulled TF2's macOS binaries in
  April 2024 and later delisted macOS as a supported platform entirely; the
  `tf` launcher target still builds on macOS in CI for consistency, but
  there's no real TF2 client left to hook there. Test on Windows or Linux
  (Linux got a 64-bit client/server executable in the same April 2024 update).
- **Equip state doesn't persist across restarts.** `ClientGCTF2::EquipItem`
  mutates `m_liveItems` in memory only; there's no
  `csgo_gc/inventory.cpp`-style `Save()`/`ReadFromFile()` round-trip to
  `tf2_gc/tf2_inventory.txt`, so equips are lost when the game restarts. Not
  yet tested against a live client either (implemented after the backpack
  fix was confirmed working; equipping itself is unverified).
- No `SOTypeEquipSlot`/`CSOEconEquipSlot` broadcast to a game server (that's
  what lets *other players* see your equipped cosmetics) -- out of scope
  along with the rest of in-match cosmetic visibility, see above.
- Confirmed working against a live TF2 client/GC connection as of
  2026-07-21 (see "Seventh real launch result" above) -- hook attach,
  handshake, and backpack population all verified in-game, not just
  compiled/linked. Equipping was added afterward and is not yet verified
  live.
