# csgo_gc — Architecture & Internals

This document explains, in detail, how `csgo_gc` works: how it injects itself into the
game, how it impersonates Valve's Game Coordinator (GC), how inventory and loadouts are
modeled, and how each user-visible feature (loadouts, in-match skins, case opening) flows
end to end. It is aimed at developers who want to modify or reason about the code.

> This is the **CS2-focused fork** of [mikkokko/csgo_gc](https://github.com/mikkokko/csgo_gc).
> File/line references point at the current tree; treat them as a guide, not a contract — read
> the code next to this doc.

---

## 1. What problem is being solved?

In Valve games the **Game Coordinator** is a backend service. Your game client maintains a
persistent connection to it for everything inventory-related: your loadout, your skins, the
Shared Object (SO) cache that mirrors your inventory, opening cases, the in-game store, etc.
Normally this is a network conversation with Valve's servers.

`csgo_gc` deletes that conversation partner and replaces it with an **in-process
implementation**. The game still thinks it is talking to a GC over the Steam
`ISteamGameCoordinator` interface — but every message is intercepted, answered locally, and
the answers are driven by a local `inventory.txt` file instead of Valve's database.

Two things make this hard:

1. **There is no spec.** The GC protocol is reverse-engineered from the game binaries. Message
   ids, field layouts, and the exact reply a given request expects are all derived from RE.
2. **CS2 is a moving target.** The upstream project targeted CS:GO (32-bit). CS2 is Source 2,
   64-bit, with a rewritten Panorama UI and changed GC messages (e.g. the old struct-based
   `k_EMsgGCUnlockCrate` became the protobuf `k_EMsgGCOpenCrate` *job*). Much of this fork is
   re-deriving those differences.

---

## 2. High-level architecture

```
                          game process (cs2.exe / csgo.exe / srcds)
   ┌─────────────────────────────────────────────────────────────────────────────────┐
   │                                                                                 │
   │   game code ── ISteamGameCoordinator ──▶  [ csgo_gc hooks / proxies ]           │
   │      ▲   SendMessage / RetrieveMessage           │                              │
   │      │   IsMessageAvailable                      │ PostToGC(event)              │
   │      │                                           ▼                              │
   │   injected callbacks                       ┌──────────────┐                     │
   │   (GCMessageAvailable_t,                   │  SharedGC    │  worker thread      │
   │    SteamServersConnected_t)                │  event queue │                     │
   │      ▲                                     └──────┬───────┘                     │
   │      │ GetHostEvents()                            │ HandleEvent()               │
   │      │                                ┌───────────┴───────────┐                 │
   │   [ Steam_BGetCallback /              │                       │                 │
   │     RunCallbacks hooks ]          ClientGC                ServerGC              │
   │                                    │                        │                   │
   │                                 Inventory               (relays client          │
   │                                 (inventory.txt)          SO caches,             │
   │                                 ItemSchema               validates)             │
   │                                 (items_game.txt)            │                   │
   │                                    │                        │                   │
   │                              NetworkingClient ◀── Steam P2P ──▶ NetworkingServer│
   │                              (listen-server clients <-> host, channel 7)        │
   └─────────────────────────────────────────────────────────────────────────────────┘
```

Key objects:

- **`SteamGameCoordinatorProxy`** — the fake `ISteamGameCoordinator` the game talks to.
- **`SharedGC`** — base class with a worker thread + event queue. Two subclasses:
  - **`ClientGC`** — owns *your* inventory. Answers handshake, loadout, store, case-open, etc.
  - **`ServerGC`** — runs on a listen/dedicated server. Relays connected players' SO caches.
- **`Inventory`** — parses/serializes `inventory.txt`, builds SO cache messages, equips items.
- **`ItemSchema`** — parses `items_game.txt` (defs, paint kits, loot lists, rarities).
- **`NetworkingClient` / `NetworkingServer`** — Steam P2P transport so skins show on other
  players in *your* match.

---

## 3. Injection & entry path

### 3.1 The launcher

The shipped archive replaces the game's launcher executable (`cs2.exe`, `csgo.exe`,
`srcds.exe`, …). The launcher (`launcher/`) loads `csgo_gc.dll` and calls one of two exported
entry points (`csgo_gc/main.cpp`) **before** handing control to the real game main:

