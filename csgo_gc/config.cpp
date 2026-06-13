#include "stdafx.h"
#include "config.h"
#include "keyvalue.h"
#include "random.h"

constexpr const char *ConfigFilePath = "csgo_gc/config.txt";

const GCConfig &GetConfig()
{
    static GCConfig instance;
    return instance;
}

GCConfig::GCConfig()
{
    KeyValue config{ "config" };

    if (!config.ParseFromFile(ConfigFilePath))
    {
        return;
    }

    m_logOutput = config.GetNumber("log_output", m_logOutput);

    std::string_view game = config.GetString("game");
    if (game == "cs2")
    {
        m_game = "cs2";
        m_appIdOverride = 730; // CS2 default; override below if set explicitly
    }

    m_appIdOverride = config.GetNumber("appid_override", m_appIdOverride);
    m_showCsgoGCServersOnly = config.GetNumber("show_csgo_gc_servers_only", m_showCsgoGCServersOnly);

    const KeyValue *ranks = config.GetSubkey("ranks");
    if (ranks)
    {
        m_competitiveRank = ranks->GetNumber("competitive_rank", m_competitiveRank);
        m_competitiveWins = ranks->GetNumber("competitive_wins", m_competitiveWins);

        m_wingmanRank = ranks->GetNumber("wingman_rank", m_wingmanRank);
        m_wingmanWins = ranks->GetNumber("wingman_wins", m_wingmanWins);

        m_dangerZoneRank = ranks->GetNumber("dangerzone_rank", m_dangerZoneRank);
        m_dangerZoneWins = ranks->GetNumber("dangerzone_wins", m_dangerZoneWins);
    }

    m_destroyUsedItems = config.GetNumber("destroy_used_items", m_destroyUsedItems);
    m_enableMatchDrops = config.GetNumber("enable_match_drops", m_enableMatchDrops);
    m_xpPerRound = config.GetNumber("xp_per_round", m_xpPerRound);
    m_maxServerItems = config.GetNumber("max_server_items", m_maxServerItems);

    const KeyValue *medals = config.GetSubkey("medals");
    if (medals)
    {
        m_featuredMedalDefIndex = medals->GetNumber<uint32_t>("featured", 0);
        for (const KeyValue &entry : *medals)
        {
            if (entry.Name() == "featured")
                continue;
            uint32_t defIndex = FromString<uint32_t>(entry.String());
            if (defIndex)
                m_medalDefIndexes.push_back(defIndex);
        }
    }

    const KeyValue *rarityWeights = config.GetSubkey("rarity_weights");
    if (rarityWeights)
    {
        m_rarityWeights.clear();
        m_rarityWeights.reserve(rarityWeights->SubkeyCount());

        for (const KeyValue &subkey : *rarityWeights)
        {
            RarityWeight weight;
            weight.rarity = FromString<uint32_t>(subkey.Name());
            weight.weight = FromString<float>(subkey.String());
            m_rarityWeights.push_back(weight);
        }
    }

    m_vacBanned = config.GetNumber("vac_banned", m_vacBanned);
    m_commendedFriendly = config.GetNumber("cmd_friendly", m_commendedFriendly);
    m_commendedTeaching = config.GetNumber("cmd_teaching", m_commendedTeaching);
    m_commendedLeader = config.GetNumber("cmd_leader", m_commendedLeader);
    m_level = config.GetNumber("player_level", m_level);
    m_xp = config.GetNumber("player_cur_xp", m_xp);
}

float GCConfig::GetRarityWeight(uint32_t rarity) const
{
    for (const RarityWeight &weight : m_rarityWeights)
    {
        if (weight.rarity == rarity)
        {
            return weight.weight;
        }
    }

    return 0;
}
