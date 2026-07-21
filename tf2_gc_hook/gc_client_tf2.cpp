#include "stdafx.h" // csgo_gc's PCH: pulls in the generated protobufs plus
                    // the <mutex>/<vector>/<thread>/etc gc_shared.h relies on
#include <cstring>
#include <ctime>

#include "gc_client_tf2.h"
#include "gc_const_tf2.h"

#include "gc_message.h"
#include "platform.h"

#include "../tf2_gc/item_schema.h"
#include "../tf2_gc/inventory.h"

// Same "try both CWD conventions" fallback as config.cpp's ConfigFilePath/
// ConfigFilePathAlt: confirmed via a real TF2 test that our own InstallGC()
// runs with CWD = the game's install root (tf.exe/tf_win64.exe live there
// directly, and our stub never chdirs -- the real launcher.dll does that
// later, after we've already run), so the un-prefixed path is what actually
// resolves; the "../../" variant is kept in case that ever isn't true for
// some other launch path (matches config.cpp's own reasoning).
static constexpr const char *SchemaFilePaths[] = { "tf2_gc/tf2_items_game.txt", "../../tf2_gc/tf2_items_game.txt" };
static constexpr const char *InventoryFilePaths[] = { "tf2_gc/tf2_inventory.txt", "../../tf2_gc/tf2_inventory.txt" };

static bool ParseFromFileWithFallback(ItemSchemaTF2 &schema, const char *const (&paths)[2], const char *&usedPath)
{
    for (const char *path : paths)
    {
        if (schema.ParseFromFile(path))
        {
            usedPath = path;
            return true;
        }
    }
    return false;
}

static bool ParseFromFileWithFallback(InventoryTF2 &inventory, const ItemSchemaTF2 &schema,
    const char *const (&paths)[2], const char *&usedPath)
{
    for (const char *path : paths)
    {
        if (inventory.ParseFromFile(path, schema))
        {
            usedPath = path;
            return true;
        }
    }
    return false;
}

// Not in the committed generated protobuf headers' enum (base_gcmessages.proto
// declares the CMsgRequestInventoryRefresh message but not its wire id), so
// define it here -- same pattern csgo_gc/gc_client.cpp uses for
// k_EMsgGCOpenCrate. Confirmed via IDA: TF2's client.dll sends this
// (GCSDK::CProtoBufMsg<CMsgRequestInventoryRefresh>, wire value 1050) from its
// backpack UI's "no items, refresh" action when it doesn't see a populated
// inventory -- exactly the message our first live test never answered.
static constexpr uint32_t k_EMsgGCRequestInventoryRefresh = 1050;

// TF2's real client (source-sdk-2013's CEconItem::DeserializeFromProtoBufItem,
// confirmed via the leaked SDK) completely ignores an item's equipped_state
// array on receipt unless "contains_equipped_state_v2" (a bool at protobuf
// field 19) is also true -- and CEconItem::SerializeToProtoBufItem shows the
// real GC unconditionally sets this true on every item it sends. This is
// almost certainly why equip changes reached the client (confirmed via
// gc_log: k_EMsgGCAdjustItemEquippedState received, our k_ESOMsg_UpdateMultiple
// reply retrieved with no errors) but never visibly applied: the client
// silently discarded the equipped_state we sent because we never set this
// flag at all.
//
// Our checked-in base_gcmessages.proto is shared with CS:GO/CS2, whose real
// wire format uses field 19 for something unrelated ("rarity"), so we can't
// add contains_equipped_state_v2 as a normal generated field without either
// breaking CS:GO or forking+regenerating the whole shared protobuf (which
// needs the exact pinned protoc version, see docs/tf2_live_hook.md). Instead,
// append the field by hand: protobuf tolerates extra trailing bytes appended
// to an otherwise-valid serialized message, and we never populate `rarity`
// for TF2 items, so there's no collision. Field 19, wire type 0 (varint):
// tag = (19 << 3) | 0 = 152, varint-encoded as 0x98 0x01, followed by the
// value byte 0x01 for "true".
static std::string SerializeEconItemForWire(const CSOEconItem &item)
{
    std::string data = item.SerializeAsString();
    data.push_back(static_cast<char>(0x98));
    data.push_back(static_cast<char>(0x01));
    data.push_back(static_cast<char>(0x01));
    return data;
}