- **`InstallGC(bool dedicated)`** (`main.cpp:13`) — CS:GO path. Calls `Platform::Initialize()`
  then `SteamHookInstall()`.
- **`PreInstallGC(bool dedicated)`** (`main.cpp:21`) — CS2 path. Calls `Platform::Initialize()`
  then `SteamHookPreInstall()`, which hooks `SteamAPI_InitFlat` so the rest of setup runs once
  tier0/SteamAPI is fully initialized.

### 3.2 What gets hooked (`steam_hook.cpp`)

`InstallSteamClientHooks()` (`steam_hook.cpp:2825`) installs **funchook inline hooks** on the
Steam client surface:

| Hooked symbol | Hook | Purpose |
|---|---|---|
| `CreateInterface` | `Hk_CreateInterface` | wrap returned Steam interfaces in proxies |
| `SteamInternal_FindOrCreateUserInterface` | `Hk_…` | (Windows) intercept `SteamGameServer*` etc. |
| `SteamAPI_RegisterCallback` / `Unregister` | `Hk_…` | capture the game's GC/connection callbacks |
| `SteamAPI_RunCallbacks` | `Hk_SteamAPI_RunCallbacks` | pump client GC events each frame |
| `SteamGameServer_RunCallbacks` | `Hk_SteamGameServer_RunCallbacks` | pump server GC events + P2P receive |
| `Steam_BGetCallback` / `Steam_FreeLastCallback` | `Hk_…` | (Windows) inject fake callbacks |

It also patches the **`ISteamGameCoordinator` vtable directly** (`HookGCVtable`,
`steam_hook.cpp:2192`): slot 0 `SendMessage`, slot 1 `IsMessageAvailable`, slot 2
`RetrieveMessage` — using `VirtualProtect` to flip the page writable.

---

## 4. Steam interface interception

The game asks Steam for interfaces by version string. The hooked `CreateInterface` /
`SteamClientProxy` returns **proxy objects** instead of the real ones, so we can intercept the
calls that matter and pass everything else through.

- **`SteamClientProxy`** (`steam_hook.cpp:1848`) — wraps the root `ISteamClient`. Holds a map
  of `SteamInterfaceProxy` per pipe/user. Every `GetISteam*()` goes through `ProxyInterface()`.
- **`SteamInterfaceProxy::GetInterface()`** (`steam_hook.cpp:1784`) — dispatches by version
  string to a specific proxy:
  - `STEAMGAMECOORDINATOR_INTERFACE_VERSION` → **`SteamGameCoordinatorProxy`**
  - `STEAMUTILS_…` → `SteamUtilsProxy`, `STEAMUSER_…` → `SteamUserProxy`,
    `STEAMGAMESERVER_…` → `SteamGameServerProxy`, etc.
- **`SteamGameCoordinatorProxy`** (`steam_hook.cpp:531`) is the heart of it. Its constructor
  (line 536) lazily creates the GC objects (see §7.4), and its overrides are the GC API the
  game uses:
  - `SendMessage()` (line 572) → `PostToGC(GCEvent::Message, …)` on the right GC.
  - `IsMessageAvailable()` (line 590).
  - `RetrieveMessage()` (line 602) — note: if the game's buffer is too small it **drops** the
    message rather than looping, which previously froze the game.

---

## 5. The CS2 GC connection dance

On real Steam, CS2's `CCSGCClientSystem` only requests a GC session **in response to a
`SteamServersConnected_t` callback**. Offline / spoofed, that callback never fires, so the game
never asks for the GC and the whole system stays dormant.

`csgo_gc` nudges it. In `Hk_Steam_BGetCallback` (`steam_hook.cpp:2772`):

- While `!s_cs2RequestedGC && s_steamServersConnectedTicksLeft > 0`, it **injects a fake
  `SteamServersConnected_t`** callback (line 2779). This makes the game request the GC
  interface.
- A tick counter (`s_steamServersConnectedTicksLeft`, capped at 600) stops the nudging from
  running forever.
- Once the game actually requests the GC interface, `s_cs2RequestedGC` is set (in
  `Hk_GetISteamGenericInterface_direct` / `Hk_SteamInternal_FindOrCreateUserInterface`,
  ~lines 2251 / 2456) and the injection stops.

