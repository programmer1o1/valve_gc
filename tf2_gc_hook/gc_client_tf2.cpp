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

static void BuildEconItem(const InventoryEntryTF2 &entry, uint64_t itemId, uint32_t accountId, CSOEconItem &item)
{
    item.set_id(itemId);
    item.set_account_id(accountId);
    item.set_inventory(static_cast<uint32_t>(itemId));
    item.set_def_index(entry.defIndex);
    item.set_quantity(1);
    // Unusual-quality items are always level 10 in TF2's real schema
    // regardless of the base item (confirmed: Ghastly Gibus's real
    // items_game.txt entry requires min_ilevel/max_ilevel = 10). Hardcoding
    // level 1 for everything was likely making Unusual items fail
    // client-side level-bounds validation. Non-Unusual items default to
    // level 1 here since our minimal test schema doesn't track per-item
    // ilevel bounds; if this fixes Unusuals but not plain items, that's
    // the next place to look.
    item.set_level(entry.hasParticle ? 10 : 1);
    item.set_quality(entry.quality);
    item.set_flags(0);
    item.set_origin(ItemOriginBaseItemTF2);

    if (entry.hasParticle)
    {
        CSOEconItemAttribute *attr = item.add_attribute();
        attr->set_def_index(AttributeParticleEffectTF2);

        // attribute wire format stores floats as their raw bit pattern in
        // the generic uint32 value field, same convention CS:GO's paint kit
        // attributes use (see ItemSchema::SetAttributeFloat).
        float particleValue = static_cast<float>(entry.particleId);
        uint32_t bits;
        memcpy(&bits, &particleValue, sizeof(bits));
        attr->set_value(bits);
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
        Platform::Print("ClientGCTF2::HandleMessage: unhandled struct message %s\n",
            MessageName(messageRead.TypeUnmasked()));
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

    default:
        Platform::Print("ClientGCTF2::HandleMessage: unhandled protobuf message %s\n",
            MessageName(messageRead.TypeUnmasked()));
        break;
    }
}

void ClientGCTF2::HandleSOCacheRequest()
{
    // Posted by steam_hook.cpp when connecting to a TF2 game server so the
    // server can validate/mirror your backpack. NetworkingClientTF2 can't
    // relay it anywhere yet (see its header) so there's nothing useful to do.
    Platform::Print("ClientGCTF2: SOCacheRequest ignored (game-server relay not implemented)\n");
}

void ClientGCTF2::OnRequestInventoryRefresh()
{
    CMsgSOCacheSubscribed message;
    BuildBackpackSOCache(message);

    int itemCount = message.objects_size() ? message.objects(0).object_data_size() : 0;
    Platform::Print("ClientGCTF2: OnRequestInventoryRefresh, resending %d backpack items\n", itemCount);

    SendMessageToGame(k_ESOMsg_CacheSubscribed, message);
}

void ClientGCTF2::BuildBackpackSOCache(CMsgSOCacheSubscribed &message)
{
    // "owner" is the original GCSDK field (plain steam id); confirmed against
    // the real GCSDK client source (nillerusr/source-engine) that TF2's older
    // client reads this, not "owner_soid" (a newer field CS:GO/CS2 apparently
    // introduced -- our checked-in gcsdk_gcmessages.proto only had that one,
    // so "owner" was never set at all here, meaning the real client attached
    // our items to a bogus owner=0 cache instead of the local player's own).
    // Set both for safety.
    message.set_owner(m_steamId);
    message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    message.mutable_owner_soid()->set_id(m_steamId);

    CMsgSOCacheSubscribed_SubscribedType *itemObject = message.add_objects();
    itemObject->set_type_id(SOTypeItemTF2);

    if (!m_schemaLoaded)
    {
        return;
    }

    uint64_t nextItemId = 1;
    uint32_t accountId = static_cast<uint32_t>(m_steamId & 0xffffffffu);

    for (const InventoryEntryTF2 &entry : m_inventory->Entries())
    {
        for (uint32_t i = 0; i < entry.count; i++)
        {
            CSOEconItem item;
            BuildEconItem(entry, nextItemId++, accountId, item);
            itemObject->add_object_data(item.SerializeAsString());
        }
    }
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

    SendMessageToGame(k_EMsgGCClientWelcome, welcome);
}

void ClientGCTF2::SendMessageToGame(uint32_t type, const google::protobuf::MessageLite &message)
{
    GCMessageWrite messageWrite{ type, message };
    PostToHost(HostEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}
