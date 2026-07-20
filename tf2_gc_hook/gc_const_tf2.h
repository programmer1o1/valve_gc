#pragma once

#include <cstdint>

// TF2-specific GC constants. See docs/tf2_live_hook.md for how each of these
// was verified (either against a real items_game.txt/GCSDK source, or against
// a live TF2 client).

// CSOEconItemAttribute def_index for "attach particle effect" (drives
// Unusual hat effects). Value is stored as a float bit-pattern in the
// generic CSOEconItemAttribute.value field, same convention CS:GO uses
// for e.g. paint kit attributes (see ItemSchema::AttributeFloat).
constexpr uint32_t AttributeParticleEffectTF2 = 134;

// Shared object type (type_id field in CMsgSOCacheSubscribed_SubscribedType).
// Economy items are SO type 1 across every Valve GC game (CS:GO, TF2, Dota2).
constexpr uint32_t SOTypeItemTF2 = 1;

enum QualityTF2 : uint32_t
{
    QualityNormalTF2 = 0,
    QualityUnusualTF2 = 3,
    QualityUniqueTF2 = 4,
};

// CSOEconItem origin. csgo_gc/gc_const_csgo.h has the same named constant
// (ItemOriginBaseItem = 22) for CS:GO's equivalent "locally-injected default
// item" case; using 0 (plausibly k_EItemOriginInvalid in Valve's shared
// origin enum) here instead was a candidate reason real-defindex items still
// didn't show up in a live test.
constexpr uint32_t ItemOriginBaseItemTF2 = 22;

// CMsgAdjustItemEquippedState.item_id / .new_slot sentinels for "unequip",
// matching csgo_gc/inventory.cpp's ItemIdInvalid/SlotUneqip constants (same
// generic message/semantics -- confirmed TF2's real client sends this exact
// message via CInventoryManager::UpdateInventoryEquippedState in
// econ_item_inventory.cpp).
constexpr uint64_t ItemIdInvalidTF2 = 0;
constexpr uint32_t SlotUnequipTF2 = 0xffff;
