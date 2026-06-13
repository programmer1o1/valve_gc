#include "stdafx.h"
#include "game_profile.h"
#include "config.h"

const GameProfile g_profileCSGO = {
    .mode                   = GameMode::CSGO,
    .appId                  = 4465480,
    .engineModule           = "engine",
    .gameEventManagerVersion = "GAMEEVENTSMANAGER002",
    .engineClientVersion    = "VEngineClient014",
    .requiredAppIdVersion   = 13857,
    .requiredAppIdVersion2  = 13862,
    .pricesheetVersion      = 1680057676,
    .activeTournamentEventId = 20,
};

// CS2 interface strings are not yet confirmed — find them with:
//   strings engine2.dll | grep -iE "GAMEEVENTSMANAGER|VEngineClient|EngineClient"
// Once found, fill them in here.
const GameProfile g_profileCS2 = {
    .mode                   = GameMode::CS2,
    .appId                  = 730,
    .engineModule           = "engine2",
    .gameEventManagerVersion = nullptr, // TODO: find from engine2.dll
    .engineClientVersion    = nullptr, // TODO: find from engine2.dll
    .requiredAppIdVersion   = 2000244, // CS2 client hello version
    .requiredAppIdVersion2  = 0,
    .pricesheetVersion      = 0,
    .activeTournamentEventId = 0,
};

const GameProfile &GetGameProfile()
{
    static const GameProfile *profile = nullptr;
    if (!profile)
    {
        // lazy init so GetConfig() is ready
        std::string_view game = GetConfig().Game();
        profile = (game == "cs2") ? &g_profileCS2 : &g_profileCSGO;
        Platform::Print("GameProfile: using %s profile\n", game == "cs2" ? "CS2" : "CS:GO");
    }
    return *profile;
}
