#include "stdafx.h"
#include "game_profile.h"
#include "config.h"

const GameProfile g_profileCSGO = {
    .mode                    = GameMode::CSGO,
    .appId                   = 4465480,
    .engineModule            = "engine",
    .gameEventManagerVersion = "GAMEEVENTSMANAGER002",
    .engineClientVersion     = "VEngineClient014",
    .requiredAppIdVersion    = 13857,
    .requiredAppIdVersion2   = 13862,
    .pricesheetVersion       = 1680057676,
    .activeTournamentEventId = 20,
    .localPlayerControllerOffset = 0,
    .networkGameClientOffset     = 0,
    .networkGameClientLocalPlayer = 0,
};

// Version data sourced from CDemoFileHeader in a real CS2 demo (PBDEMS2):
//   network_protocol = 14165  (field 2)
//   build_num        = 10772  (field 13)
//
// Interface strings sourced from a2x/cs2-dumper (engine2.dll exports).
// CS2 renamed everything — Source 2 dropped the Source 1 naming convention:
//   GAMEEVENTSMANAGER002  ->  GameEventSystemClientV001
//   VEngineClient014      ->  Source2EngineToClient001
//
// NOTE: vtable layouts differ from Source 1. GetLocalPlayer/GetPlayerInfo
// offsets in IVEngineClient do NOT map to Source2EngineToClient001.
// The engine client hook is currently a no-op for CS2 (nullptr skips it).
// Game event registration (AddListener) also needs vtable research for CS2.
const GameProfile g_profileCS2 = {
    .mode                    = GameMode::CS2,
    .appId                   = 730,
    .engineModule            = "engine2",
    .gameEventManagerVersion = "GameEventSystemClientV001",  // vtable-compatible with IGameEventManager2 (verified via hl2sdk-cs2)
    .engineClientVersion     = nullptr, // Source2EngineToClient001 vtable differs; use offset path instead
    .requiredAppIdVersion    = 14165,   // network_protocol from CDemoFileHeader
    .requiredAppIdVersion2   = 10772,   // build_num from CDemoFileHeader
    .pricesheetVersion       = 0,
    .activeTournamentEventId = 0,
    // Source: a2x/cs2-dumper offsets.rs
    .localPlayerControllerOffset  = 0x2320720, // dwLocalPlayerController (client.dll)
    .networkGameClientOffset      = 0x90A1A0,  // dwNetworkGameClient (engine2.dll)
    .networkGameClientLocalPlayer = 0xF8,       // offset within INetworkGameClient
};

// TF2 is Source 1, same vintage as CS:GO's engine build, so this profile
// reuses CS:GO's interface version strings as a best-effort default.
// UNVERIFIED against a real tf/bin engine.dylib/.so/.dll — if hooking fails
// on launch, dump the actual "GAMEEVENTSMANAGER*"/"VEngineClient*" strings
// out of TF2's engine binary and fix these. See docs/tf2_live_hook.md.
// requiredAppIdVersion/pricesheetVersion/activeTournamentEventId and the
// localPlayerController*/networkGameClient* offsets are CS:GO/CS2-only
// fields (matchmaking hello, CS2 memory offsets); unused by ClientGCTF2.
const GameProfile g_profileTF2 = {
    .mode                    = GameMode::TF2,
    .appId                   = 440,
    .engineModule            = "engine",
    .gameEventManagerVersion = "GAMEEVENTSMANAGER002",
    .engineClientVersion     = "VEngineClient014",
    .requiredAppIdVersion    = 0,
    .requiredAppIdVersion2   = 0,
    .pricesheetVersion       = 0,
    .activeTournamentEventId = 0,
    .localPlayerControllerOffset = 0,
    .networkGameClientOffset     = 0,
    .networkGameClientLocalPlayer = 0,
};

const GameProfile &GetGameProfile()
{
    static const GameProfile *profile = nullptr;
    if (!profile)
    {
        // lazy init so GetConfig() is ready
        std::string_view game = GetConfig().Game();
        if (game == "cs2")
        {
            profile = &g_profileCS2;
        }
        else if (game == "tf2")
        {
            profile = &g_profileTF2;
        }
        else
        {
            profile = &g_profileCSGO;
        }

        Platform::Print("GameProfile: using %s profile\n",
            game == "cs2" ? "CS2" : (game == "tf2" ? "TF2" : "CS:GO"));
    }
    return *profile;
}
