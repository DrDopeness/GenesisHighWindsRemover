#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gwh
{
    std::string ToLowerAscii(std::string_view value);
    std::string NormalizeIdentifier(std::string_view value);
    std::string NormalizeMapName(std::string_view value);
    std::string ExtractGeneratedClassLeaf(std::string_view value);

    bool IsAllowedMap(
        std::string_view current_map,
        const std::vector<std::string>& allowed_maps);

    bool MatchesBuffIdentifier(
        std::string_view class_name,
        std::string_view class_path,
        const std::vector<std::string>& exact_names_or_paths,
        const std::vector<std::string>& normalized_name_contains);
}
