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

// Matches csgo_gc/inventory.cpp's InventoryFilePath convention: the dylib
// lives at tf2_gc/<platform>/tf2_gc.<ext>, so "../../tf2_gc/..." resolves to
// the tf2_gc/ directory next to it. Deploy examples/tf2_items_game.txt and
// examples/tf2_unusual_inventory.txt there as tf2_items_game.txt/tf2_inventory.txt.
static constexpr const char *SchemaFilePath = "../../tf2_gc/tf2_items_game.txt";
static constexpr const char *InventoryFilePath = "../../tf2_gc/tf2_inventory.txt";

static void BuildEconItem(const InventoryEntryTF2 &entry, uint64_t itemId, uint32_t accountId, CSOEconItem &item)
{
    item.set_id(itemId);
    item.set_account_id(accountId);
    item.set_inventory(static_cast<uint32_t>(itemId));
    item.set_def_index(entry.defIndex);
    item.set_quantity(1);
    item.set_level(1);
    item.set_quality(entry.quality);
    item.set_flags(0);
    item.set_origin(0); // no origin history for a locally-injected backpack

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
    m_schemaLoaded = m_schema->ParseFromFile(SchemaFilePath) &&
        m_inventory->ParseFromFile(InventoryFilePath, *m_schema);

    if (!m_schemaLoaded)
    {
        Platform::Print("ClientGCTF2: failed to load \"%s\" / \"%s\", backpack will be empty\n",
            SchemaFilePath, InventoryFilePath);
    }
    else
    {
        Platform::Print("ClientGCTF2: loaded %zu backpack entries from %s items / %s particles\n",
            m_inventory->Entries().size(), SchemaFilePath, InventoryFilePath);
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

void ClientGCTF2::BuildBackpackSOCache(CMsgSOCacheSubscribed &message)
{
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
