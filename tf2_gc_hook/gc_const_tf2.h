#pragma once

#include <cstdint>

// TF2-specific GC constants. Best-effort values ported from publicly
// documented TF2 schema/wire data (TF2 Wiki attribute list, Steam Web API
// ISteamEconomy/GetSchema); NOT verified against a live TF2 client.
// See docs/tf2_live_hook.md.

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
