#include "item_schema.h"

#include "keyvalue.h"

bool ItemSchemaTF2::ParseFromFile(const char *path)
{
    // ParseFromFile parses the file's contents as this KeyValue's subkeys, so
    // the "items_game" key written in the file shows up as a child of root.
    KeyValue root{ "root" };
    if (!root.ParseFromFile(path))
    {
        return false;
    }

    const KeyValue *itemsGame = root.GetSubkey("items_game");
    if (!itemsGame)
    {
        return false;
    }

    if (const KeyValue *items = itemsGame->GetSubkey("items"))
    {
        ParseItems(*items);
    }

    if (const KeyValue *particles = itemsGame->GetSubkey("attribute_controlled_attached_particles"))
    {
        ParseParticles(*particles);
    }

    return !m_items.empty();
}

void ItemSchemaTF2::ParseItems(const KeyValue &itemsKey)
{
    for (const KeyValue &itemKey : itemsKey)
    {
        ItemInfo info;
        info.defIndex = FromString<uint32_t>(itemKey.Name());
        info.name = itemKey.GetString("name");
        info.itemSlot = itemKey.GetString("item_slot");
        info.canBeUnusual = itemKey.GetNumber<uint32_t>("can_be_unusual") != 0;

        m_itemNameToDefIndex[info.name] = info.defIndex;
        m_items.emplace(info.defIndex, std::move(info));
    }
}

void ItemSchemaTF2::ParseParticles(const KeyValue &particlesKey)
{
    for (const KeyValue &particleKey : particlesKey)
    {
        ParticleInfo info;
        info.id = FromString<uint32_t>(particleKey.Name());
        info.name = particleKey.String();

        m_particleNameToId[info.name] = info.id;
        m_particles.emplace(info.id, std::move(info));
    }
}

const ItemSchemaTF2::ItemInfo *ItemSchemaTF2::ItemInfoByName(std::string_view name) const
{
    auto it = m_itemNameToDefIndex.find(std::string{ name });
    if (it == m_itemNameToDefIndex.end())
    {
        return nullptr;
    }

    return ItemInfoByDefIndex(it->second);
}

const ItemSchemaTF2::ItemInfo *ItemSchemaTF2::ItemInfoByDefIndex(uint32_t defIndex) const
{
    auto it = m_items.find(defIndex);
    return it != m_items.end() ? &it->second : nullptr;
}

const ItemSchemaTF2::ParticleInfo *ItemSchemaTF2::ParticleInfoByName(std::string_view name) const
{
    auto it = m_particleNameToId.find(std::string{ name });
    if (it == m_particleNameToId.end())
    {
        return nullptr;
    }

    return ParticleInfoById(it->second);
}

const ItemSchemaTF2::ParticleInfo *ItemSchemaTF2::ParticleInfoById(uint32_t id) const
{
    auto it = m_particles.find(id);
    return it != m_particles.end() ? &it->second : nullptr;
}
