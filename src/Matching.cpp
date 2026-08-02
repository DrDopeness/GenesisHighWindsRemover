#include "Matching.h"

#include <algorithm>
#include <cctype>

namespace
{
    std::string TrimAscii(std::string_view value)
    {
        const auto is_space = [](const unsigned char ch) { return std::isspace(ch) != 0; };

        while (!value.empty() && is_space(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);

        while (!value.empty() && is_space(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);

        return std::string(value);
    }

    bool StartsWith(std::string_view value, std::string_view prefix)
    {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }
}

namespace gwh
{
    std::string ToLowerAscii(const std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return result;
    }

    std::string NormalizeIdentifier(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size());

        for (const unsigned char ch : value)
        {
            if (std::isalnum(ch) != 0)
                result.push_back(static_cast<char>(std::tolower(ch)));
        }

        return result;
    }

    std::string NormalizeMapName(const std::string_view value)
    {
        std::string result = ToLowerAscii(TrimAscii(value));

        const auto separator = result.find_last_of("/\\");
        if (separator != std::string::npos)
            result.erase(0, separator + 1);

        const auto object_separator = result.find_last_of('.');
        if (object_separator != std::string::npos)
            result.erase(0, object_separator + 1);

        constexpr std::string_view pie_prefix = "uedpie_";
        if (StartsWith(result, pie_prefix))
        {
            const auto suffix = result.find('_', pie_prefix.size());
            if (suffix != std::string::npos)
            {
                const auto instance = std::string_view(result).substr(
                    pie_prefix.size(), suffix - pie_prefix.size());
                const bool numeric_instance = !instance.empty() &&
                    std::all_of(instance.begin(), instance.end(), [](const unsigned char ch)
                    {
                        return std::isdigit(ch) != 0;
                    });

                if (numeric_instance)
                    result.erase(0, suffix + 1);
            }
        }

        return result;
    }

    std::string ExtractGeneratedClassLeaf(const std::string_view value)
    {
        std::string result = TrimAscii(value);

        while (!result.empty() && (result.back() == '\'' || result.back() == '"'))
            result.pop_back();

        const auto object_separator = result.find_last_of('.');
        if (object_separator != std::string::npos)
            result.erase(0, object_separator + 1);
        else
        {
            const auto path_separator = result.find_last_of("/\\");
            if (path_separator != std::string::npos)
                result.erase(0, path_separator + 1);
        }

        return TrimAscii(result);
    }

    bool IsAllowedMap(
        const std::string_view current_map,
        const std::vector<std::string>& allowed_maps)
    {
        const std::string normalized_current = NormalizeMapName(current_map);
        if (normalized_current.empty())
            return false;

        return std::any_of(allowed_maps.begin(), allowed_maps.end(), [&](const std::string& allowed)
        {
            const std::string normalized_allowed = NormalizeMapName(allowed);
            return !normalized_allowed.empty() && normalized_current == normalized_allowed;
        });
    }

    bool MatchesBuffIdentifier(
        const std::string_view class_name,
        const std::string_view class_path,
        const std::vector<std::string>& exact_names_or_paths,
        const std::vector<std::string>& normalized_name_contains)
    {
        const std::string lower_name = ToLowerAscii(class_name);
        const std::string lower_path = ToLowerAscii(class_path);

        for (const std::string& candidate : exact_names_or_paths)
        {
            const std::string lower_candidate = ToLowerAscii(TrimAscii(candidate));
            if (!lower_candidate.empty() &&
                (lower_name == lower_candidate || lower_path == lower_candidate))
            {
                return true;
            }
        }

        const std::string normalized_name = NormalizeIdentifier(class_name);
        const std::string normalized_path = NormalizeIdentifier(class_path);

        for (const std::string& candidate : normalized_name_contains)
        {
            const std::string normalized_candidate = NormalizeIdentifier(candidate);
            if (!normalized_candidate.empty() &&
                (normalized_name.find(normalized_candidate) != std::string::npos ||
                 normalized_path.find(normalized_candidate) != std::string::npos))
            {
                return true;
            }
        }

        return false;
    }
}