After connection, GC→game delivery also rides on `Steam_BGetCallback`: when `ClientGC` has a
message queued, the hook synthesizes a `GCMessageAvailable_t` callback (line 2794), gated by
`s_gcInjectedThisTick` so at most one is injected per tick.

This injection layer is the single most CS2-specific part of the project — it's what gets the
game to *open the conversation* in the first place.

---

## 6. Message transport & wire format (`gc_message.cpp/.h`)

GC messages are length-prefixed blobs. The high bit of the message-type dword
(`ProtobufMask = 0x80000000`) flags protobuf vs legacy struct messages.

**`GCMessageRead`** parses an incoming blob:
- First dword: message type. `IsProtobuf()` tests the mask; `TypeUnmasked()` strips it.
- **Protobuf:** next dword is header size; if non-zero a `CMsgProtoBufHeader` follows, from
  which `job_id_source` is captured into `m_jobId` (`JobId()`). The remainder is the protobuf
  payload.
- **Struct (legacy):** fixed zero-filled header, then the raw struct.

**`GCMessageWrite`** builds an outgoing blob:
- Protobuf ctor writes `type | ProtobufMask`; if a `jobId` is supplied it emits a
  `CMsgProtoBufHeader` with **`job_id_target` set** (this is how a reply is routed back to the
  request that is waiting on it — see §10.4), otherwise header size 0, then the serialized
  message.
- Struct ctor and raw-bytes ctor exist for legacy messages.

The **job id** mechanism is essential: several client requests (store purchase, **case
opening**) are sent as GCSDK *jobs* and the game blocks a job waiting for a reply addressed to
the same id. Answer the job → the UI completes; don't → it times out and shows an error.

---

## 7. The GC core

### 7.1 `SharedGC` — threading & event model (`gc_shared.cpp/.h`)

`SharedGC` owns a **worker thread** and two queues guarded by mutexes:

- **Host → GC** (`PostToGC`): the game thread pushes work (a GC message, a P2P net message, an
  SO-cache request) as a `GCEvent`. The worker thread pops it and calls the virtual
  `HandleEvent(GCEvent, id, buffer)`.
- **GC → Host** (`GetHostEvents`): the GC pushes replies as `HostEvent`s; the RunCallbacks
  hooks drain them on the game thread and deliver them (as injected callbacks / SO updates).

`GCEvent` distinguishes the *source* of work (`Message`, `NetMessage`, `SOCacheRequest`,
`ReloadInventory`, round/MVP events …); within a `Message`, dispatch is by GC message id.

This thread split keeps inventory work off the game's frame thread and serializes all mutation
of the inventory behind one worker.

### 7.2 `ClientGC` (`gc_client.cpp/.h`)

Owns the local `Inventory`. `HandleMessage(type, data, size)` (`gc_client.cpp:223`) is a big
switch over GC message ids. Notable handlers:

- **Handshake:** `k_EMsgGCClientHello` (+ `…R2..R4`) → `OnClientHello()` replies with
  `k_EMsgGCClientWelcome`, then matchmaking hello / rank / connection-status messages.
- **SO cache:** on request, `BuildCacheSubscription()` produces `CMsgSOCacheSubscribed` and it
  is pushed to the game (and, on a server, to clients).
