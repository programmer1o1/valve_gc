#pragma once

#include "gc_shared.h"

// Minimal stand-in for csgo_gc/gc_server.h's ServerGC. steam_hook.cpp
// unconditionally instantiates a "server GC" when hooking a listen/dedicated
// server, so this type has to exist -- but relaying backpack contents to a
// TF2 game server (so other players see your cosmetics) is out of scope for
// this milestone. It answers nothing and drops every event.
class ServerGCTF2 final : public SharedGC
{
public:
    ServerGCTF2() = default;
    ~ServerGCTF2() = default;

    bool RoundMVPMusicKitCountForUserId(int, int &) const { return false; }

private:
    void HandleEvent(GCEvent, uint64_t, const std::vector<uint8_t> &) override {}
};
