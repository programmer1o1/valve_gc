#pragma once

#include "gc_const_csgo.h"

enum class GameMode
{
    CSGO,
    CS2,
    TF2,
};

struct GameProfile
{
    GameMode mode;

    // Steam app ID used for the game
    uint32_t appId;

    // Engine module name passed to Platform::ModuleFactory
    // CS:GO: "engine"   CS2: "engine2"
    const char *engineModule;

    // Interface version strings queried from the engine factory
    const char *gameEventManagerVersion;
    const char *engineClientVersion; // nullptr if vtable layout is incompatible

    // Values sent in MatchmakingGC2ClientHello global_stats
    uint32_t requiredAppIdVersion;   // network_protocol / build number
    uint32_t requiredAppIdVersion2;  // secondary build number
    uint32_t pricesheetVersion;
    uint32_t activeTournamentEventId;

    // CS2: local player is accessed via memory offsets rather than IVEngineClient vtable.
    // Source: a2x/cs2-dumper offsets.rs
    // Usage: *(client.dll + localPlayerControllerOffset) -> CCSPlayerController*
    //        then read userId from the controller struct.
    // 0 = not applicable (CS:GO uses IVEngineClient::GetLocalPlayer instead)
    uintptr_t localPlayerControllerOffset; // dwLocalPlayerController in client.dll
    uintptr_t networkGameClientOffset;     // dwNetworkGameClient in engine2.dll
    uintptr_t networkGameClientLocalPlayer; // offset within INetworkGameClient -> local player
};

// Returns the active profile based on config "game" key
const GameProfile &GetGameProfile();

// Known profiles
extern const GameProfile g_profileCSGO;
extern const GameProfile g_profileCS2;
extern const GameProfile g_profileTF2;