static void BuildEconItem(const InventoryEntryTF2 &entry, uint64_t itemId, uint32_t accountId, CSOEconItem &item)
{
    item.set_id(itemId);
    item.set_account_id(accountId);
    item.set_inventory(static_cast<uint32_t>(itemId));
    item.set_def_index(entry.defIndex);
    item.set_quantity(1);
    // CSOEconItem.level must fall within the item's own real [min_ilevel,
    // max_ilevel] bounds or the client silently rejects it as invalid --
    // confirmed by a live test where equipping any of ~140 weapons that
    // require an exact non-1 level (e.g. Force-a-Nature needs exactly 10,
    // Sandman needs exactly 15) just fell back to the class's default
    // weapon instead. entry.itemLevel carries the correct per-item value
    // (schema-driven, see item_schema.h/inventory.cpp), already accounting
    // for Unusual's separate always-level-10 rule.
    item.set_level(entry.itemLevel);
    item.set_quality(entry.quality);
    item.set_flags(0);
    item.set_origin(ItemOriginBaseItemTF2);

    if (entry.hasParticle)
    {
        CSOEconItemAttribute *attr = item.add_attribute();
        attr->set_def_index(AttributeParticleEffectTF2);

        // Real clients read typed attribute values from value_bytes (a raw
        // little-endian buffer), not the legacy plain-uint32 value field --
        // confirmed by csgo_gc/item_schema.cpp's SetAttributeFloat, which is
        // the same convention already verified working against a real
        // client for CS:GO's paint kit attributes. "Attach particle effect"
        // is a float-typed attribute (the effect index stored as a float),
        // so encode it the same way.
        float particleValue = static_cast<float>(entry.particleId);
        attr->set_value_bytes(&particleValue, sizeof(particleValue));
    }

    if (entry.isAustralium)
    {
        // Confirmed via the actual leaked Valve client source
        // (ValveSoftware/source-sdk-2013, econ_item_view.cpp's
        // CEconItemView::GetSkin): it only honors this style field at all if
        // GetStaticData()->GetNumStyles() is nonzero for the item's OWN
        // defindex -- otherwise it falls through to a plain per-team/default
        // skin and the style value is silently ignored. Only the
        // "Upgradeable ..." /paintkit-base defindexes declare a styles table
        // in the real schema, so entry.defIndex here must already be one of
        // those (tf2_items_game.txt's AUSTRALIUM_ITEMS), not the plain
        // weapon's defindex -- see inventory.cpp's HasAustraliumPrefix.
        item.set_style(StyleAustraliumGoldTF2);

        CSOEconItemAttribute *attr = item.add_attribute();
        attr->set_def_index(AttributeIsAustraliumItemTF2);

        // "is_australium_item" is stored_as_integer in the real schema (per
        // its attributes block), unlike the particle-effect float above --
        // encode the raw int32 bytes, not a float bit-pattern.
        int32_t isAustralium = 1;
        attr->set_value_bytes(&isAustralium, sizeof(isAustralium));
    }
}

ClientGCTF2::ClientGCTF2(uint64_t steamId)
    : m_steamId{ steamId }
    , m_schema{ std::make_unique<ItemSchemaTF2>() }
    , m_inventory{ std::make_unique<InventoryTF2>() }
{
    const char *schemaPath = "(not found)";
    const char *inventoryPath = "(not found)";

    m_schemaLoaded = ParseFromFileWithFallback(*m_schema, SchemaFilePaths, schemaPath) &&
        ParseFromFileWithFallback(*m_inventory, *m_schema, InventoryFilePaths, inventoryPath);

    if (!m_schemaLoaded)
    {
        Platform::Print("ClientGCTF2: failed to load \"%s\" / \"%s\", backpack will be empty\n",
            schemaPath, inventoryPath);
    }
    else
    {
        Platform::Print("ClientGCTF2: loaded %zu backpack entries from %s items / %s particles\n",
            m_inventory->Entries().size(), schemaPath, inventoryPath);

        // Build the live, mutable item map once here (not per-SO-cache-build)
        // so equip-state changes (see EquipItem) persist across resends.
        uint64_t nextItemId = 1;
        uint32_t accountId = static_cast<uint32_t>(m_steamId & 0xffffffffu);
        for (const InventoryEntryTF2 &entry : m_inventory->Entries())
        {
            for (uint32_t i = 0; i < entry.count; i++)
            {
                uint64_t itemId = nextItemId++;
                BuildEconItem(entry, itemId, accountId, m_liveItems[itemId]);
            }
        }
    }

    StartThread();

    Platform::Print("ClientGCTF2 spawned for user %llu\n", (unsigned long long)steamId);
}

