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
        QualityVintage = 2,
        QualityUnusual = 3,
        QualityUnique = 4,
        QualityCommunity = 5,
        QualityDeveloper = 6,
        QualitySelfmade = 7,
        QualityCustomized = 8,
        QualityStrange = 9,
        QualityCompleted = 10,
        QualityHaunted = 11,
        QualityCollectors = 14,
        QualityDecoratedWeapon = 15,
    };

    struct ItemInfo
    {
        uint32_t defIndex{};
        std::string name;
        std::string itemSlot;
        bool canBeUnusual{};
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
