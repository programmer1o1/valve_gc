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

// CSOEconItemAttribute def_index for "is australium item" (attribute_class
// "is_australium_item", stored_as_integer per the real schema -- confirmed
// in a real items_game.txt's attributes block). Also confirmed against the
// actual leaked Valve client source (ValveSoftware/source-sdk-2013,
// econ_item_interface.cpp / econ_item_description.cpp): this attribute
// alone drives the "Australium " display-name prefix (via localization
// token "ItemNameAustralium") and marketplace listability, but NOT the
// gold skin itself -- see StyleAustraliumGoldTF2 below for that.
constexpr uint32_t AttributeIsAustraliumItemTF2 = 2027;

// CSOEconItem.style value that selects the gold skin variant. Confirmed via
// the real client source's CEconItemView::GetSkin (econ_item_view.cpp):
// it only calls GetStyleSkin(style, team) -- i.e. only honors this field at
// all -- if GetStaticData()->GetNumStyles() is nonzero for the item's OWN
// defindex; otherwise it silently falls through to a plain per-team/default
// skin. Only the "Upgradeable ..." /paintkit-base defindexes declare a
// styles table in the real schema (the plain weapon defindex has none), so
// this only works when entry.defIndex is already one of those -- see
// tf2_items_game.txt's AUSTRALIUM_ITEMS and inventory.cpp's
// HasAustraliumPrefix, which look up a distinct "Australium <Weapon>"
// schema entry pointing at the Upgradeable defindex, not the plain one.
constexpr uint32_t StyleAustraliumGoldTF2 = 1;

// Shared object type (type_id field in CMsgSOCacheSubscribed_SubscribedType).
// Economy items are SO type 1 across every Valve GC game (CS:GO, TF2, Dota2).
constexpr uint32_t SOTypeItemTF2 = 1;

// CSOEconGameAccountClient SO type -- same value as csgo_gc/gc_const_csgo.h's
// SOTypeGameAccountClient, a Valve-wide constant (see docs/tf2_live_hook.md).
// Carries additional_backpack_slots; never sending this object at all left
// the real client assuming whatever default backpack capacity it falls back
// to, which a 300+ item injected backpack can exceed ("not enough space").
constexpr uint32_t SOTypeGameAccountClientTF2 = 7;

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

// CMsgSOCacheSubscribed/CMsgSOMultipleObjects.version -- csgo_gc/inventory.cpp
// sets this same fixed nonzero constant (InventoryVersion) on every SO cache
// message it sends (BuildCacheSubscription, AddToMultipleObjects,
// ToSingleObject). We never set it at all here, which is a likely reason
// equip updates (CMsgSOMultipleObjects) got silently ignored client-side
// even though the message demonstrably arrived (no parse/validation
// errors): GCSDK's shared object cache tracks a version per cache and may
// drop updates that don't look newer than what it already has, and an
// entirely absent/zero version is the least "new" a message can look.
constexpr uint64_t InventoryVersionTF2 = 7523377975160828514;
