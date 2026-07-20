#include "inventory.h"

#include <cstdio>

#include "item_schema.h"
#include "keyvalue.h"

static const char *QualityName(uint32_t quality)
{
    switch (quality)
    {
    case ItemSchemaTF2::QualityNormal: return "Normal";
    case ItemSchemaTF2::QualityGenuine: return "Genuine";
    case ItemSchemaTF2::QualityVintage: return "Vintage";
    case ItemSchemaTF2::QualityUnusual: return "Unusual";
    case ItemSchemaTF2::QualityUnique: return "Unique";
    case ItemSchemaTF2::QualityCommunity: return "Community";
    case ItemSchemaTF2::QualityDeveloper: return "Developer";
    case ItemSchemaTF2::QualitySelfmade: return "Self-Made";
    case ItemSchemaTF2::QualityCustomized: return "Customized";
    case ItemSchemaTF2::QualityStrange: return "Strange";
    case ItemSchemaTF2::QualityCompleted: return "Completed";
    case ItemSchemaTF2::QualityHaunted: return "Haunted";
    case ItemSchemaTF2::QualityCollectors: return "Collector's";
    case ItemSchemaTF2::QualityDecoratedWeapon: return "Decorated Weapon";
    default: return "Unknown";
    }
}

// Parses "[particle effect]Item Name" -> (particle effect, Item Name).
// No brackets means a plain (non-Unusual) item.
static bool SplitBracketNotation(std::string_view entryName, std::string_view &particle, std::string_view &itemName)
{
    if (entryName.empty() || entryName[0] != '[')
    {
        particle = {};
        itemName = entryName;
        return false;
    }

    size_t close = entryName.find(']');
    if (close == std::string_view::npos)
    {
        particle = {};
        itemName = entryName;
        return false;
    }

    particle = entryName.substr(1, close - 1);
    itemName = entryName.substr(close + 1);
    return true;
}

bool InventoryTF2::ParseFromFile(const char *path, const ItemSchemaTF2 &schema)
{
    KeyValue root{ "root" };
    if (!root.ParseFromFile(path))
    {
        return false;
    }

    const KeyValue *inventory = root.GetSubkey("inventory");
    if (!inventory)
    {
        return false;
    }

    for (const KeyValue &entryKey : *inventory)
    {
        std::string_view particleName;
        std::string_view itemName;
        bool isUnusual = SplitBracketNotation(entryKey.Name(), particleName, itemName);

        const ItemSchemaTF2::ItemInfo *itemInfo = schema.ItemInfoByName(itemName);
        if (!itemInfo)
        {
            fprintf(stderr, "tf2_gc: unknown item \"%.*s\", skipping\n",
                static_cast<int>(itemName.size()), itemName.data());
            continue;
        }

        InventoryEntryTF2 entry;
        entry.defIndex = itemInfo->defIndex;
        entry.itemName = itemInfo->name;
        entry.count = FromString<uint32_t>(entryKey.String());
        if (entry.count == 0)
        {
            entry.count = 1;
        }

        if (isUnusual)
        {
            const ItemSchemaTF2::ParticleInfo *particleInfo = schema.ParticleInfoByName(particleName);
            if (!particleInfo)
            {
                fprintf(stderr, "tf2_gc: unknown particle effect \"%.*s\" on \"%s\", skipping\n",
                    static_cast<int>(particleName.size()), particleName.data(), entry.itemName.c_str());
                continue;
            }

            if (!itemInfo->canBeUnusual)
            {
                fprintf(stderr, "tf2_gc: \"%s\" is not schema-flagged can_be_unusual, keeping anyway\n",
                    entry.itemName.c_str());
            }

            entry.quality = ItemSchemaTF2::QualityUnusual;
            entry.hasParticle = true;
            entry.particleId = particleInfo->id;
            entry.particleName = particleInfo->name;
        }
        else
        {
            entry.quality = ItemSchemaTF2::QualityUnique;
        }

        entry.qualityName = QualityName(entry.quality);

        m_entries.push_back(std::move(entry));
    }

    return true;
}
