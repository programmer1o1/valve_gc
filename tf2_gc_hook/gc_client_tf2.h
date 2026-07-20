#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gc_shared.h"

class GCMessageRead;
class ItemSchemaTF2;
class InventoryTF2;
class CMsgSOCacheSubscribed;
class CMsgSOMultipleObjects;
class CSOEconItem;

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
// Confirmed working against a live TF2 client (backpack display + equipping).
//
// Scope: backpack display + equipping. No crafting, trading, case opening
// (that's k_EMsgGCUnlockCrate's ancestor message, not ported here), or
// in-match cosmetic visibility to other players/servers
// (NetworkingClientTF2/NetworkingServerTF2 are no-ops for that reason, so
// equips only apply for this local backpack view, not in an actual game).
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
    void OnAdjustItemEquippedState(GCMessageRead &messageRead);
    void BuildBackpackSOCache(CMsgSOCacheSubscribed &message);

    // Mirrors csgo_gc/inventory.cpp's Inventory::EquipItem/UnequipItem, minus
    // the "default item" (base weapon with no real CSOEconItem) handling our
    // TF2 test inventory doesn't need. Returns true if anything changed.
    bool EquipItem(uint64_t itemId, uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update);
    void UnequipItem(uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update);
    void AddToMultipleObjects(CMsgSOMultipleObjects &message, uint32_t typeId, const CSOEconItem &item);

    void SendMessageToGame(uint32_t type, const google::protobuf::MessageLite &message);

    const uint64_t m_steamId;
    std::unique_ptr<ItemSchemaTF2> m_schema;
    std::unique_ptr<InventoryTF2> m_inventory;
    bool m_schemaLoaded{ false };

    // Live, mutable mirror of the backpack (itemId -> CSOEconItem), built
    // once at startup from m_inventory's entries. BuildBackpackSOCache reads
    // from here (not m_inventory directly) so equip-state changes persist
    // across SO cache resends within the session.
    std::unordered_map<uint64_t, CSOEconItem> m_liveItems;
};
