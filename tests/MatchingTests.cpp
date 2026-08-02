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

    assert(gwh::ExtractGeneratedClassLeaf("  Buff_SkiffBuffetedByWind_C  ") ==
        "Buff_SkiffBuffetedByWind_C");
    assert(gwh::ExtractGeneratedClassLeaf(
        "BlueprintGeneratedClass'/Game/Test/TestBuff.TestBuff_C'") ==
        "TestBuff_C");

    const std::vector<std::string> skiff_exact{
        "Buff_SkiffBuffetedByWind_C"
    };
    const std::vector<std::string> contains{ "buffetedbywind", "highwind" };

    assert(gwh::MatchesBuffIdentifier(
        "Buff_SkiffBuffetedByWind_C", "ignored", skiff_exact, {}));
    assert(gwh::MatchesBuffIdentifier(
        "BUFF_SKIFFBUFFETEDBYWIND_C", "ignored", skiff_exact, {}));
    assert(!gwh::MatchesBuffIdentifier(
        "Buff_Skiff_BuffetedByWind_C", "ignored", skiff_exact, {}));

    assert(gwh::MatchesBuffIdentifier(
        "Buff_SkiffBuffetedByWind_C", "ignored", skiff_exact, contains));
    assert(gwh::MatchesBuffIdentifier(
        "Future_High_Winds_Buff_C", "ignored", skiff_exact, contains));
    assert(gwh::MatchesBuffIdentifier(
        "Unknown_C",
        "/Game/Test/Buff_SkiffBuffetedByWind.Buff_SkiffBuffetedByWind_C",
        skiff_exact,
        contains));

    assert(!gwh::MatchesBuffIdentifier(
        "Gen_AreaBuff_Arctic_C", "ignored", skiff_exact, contains));
    assert(!gwh::MatchesBuffIdentifier(
        "ArcticBlizzard_High_C", "ignored", skiff_exact, contains));
    assert(!gwh::MatchesBuffIdentifier(
        "ArcticBlizzard_Low_C", "ignored", skiff_exact, contains));

    assert(gwh::NormalizeIdentifier("Buff_SkiffBuffetedByWind_C") ==
        "buffskiffbuffetedbywindc");

    std::cout << "Matching tests passed\n";
    return 0;
}