ClientGCTF2::~ClientGCTF2()
{
    StopThread();
    Platform::Print("ClientGCTF2 destroyed\n");
}

void ClientGCTF2::HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer)
{
    switch (type)
    {
    case GCEvent::Message:
        HandleMessage(static_cast<uint32_t>(id), buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::SOCacheRequest:
        HandleSOCacheRequest();
        break;

    default:
        // NetMessage, LocalPlayerRoundMVP, SyncLocalPlayerMusicKitState,
        // ClientSOCacheUnsubscribe, ReloadInventory, RoundEnd: CS:GO-only
        // concepts steam_hook.cpp may still post generically. Ignore.
        break;
    }
}

void ClientGCTF2::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    {
        return;
    }

    if (!messageRead.IsProtobuf())
    {
        // k_EMsgGCDelete is a raw struct message (just a uint64 item id),
        // same as CS:GO's csgo_gc/gc_client.cpp handles it -- not a protobuf.
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCDelete:
            OnDeleteItem(messageRead);
            break;

        default:
            Platform::Print("ClientGCTF2::HandleMessage: unhandled struct message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
        return;
    }

    switch (messageRead.TypeUnmasked())
    {
    case k_EMsgGCClientHello:
    case k_EMsgGCClientHelloR2:
    case k_EMsgGCClientHelloR3:
    case k_EMsgGCClientHelloR4:
        OnClientHello(messageRead);
        break;

    case k_EMsgGCRequestInventoryRefresh:
        OnRequestInventoryRefresh();
        break;

    case k_EMsgGCAdjustItemEquippedState:
        OnAdjustItemEquippedState(messageRead);
        break;

    default:
        Platform::Print("ClientGCTF2::HandleMessage: unhandled protobuf message %s\n",
            MessageName(messageRead.TypeUnmasked()));
        break;
    }
}

void ClientGCTF2::HandleSOCacheRequest()
{
    // Posted by steam_hook.cpp when connecting to a TF2 game server (including
    // our own listen server on a map/round start) so the server's ServerGCTF2
    // can relay our equipped items to the game server's client.dll -- without
    // this, the server never learns our loadout and the game falls back to
    // default items (see docs/tf2_live_hook.md, the "lost connection to the
    // item server" popup). Only equipped items are sent, matching
    // ServerGCTF2::RemoveUnequippedItemsTF2's expectations.
    CMsgSOCacheSubscribed message;
    BuildBackpackSOCache(message, /*equippedOnly=*/true);

    int itemCount = message.objects_size() ? message.objects(0).object_data_size() : 0;
    Platform::Print("ClientGCTF2: HandleSOCacheRequest, sending %d equipped items to game server\n", itemCount);

    GCMessageWrite messageWrite{ k_ESOMsg_CacheSubscribed, message };
    PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
}

void ClientGCTF2::OnRequestInventoryRefresh()
{
    CMsgSOCacheSubscribed message;
    BuildBackpackSOCache(message);

    int itemCount = message.objects_size() ? message.objects(0).object_data_size() : 0;
    Platform::Print("ClientGCTF2: OnRequestInventoryRefresh, resending %d backpack items\n", itemCount);

    SendMessageToGame(/*sendToGameServer=*/false, k_ESOMsg_CacheSubscribed, message);
}

void ClientGCTF2::AddToMultipleObjects(CMsgSOMultipleObjects &message, uint32_t typeId, const CSOEconItem &item)
{
    if (!message.has_owner())
    {
        // Same owner/owner_soid double-set as BuildBackpackSOCache -- TF2's
        // real client only reads "owner" (see docs/tf2_live_hook.md).
        // "version" also has to be set here -- csgo_gc/inventory.cpp's
        // AddToMultipleObjects sets this same fixed constant on every
        // CMsgSOMultipleObjects it builds; leaving it unset here is a likely
        // reason equip updates were silently ignored (see gc_const_tf2.h).
        message.set_version(InventoryVersionTF2);
        message.set_owner(m_steamId);
        message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
        message.mutable_owner_soid()->set_id(m_steamId);
    }

    CMsgSOMultipleObjects_SingleObject *single = message.add_objects_modified();
    single->set_type_id(typeId);
    single->set_object_data(SerializeEconItemForWire(item));
}

void ClientGCTF2::UnequipItem(uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update)
{
    for (auto &pair : m_liveItems)
    {
        CSOEconItem &item = pair.second;
        bool modified = false;

        for (auto it = item.mutable_equipped_state()->begin(); it != item.mutable_equipped_state()->end();)
        {
            if (it->new_class() == classId && it->new_slot() == slotId)
            {
                it = item.mutable_equipped_state()->erase(it);
                modified = true;
            }
            else
            {
                ++it;
            }
        }

        if (modified)
        {
            AddToMultipleObjects(update, SOTypeItemTF2, item);
        }
    }
}

bool ClientGCTF2::EquipItem(uint64_t itemId, uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update)
{
    if (slotId == SlotUnequipTF2)
    {
        // unequip this specific item from every slot it's in
        auto it = m_liveItems.find(itemId);
        if (it == m_liveItems.end())
        {
            return false;
        }

        it->second.clear_equipped_state();
        AddToMultipleObjects(update, SOTypeItemTF2, it->second);
        return true;
    }

    // whatever else is currently in this (class, slot) gets unequipped first
    UnequipItem(classId, slotId, update);

    if (itemId == ItemIdInvalidTF2)
    {
        // no item id given -- just unequip, nothing new to equip
        return true;
    }

    auto it = m_liveItems.find(itemId);
    if (it == m_liveItems.end())
    {
        Platform::Print("ClientGCTF2: EquipItem: no such item %llu\n", (unsigned long long)itemId);
        return false;
    }

    CSOEconItem &item = it->second;
    CSOEconItemEquipped *equippedState = item.add_equipped_state();
    equippedState->set_new_class(classId);
    equippedState->set_new_slot(slotId);

    AddToMultipleObjects(update, SOTypeItemTF2, item);
    return true;
}

void ClientGCTF2::OnDeleteItem(GCMessageRead &messageRead)
{
    // Raw struct message: just a uint64 item id, same as csgo_gc/gc_client.cpp's
    // DeleteItem/CS:GO's k_EMsgGCDelete handling -- no protobuf body.
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("ClientGCTF2: parsing CMsgGCDelete failed, ignoring\n");
        return;
    }

    auto it = m_liveItems.find(itemId);
    if (it == m_liveItems.end())
    {
        Platform::Print("ClientGCTF2: delete request for unknown item=%llu, ignoring\n",
            (unsigned long long)itemId);
        return;
    }

    // Mirrors csgo_gc/inventory.cpp's DestroyItem: the destroy notification
    // only needs the item's id, not its full contents.
    CSOEconItem destroyedItem;
    destroyedItem.set_id(itemId);

    CMsgSOSingleObject destroyed;
    destroyed.set_version(InventoryVersionTF2);
    destroyed.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    destroyed.mutable_owner_soid()->set_id(m_steamId);
    destroyed.set_owner(m_steamId);
    destroyed.set_type_id(SOTypeItemTF2);
    destroyed.set_object_data(destroyedItem.SerializeAsString());

    m_liveItems.erase(it);

    Platform::Print("ClientGCTF2: deleted item=%llu\n", (unsigned long long)itemId);

    // Without erasing from m_liveItems above, the item would just come back
    // on the next SO cache rebuild -- that's the "delete doesn't work"
    // symptom. true = also tell the game server, same as every other
    // destroy path (equip/unequip changes the server needs to know about).
    SendMessageToGame(/*sendToGameServer=*/true, k_ESOMsg_Destroy, destroyed);
}

