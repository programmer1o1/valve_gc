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

// Version data sourced from CDemoFileHeader in a real CS2 demo (PBDEMS2):
//   network_protocol = 14165  (field 2)
//   build_num        = 10772  (field 13)
//
// Interface strings still needed — on the Windows CS2 install run:
//   strings "game\bin\win64\engine2.dll" | grep -iE "GAMEEVENTSMANAGER|EngineClient"
// Then fill in gameEventManagerVersion and engineClientVersion below.
const GameProfile g_profileCS2 = {
    .mode                   = GameMode::CS2,
    .appId                  = 730,
    .engineModule           = "engine2",
    .gameEventManagerVersion = nullptr, // TODO: strings engine2.dll | grep GAMEEVENTSMANAGER
    .engineClientVersion    = nullptr, // TODO: strings engine2.dll | grep EngineClient
    .requiredAppIdVersion   = 14165,   // network_protocol from CDemoFileHeader
    .requiredAppIdVersion2  = 10772,   // build_num from CDemoFileHeader
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
