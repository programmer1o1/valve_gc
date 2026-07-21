#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ItemSchemaTF2;

// A single resolved inventory entry: an item, optionally carrying an
// Unusual particle effect (quality 5's "[effect]item name" bracket
// notation, matching the convention used by examples/unusual_loot_lists.txt).
struct InventoryEntryTF2
{
    uint32_t defIndex{};
    std::string itemName;
    uint32_t quality{};
    const char *qualityName{};

    bool hasParticle{};
    uint32_t particleId{};
    std::string particleName;

    // "Australium ItemName" prefix notation: real Australium weapons aren't
    // a separate defindex, just the base weapon carrying attribute 2027
    // ("is_australium_item") and visual style index 1 (see item_schema.h).
    bool isAustralium{};

    // Copied from the schema's ItemInfo::minLevel. CSOEconItem.level must
    // fall within the item's own [minLevel, maxLevel] or the real client
    // rejects it as invalid and falls back to the class's default weapon
    // for that slot -- many weapons require an exact non-1 level (e.g.
    // Force-a-Nature needs exactly 10), so this can't be a fixed constant.
    uint32_t itemLevel{ 1 };

    uint32_t count{};
};

class InventoryTF2
{
public:
    // Parses a test inventory file (see examples/tf2_unusual_inventory.txt)
    // against the given schema. Unresolvable entries are skipped and logged
    // to stderr rather than aborting the whole load.
    bool ParseFromFile(const char *path, const ItemSchemaTF2 &schema);

    const std::vector<InventoryEntryTF2> &Entries() const { return m_entries; }

private:
    std::vector<InventoryEntryTF2> m_entries;
};