- **Loadout:** `k_EMsgGCAdjustEquipSlots` (CS2's batched form) → `AdjustEquipSlots()` loops the
  slots calling `Inventory::EquipItem(...)`, emits `k_ESOMsg_UpdateMultiple`, and **`Save()`s**
  to disk. (`k_EMsgGCAdjustItemEquippedState` is the legacy per-item form.)
- **Case opening:** `k_EMsgGCOpenCrate` (2534) → `OpenCrate()` → `DoUnlockCrate()` (see §10.5).
- **Other:** `UseItem`, `SetItemPositions`, `ApplySticker`, name tags, trade-ups (`Craft`),
  store (`StorePurchaseInit/Finalize`), delete.

`SendMessageToGame(bool toGameServer, type, message, jobId = invalid)` (`gc_client.cpp:394`) is
the universal outbound helper:
- it always posts the message locally to the game client;
- if `toGameServer` is true it **also** routes it over the network to the server (so SO updates
  reach the host and thus other players);
- `jobId` makes it a job *reply* (sets `job_id_target`).

### 7.3 `ServerGC` (`gc_server.cpp/.h`)

Runs in the listen/dedicated server process. It does **not** own an inventory; it **relays and
validates** the SO caches that connected clients send it:

- `k_EMsgGCServerHello` / `…Server2GCClientValidate` (the GC-side of `BeginAuthSession`) →
  posts an `SOCacheRequest` so the relevant client pushes its cache.
- Incoming SO messages (`k_ESOMsg_Create/Update/Destroy/UpdateMultiple/CacheSubscribed`) are
  **validated**: the owner SOID must match the sending steamId, unequipped items are stripped
  (`RemoveUnequippedItems`), and an item cap is enforced (`MaxServerSOCacheItems`). Only then
  are they forwarded to the server's game code, which renders other players' skins.

### 7.4 Client vs server GC lifecycle

Both are held in a small template wrapper:

```cpp
template<typename GC, typename Networking>
class GCWrapper { GC m_gc; Networking m_networking; GCMessageQueue m_messageQueue; };
```

- `s_clientGC = new GCWrapper<ClientGC, NetworkingClient>{ SteamNetworkingMessages(), steamId }`
- `s_serverGC = new GCWrapper<ServerGC, NetworkingServer>{ SteamGameServerNetworkingMessages() }`

They are created lazily in the `SteamGameCoordinatorProxy` constructor (`steam_hook.cpp:536`)
— a proxy built with `steamId == 0` makes the server GC, a non-zero steamId makes the client GC
— and deleted/nulled in its destructor (lines 555-570). The RunCallbacks hooks always
null-check `s_serverGC`/`s_clientGC` before use because these can be torn down on map changes.

---

## 8. The Shared Object (SO) cache

The SO cache is the data model the game actually reads to know your inventory. csgo_gc emits
standard SO cache messages so the game treats local data as authoritative:

| Constant | Value | Meaning |
|---|---|---|
| `k_ESOMsg_Create` | 21 | a new SO (e.g. a freshly unboxed item) |
| `k_ESOMsg_Update` | 22 | single SO updated |
| `k_ESOMsg_Destroy` | 23 | SO removed (crate/key consumed) |
| `k_ESOMsg_UpdateMultiple` | 26 | batch update (loadout changes) |
| `k_ESOMsg_CacheSubscribed` | — | full snapshot of the cache |

SO **types** (`SOTypeId`, `gc_const_csgo.h`): `SOTypeItem = 1`, `SOTypeEquipSlot = 3`,
`SOTypeGameAccountClient = 7`, `SOTypeDefaultEquippedDefinitionInstanceClient = 43`, etc.

**Ordering matters.** When unboxing, the `k_ESOMsg_Create` for the new item must reach the game
*before* the job reply, because the reply handler immediately scans the SO cache for the new
item (by `origin == 5`). Get the order wrong and the item "isn't there yet."

---

## 9. Inventory & item schema

### 9.1 `inventory.txt` (`inventory.cpp`, `keyvalue.cpp`)

The inventory is a Valve-style KeyValue file (parsed by `keyvalue.cpp`'s recursive-descent
parser). `ReadFromFile()` / `WriteToFile()` round-trip it; `ReadItem()` / `WriteItem()` handle
one item. Shape:

```
"inventory"
{
    "items"
    {
        "2"                         // high item id (key)
        {
            "inventory"  "1"        // backpack position
            "def_index"  "507"      // item definition (weapon/agent/glove/…)
            "quality"    "4"
            "origin"     "24"
            "rarity"     "6"
            "attributes"
            {
                "6"  "38.000000"    // attribute def_index -> value (e.g. paint kit)
                "8"  "0.000001"     // wear
            }
            "equipped_state"
            {
                "3"  "0"            // class_id (CT=2 / T=3) -> slot_id (melee=0, …)
            }
        }
    }
    "default_equips"                // equipping a *default* (non-owned) item per slot
    {
        "61" { "class_id" "3"  "slot_id" "2" }
    }
}
```

- **Item ids** combine the account id (low 32 bits, derived from `steamId & 0xffffffff`) and a
  per-item "high id" (the KeyValue key). `AllocateItem()` auto-increments high ids and avoids
  the default-item id range.
- **Loadout** lives in each item's `equipped_state` (`class_id` → `slot_id`) plus the
  `default_equips` block for default items. Class ids: **CT = 2, T = 3**. Slot ids come from the
  schema's flexible-loadout slots (melee = 0, secondary, …, **agent = 38**, **gloves = 41**,
  music kit = 54, graffiti = 56).

