#include <cstdio>

#include "inventory.h"
#include "item_schema.h"

int main(int argc, char **argv)
{
    const char *schemaPath = argc > 1 ? argv[1] : "examples/tf2_items_game.txt";
    const char *inventoryPath = argc > 2 ? argv[2] : "examples/tf2_unusual_inventory.txt";

    ItemSchemaTF2 schema;
    if (!schema.ParseFromFile(schemaPath))
    {
        fprintf(stderr, "tf2_gc: failed to parse schema \"%s\"\n", schemaPath);
        return 1;
    }

    printf("Loaded TF2 schema: %zu items, %zu unusual particle effects\n",
        schema.ItemCount(), schema.ParticleCount());

    InventoryTF2 inventory;
    if (!inventory.ParseFromFile(inventoryPath, schema))
    {
        fprintf(stderr, "tf2_gc: failed to parse inventory \"%s\"\n", inventoryPath);
        return 1;
    }

    printf("\nInventory (%zu entries):\n", inventory.Entries().size());

    for (const InventoryEntryTF2 &entry : inventory.Entries())
    {
        if (entry.hasParticle)
        {
            printf("  x%u  [%s] %s (defindex %u, %s, particle %u)\n",
                entry.count, entry.particleName.c_str(), entry.itemName.c_str(),
                entry.defIndex, entry.qualityName, entry.particleId);
        }
        else
        {
            printf("  x%u  %s (defindex %u, %s)\n",
                entry.count, entry.itemName.c_str(), entry.defIndex, entry.qualityName);
        }
    }

    return 0;
}