void ClientGCTF2::OnAdjustItemEquippedState(GCMessageRead &messageRead)
{
    CMsgAdjustItemEquippedState message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("ClientGCTF2: parsing CMsgAdjustItemEquippedState failed, ignoring\n");
        return;
    }

    CMsgSOMultipleObjects update;
    if (!EquipItem(message.item_id(), message.new_class(), message.new_slot(), update))
    {
        return;
    }

    Platform::Print("ClientGCTF2: equip item=%llu class=%u slot=%u\n",
        (unsigned long long)message.item_id(), message.new_class(), message.new_slot());

    // Also tell the game server (true), same as csgo_gc/gc_client.cpp's
    // AdjustItemEquippedState -- otherwise the equip change never reaches
    // ServerGCTF2, so it never gets applied on the loadout screen/in-game.
    SendMessageToGame(/*sendToGameServer=*/true, k_ESOMsg_UpdateMultiple, update);
}

void ClientGCTF2::BuildBackpackSOCache(CMsgSOCacheSubscribed &message, bool equippedOnly)
{
    // "owner" is the original GCSDK field (plain steam id); confirmed against
    // the real GCSDK client source (nillerusr/source-engine) that TF2's older
    // client reads this, not "owner_soid" (a newer field CS:GO/CS2 apparently
    // introduced -- our checked-in gcsdk_gcmessages.proto only had that one,
    // so "owner" was never set at all here, meaning the real client attached
    // our items to a bogus owner=0 cache instead of the local player's own).
    // Set both for safety.
    //
    // "version" also has to be set -- csgo_gc/inventory.cpp's
    // BuildCacheSubscription sets this same fixed constant on every
    // CMsgSOCacheSubscribed it builds. Leaving it unset (defaulting to 0)
    // is a likely reason the loadout screen never reflected equip changes:
    // GCSDK's shared object cache tracks a per-cache version and can treat
    // an update that doesn't look newer than what it already has as stale
    // and drop it silently (no parse/validation error either side).
    message.set_version(InventoryVersionTF2);
    message.set_owner(m_steamId);
    message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    message.mutable_owner_soid()->set_id(m_steamId);

    CMsgSOCacheSubscribed_SubscribedType *itemObject = message.add_objects();
    itemObject->set_type_id(SOTypeItemTF2);

    // Serialize from the live map (reflects any equip-state changes),
    // not by rebuilding fresh from m_inventory -- that would silently
    // reset every item to unequipped on every resend.
    for (const auto &pair : m_liveItems)
    {
        if (equippedOnly && !pair.second.equipped_state_size())
        {
            continue;
        }

        itemObject->add_object_data(SerializeEconItemForWire(pair.second));
    }

    // Without this SO object, the real client falls back to whatever default
    // backpack capacity it assumes for the account and starts refusing to
    // place items ("not enough backpack space") once our injected backpack
    // exceeds it. csgo_gc/inventory.cpp sends this same SO type (7, a
    // Valve-wide constant per docs/tf2_live_hook.md, not CS:GO-specific) for
    // the equivalent CS:GO concept; additional_backpack_slots is a generic
    // shared-proto field (protobufs/base_gcmessages.proto), so reuse it here
    // set generously high rather than guessing TF2's exact base slot count.
    CSOEconGameAccountClient accountClient;
    accountClient.set_additional_backpack_slots(1000);

    CMsgSOCacheSubscribed_SubscribedType *accountObject = message.add_objects();
    accountObject->set_type_id(SOTypeGameAccountClientTF2);
    accountObject->add_object_data(accountClient.SerializeAsString());
}

void ClientGCTF2::OnClientHello(GCMessageRead &messageRead)
{
    Platform::Print("ClientGCTF2: OnClientHello type=%u\n", messageRead.TypeUnmasked());

    CMsgClientHello hello;
    if (!messageRead.ReadProtobuf(hello))
    {
        Platform::Print("ClientGCTF2: parsing CMsgClientHello failed, ignoring\n");
        return;
    }

    CMsgClientWelcome welcome;
    welcome.set_version(0);
    welcome.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));

    BuildBackpackSOCache(*welcome.add_outofdate_subscribed_caches());

    int itemCount = welcome.outofdate_subscribed_caches(0).objects(0).object_data_size();
    Platform::Print("ClientGCTF2: sending welcome with %d backpack items\n", itemCount);

    SendMessageToGame(/*sendToGameServer=*/false, k_EMsgGCClientWelcome, welcome);
}

void ClientGCTF2::SendMessageToGame(bool sendToGameServer, uint32_t type, const google::protobuf::MessageLite &message)
{
    GCMessageWrite messageWrite{ type, message };

    if (sendToGameServer)
    {
        PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
    }

    PostToHost(HostEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}