> The game **rewrites `inventory.txt`** whenever you change loadout in the menu, so manual or
> external edits must be done with the game closed.

### 9.2 Building the cache (`BuildCacheSubscription`)

`Inventory::BuildCacheSubscription(msg, level, server)` turns the whole inventory into a
`CMsgSOCacheSubscribed`: it adds every item (`SOTypeItem`), an equip-slot object per
`equipped_state` (`SOTypeEquipSlot`), persona/level data, and—client-side only—the
`SOTypeGameAccountClient` record and default equips. On the **server** path, items without an
equipped state are skipped (only what's actually worn needs to reach other players).

### 9.3 Equipping (`EquipItem` / `UnequipItem`)

`EquipItem(itemId, classId, slotId, update)`:
- `slotId == 0xffff` → unequip the item from all slots;
- `itemId == 0` → unequip whatever currently occupies `(classId, slotId)`;
- a **default** item id → record it in `m_defaultEquips`;
- a real item → evict the current occupant of the slot, append an `equipped_state`, and emit
  both the item and the `CSOEconEquipSlot` update.

`AdjustEquipSlots` calls this for each slot in the batch and then `Save()`s — that is what makes
loadout changes **persist** across restarts.

### 9.4 Item schema (`item_schema.cpp`)

Loads `csgo/scripts/items/items_game.txt` and indexes items (with prefab inheritance),
attributes, sticker kits, paint kits (+ rarities), music definitions, item sets, and the
revolving/unusual/client **loot lists** used by case opening. `CreateItemFromLootListItem()`
fabricates a fully-formed `CSOEconItem`: for paintable items it rolls a seed (0–1000) and wear
(uniform in the kit's min/max float), sets the paint-kit attribute, and for StatTrak adds the
kill-eater attributes and bumps quality.

### 9.5 Case opening selection (`case_opening.cpp`)

`SelectItemFromCrate(crate, out)`:
1. resolve the crate's loot list via its `supply_crate_series` attribute;
2. flatten nested loot lists;
3. pick a rarity using configurable weights (`GetConfig().GetRarityWeight`);
4. pick a random item of that rarity;
5. decide StatTrak;
6. create the item with **`origin = ItemOriginCrate (5)`** and an "unacknowledged, found in
   crate" state.

The `origin == 5` is not cosmetic — the game's unbox-response handler **finds the new item by
scanning for `origin == 5`** (see §10.5).

---

## 10. End-to-end flows

### 10.1 Startup / GC handshake
1. csgo_gc nudges `SteamServersConnected_t` → CS2 requests the GC interface (§5).
2. Game sends `k_EMsgGCClientHello`; `ClientGC::OnClientHello` replies `ClientWelcome` + rank /
   matchmaking / connection-status messages.
3. Game requests the SO cache; `BuildCacheSubscription` answers with the full inventory.
4. Menus now show your loadout.

### 10.2 Changing loadout (and why it persists)
1. You equip a skin/knife/glove/agent in the menu.
2. Game sends `k_EMsgGCAdjustEquipSlots` (batched).
3. `AdjustEquipSlots` → `EquipItem` per slot → emits `k_ESOMsg_UpdateMultiple` → `Save()`
   rewrites `inventory.txt`.
4. Because it's persisted to disk, the change survives a restart.

### 10.3 Skins in-match on other players (P2P relay)
1. You start a listen server. `ServerGC` comes up; `NetworkingServer` opens a P2P channel.
2. When a player connects, the server sends a custom `k_EMsgNetworkConnect` (with the auth
   ticket) over Steam P2P channel 7.
3. The client validates it and posts an `SOCacheRequest`; `ClientGC` ships its SO cache to the
   server over P2P.
4. `ServerGC` validates (owner match, strip unequipped, cap) and forwards to the server's game
   code, which renders the right skins/agents/gloves on that player.

This is why skins only apply on a server **running csgo_gc** — Valve/community servers run their
own GC and never receive this cache.

### 10.4 Store purchase / jobs
`StorePurchaseInit` generates a transaction id, calls `Inventory::PurchaseItem`, and replies
`k_EMsgGCStorePurchaseInitResponse` **with the request's job id** so the waiting job completes.
This is the same job-reply pattern that case opening depends on.

### 10.5 Case opening (and the one remaining seam)

The real protocol was settled by reverse-engineering `client.dll`'s `CGCUnlockCrateResponse`
job (handles GC message **1008**, `k_EMsgGCUnlockCrateResponse`):

- The client sends `k_EMsgGCOpenCrate` (2534, `CMsgOpenCrate`: field 1 = key id, field 2 =
  crate id) **as a GCSDK job** and waits for a reply addressed to that job id.
- The job reply carries a **result code at offset 36**. **Zero = success**; on success the job
  itself scans the SO cache for the **highest-id item with `origin == 5`** and fires the X-ray
  reveal UI. **Non-zero (or a timed-out job)** is what shows the
  *"we are unable to retrieve your item"* dialog.

So unboxing is a **single GC step**, not two. `OpenCrate()` (`gc_client.cpp:935`) parses the two
ids out of the protobuf wire format and calls `DoUnlockCrate(crateId, keyId, JobId())`, which:
1. `Inventory::UnlockCrate` rolls the item (`origin = 5`), optionally destroys the crate/key;
2. sends `k_ESOMsg_Destroy` (crate, key) and `k_ESOMsg_Create` (new item) **first**;
3. sends `k_EMsgGCUnlockCrateResponse` with an empty body (**result 0**) addressed to the job
   id;
4. sends the `k_EMsgGCItemCustomizationNotification`.

> **Known limitation.** Functionally this works — the unboxed item is created, persisted, and
> appears in your inventory. But CS2 still pops the error dialog instead of playing the reveal.
> The notification only forwards to **Panorama JS** (`client.dll`'s `sub_180EC3A60` maps the
> request enum to a string and fires a `CUIEvent`); the reveal/claim UI lives in compiled
> Panorama, which can't be driven purely from the native side here. The dialog is cosmetic — the
> item is yours regardless. An earlier "two-step scan/commit (X-ray)" model was tried and
> reverted: it sent the reveal notification but never answered the job, so the job always timed
> out. (See `ItemOriginCrate` in `gc_const_csgo.h` and `DoUnlockCrate` in `gc_client.cpp`.)

---

## 11. Networking layer (`networking_*.{h,cpp}`)

Transport is Steam's P2P **`ISteamNetworkingMessages`** on a dedicated channel
(`NetMessageChannel = 7`), reliable send flags.

- **`NetworkingServer`** runs host-side. `ReceiveMessage` pulls one message, checks the sender
  has a session (`m_clients`), and hands it up. Session requests are accepted only for known
  clients (`OnSessionRequest`). `ClientConnected`/`ClientDisconnected` open/close channels and
  send the bootstrap `k_EMsgNetworkConnect`.
- **`NetworkingClient`** runs client-side: receives the connect bootstrap, validates the ticket,
  and triggers the SO-cache push; relays MVP/music-kit state with `k_EMsgNetworkMusicKitMVPState`.
- **Custom net messages** live in `networking_shared.h`:
  `k_EMsgNetworkConnect = (1u<<31) - 1`, `k_EMsgNetworkMusicKitMVPState = (1u<<31) - 2`.

> **Map-change crash fix (important detail).** `NetworkingServer` deliberately does **not** cache
> the `ISteamNetworkingMessages` pointer. On a map change the listen server restarts and that
> interface is freed; a cached pointer dangles and the next poll faults
> (`NetworkingServer::ReceiveMessage`). Instead, `Messages()` (`networking_server.h:48`)
> live-fetches `SteamGameServerNetworkingMessages()` each call, with null guards on every use.

---

## 12. Platform layer (`platform.h`, `platform_windows.cpp`, `platform_unix.cpp`)

`Platform::` abstracts everything OS-specific behind one header:

- `Initialize()` / `Print()` / `Error()` — logging via tier0's `ConColorMsg` (loaded by name),
  log file, and a native error dialog.
- `DataDir()` — where `inventory.txt` / `config.txt` live (relative to the game's bin dir).
- `SteamClientPath` / `SteamClientFactory` / `ModuleFactory` — locate and load
  `steamclient`/engine modules and fetch their `CreateInterface`.
- `PatchGraffitiPublicKey` / `PatchServerBrowserAppId` — in-place code patches (find the code
  section, `memmem` the pattern, flip protection, overwrite).
- `FileModificationTime` / `ModuleBase` — misc helpers.

Windows uses Win32 + PE walking; Unix uses `dlopen`/`dlsym` and Mach-O (macOS) or
`dl_iterate_phdr` (Linux).

---

## 13. Build system & protobufs

- **Architectures.** CS:GO client + Linux `srcds` are 32-bit; CS2 (`cs2`) is 64-bit. The
  top-level `CMakeLists.txt` detects `CMAKE_SIZEOF_VOID_P` and routes outputs to per-platform
  dirs (`x64`/`osx64`/`linux64`, etc.). See the README for the exact configure commands.
- **Dependencies** (FetchContent): **protobuf**, **Crypto++** (cryptopp-cmake), **funchook**
  (inline hooking), **diStorm3** (disassembler used by funchook). Steam is linked via the
  vendored `steamworks/` SDK (`steam_api`/`steam_api64`/`libsteam_api`).
- **Protobufs are committed.** `protobufs/` holds both the `.proto` sources
  (`base_gcmessages`, `cstrike15_gcmessages`, `econ_gcmessages`, `engine_gcmessages`,
  `gcsdk_gcmessages`, `gcsystemmsgs`, `steammessages`) **and** the generated `.pb.h/.pb.cc`.
  The build compiles the committed `.pb.cc` (globbed in `csgo_gc/CMakeLists.txt`) and does
  **not** regenerate from `.proto`. *Editing a `.proto` alone has no effect* — regenerate the
  `.pb.*` or hand-edit, which is why some CS2-only messages (e.g. `k_EMsgGCOpenCrate`) are
  parsed by hand off the wire instead.
- **Crash symbolization.** The MSVC build passes `/MAP` and copies `csgo_gc.map` next to the DLL
  (`csgo_gc/CMakeLists.txt`), so an access-violation address in a minidump can be mapped back to
  a function (this is how the map-change crash was pinned to `NetworkingServer::ReceiveMessage`).
- **CI** (`.github/workflows/build.yml`) builds ubuntu/macos/windows. Windows uses **Ninja +
  sccache** (the VS generator ignores the compiler-launcher needed for sccache); macOS builds
  `x86_64` with `FUNCHOOK_CPU=x86`.

---

## 14. Scope & limitations

- **Skins apply only on csgo_gc servers** (offline practice, workshop, your own listen/dedicated
  server). Valve matchmaking and community servers run their own GC.
- **Matchmaking is not, and cannot be, supported** — it needs a centralized server.
- **Case opening** creates the item but CS2 shows an error dialog instead of the reveal (§10.5).

---

## 15. File map

| Area | Files |
|---|---|
| Entry / hooks | `main.cpp`, `steam_hook.cpp`, `launcher/` |
| GC core | `gc_shared.*`, `gc_client.*`, `gc_server.*`, `gc_message.*`, `gc_const*.h` |
| Inventory / schema | `inventory.*`, `item_schema.*`, `case_opening.*`, `graffiti.*`, `keyvalue.*`, `config.*`, `game_profile.*`, `random.h` |
| Networking | `networking_client.*`, `networking_server.*`, `networking_shared.h` |
| Platform | `platform.h`, `platform_windows.cpp`, `platform_unix.cpp`, `appid.cpp` |
| Protobuf | `protobufs/*.proto`, `protobufs/*.pb.{h,cc}` |
| Steam SDK | `steamworks/` |
| Build | `CMakeLists.txt`, `csgo_gc/CMakeLists.txt`, `.github/workflows/build.yml` |

---

*This document describes the current implementation for contributors. For install/usage, see the
top-level [README](../README.md).*
