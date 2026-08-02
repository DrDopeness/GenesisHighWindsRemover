#include "../src/Matching.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    const std::vector<std::string> maps{ "Genesis_WP" };
    assert(gwh::IsAllowedMap("Genesis_WP", maps));
    assert(gwh::IsAllowedMap("UEDPIE_0_Genesis_WP", maps));
    assert(gwh::IsAllowedMap("/Game/Genesis/Genesis_WP.Genesis_WP", maps));
    assert(!gwh::IsAllowedMap("Gen2_WP", maps));
    assert(!gwh::IsAllowedMap("TheIsland_WP", maps));

    assert(gwh::ExtractGeneratedClassLeaf(
        "/Game/Genesis/CoreBlueprints/Buffs/AreaBuffs/"
        "Gen_AreaBuff_Arctic.Gen_AreaBuff_Arctic_C") ==
        "Gen_AreaBuff_Arctic_C");
    assert(gwh::ExtractGeneratedClassLeaf("  ArcticBlizzard_High_C  ") ==
        "ArcticBlizzard_High_C");
    assert(gwh::ExtractGeneratedClassLeaf(
        "BlueprintGeneratedClass'/Game/Test/TestBuff.TestBuff_C'") ==
        "TestBuff_C");

    const std::vector<std::string> controller_exact{
        "Gen_AreaBuff_Arctic_C",
        "/Game/Genesis/CoreBlueprints/Buffs/AreaBuffs/"
        "Gen_AreaBuff_Arctic.Gen_AreaBuff_Arctic_C"
    };
    const std::vector<std::string> status_exact{
        "ArcticBlizzard_High_C",
        "/Game/Genesis/CoreBlueprints/Buffs/AreaBuffs/ArcticSubBuffs/"
        "ArcticBlizzard_High.ArcticBlizzard_High_C"
    };
    const std::vector<std::string> contains{ "arcticblizzardhigh", "highwind" };

    assert(gwh::MatchesBuffIdentifier(
        "Gen_AreaBuff_Arctic_C", "ignored", controller_exact, {}));
    assert(gwh::MatchesBuffIdentifier(
        "GEN_AREABUFF_ARCTIC_C", "ignored", controller_exact, {}));
    assert(!gwh::MatchesBuffIdentifier(
        "ArcticBlizzard_High_C", "ignored", controller_exact, {}));

    assert(gwh::MatchesBuffIdentifier(
        "ArcticBlizzard_High_C", "ignored", status_exact, contains));
    assert(gwh::MatchesBuffIdentifier(
        "ARCTICBLIZZARD_HIGH_C", "ignored", status_exact, contains));
    assert(gwh::MatchesBuffIdentifier(
        "Future_High_Winds_Buff_C", "ignored", status_exact, contains));
    assert(gwh::MatchesBuffIdentifier(
        "Unknown_C",
        "/Game/Genesis/CoreBlueprints/Buffs/AreaBuffs/ArcticSubBuffs/"
        "ArcticBlizzard_High.ArcticBlizzard_High_C",
        status_exact,
        contains));

    assert(!gwh::MatchesBuffIdentifier(
        "ArcticBlizzard_Med_C", "ignored", status_exact, contains));
    assert(!gwh::MatchesBuffIdentifier(
        "ArcticBlizzard_Low_C", "ignored", status_exact, contains));
    assert(!gwh::MatchesBuffIdentifier(
        "Buff_SkiffBuffetedByWind_C", "ignored", status_exact, contains));

    assert(gwh::NormalizeIdentifier("ArcticBlizzard_High_C") ==
        "arcticblizzardhighc");

    std::cout << "Matching tests passed\n";
    return 0;
}
