#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

class KeyValue;

// TF2 item schema: parses a subset of items_game.txt (items, qualities,
// and the unusual "attribute_controlled_attached_particles" effect table).
//
// This is a standalone first milestone: schema parsing + a synthetic test
// inventory (see inventory.h), not wired into the live GC hook yet.
class ItemSchemaTF2
{
public:
    enum Quality
    {
        QualityNormal = 0,
        QualityGenuine = 1,
        QualityVintage = 3,
        QualityUnusual = 5,
        QualityUnique = 6,
        QualityCommunity = 7,
        QualityDeveloper = 8,
        QualitySelfmade = 9,
        QualityCustomized = 10,
        QualityStrange = 11,
        QualityCompleted = 12,
        QualityHaunted = 13,
        QualityCollectors = 14,
        QualityDecoratedWeapon = 15,
    };

    struct ItemInfo
    {
        uint32_t defIndex{};
        std::string name;
        std::string itemSlot;
        bool canBeUnusual{};

        // CSOEconItem.level must fall within [minLevel, maxLevel] or the real
        // client silently rejects the item as invalid (see gc_client_tf2.cpp's
        // BuildEconItem) -- for many weapons these are equal (an exact
        // required level, e.g. Force-a-Nature needs exactly 10), not 1, so
        // this can't be hardcoded generically like the old code assumed.
        // Defaults to 1/1 (matches items_game.txt's own convention of no
        // min_ilevel/max_ilevel meaning "level 1 only") when unset.
        uint32_t minLevel{ 1 };
        uint32_t maxLevel{ 1 };
    };

    struct ParticleInfo
    {
        uint32_t id{};
        std::string name;
    };

    bool ParseFromFile(const char *path);

    const ItemInfo *ItemInfoByName(std::string_view name) const;
    const ItemInfo *ItemInfoByDefIndex(uint32_t defIndex) const;
    const ParticleInfo *ParticleInfoByName(std::string_view name) const;
    const ParticleInfo *ParticleInfoById(uint32_t id) const;

    size_t ItemCount() const { return m_items.size(); }
    size_t ParticleCount() const { return m_particles.size(); }

private:
    void ParseItems(const KeyValue &itemsKey);
    void ParseParticles(const KeyValue &particlesKey);

    std::unordered_map<uint32_t, ItemInfo> m_items;
    std::unordered_map<std::string, uint32_t> m_itemNameToDefIndex;

    std::unordered_map<uint32_t, ParticleInfo> m_particles;
    std::unordered_map<std::string, uint32_t> m_particleNameToId;
};
