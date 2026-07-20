#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gc_shared.h"

class GCMessageRead;
class ItemSchemaTF2;
class InventoryTF2;
class CMsgSOCacheSubscribed;

namespace google
{
namespace protobuf
{
class MessageLite;
}
}

// TF2's ClientGC equivalent: answers the generic GCSDK client-hello handshake
// with a CMsgClientWelcome carrying your backpack as a CSOEconItem SO cache,
// same wire protocol csgo_gc already speaks for CS:GO/CS2 (see docs/tf2_live_hook.md).
//
// Scope: only the "see your own backpack locally" milestone. No crafting,
// trading, case opening (that's k_EMsgGCUnlockCrate's ancestor message, not
// ported here), or in-match cosmetic visibility to other players/servers
// (NetworkingClientTF2/NetworkingServerTF2 are no-ops for that reason).
class ClientGCTF2 final : public SharedGC
{
public:
    explicit ClientGCTF2(uint64_t steamId);
    ~ClientGCTF2();

    // steam_hook.cpp calls this generically regardless of which ActiveClientGC
    // is active; TF2 has no music kit MVP concept here, so always 0.
    uint32_t LocalPlayerMusicKitMVPsForRoundMVPEvent() const { return 0; }

private:
    void HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer) override;

    void HandleMessage(uint32_t type, const void *data, uint32_t size);
    void HandleSOCacheRequest();
    void OnClientHello(GCMessageRead &messageRead);
    void OnRequestInventoryRefresh();
    void BuildBackpackSOCache(CMsgSOCacheSubscribed &message);

    void SendMessageToGame(uint32_t type, const google::protobuf::MessageLite &message);

    const uint64_t m_steamId;
    std::unique_ptr<ItemSchemaTF2> m_schema;
    std::unique_ptr<InventoryTF2> m_inventory;
    bool m_schemaLoaded{ false };
};
