#pragma once

#include "gc_const_csgo.h"

enum class GameMode
{
    CSGO,
    CS2,
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
    // CS2 values: run `strings engine2.dll | grep -i "manager\|engineclient"` to find them
    const char *gameEventManagerVersion;
    const char *engineClientVersion;

    // Values sent in MatchmakingGC2ClientHello global_stats
    uint32_t requiredAppIdVersion;   // cs:go build number the GC expects
    uint32_t requiredAppIdVersion2;  // secondary build number (cs:go s2 / cs2)
    uint32_t pricesheetVersion;
    uint32_t activeTournamentEventId;
};

// Returns the active profile based on config "game" key
const GameProfile &GetGameProfile();

// Known profiles
extern const GameProfile g_profileCSGO;
extern const GameProfile g_profileCS2;
