#include "API/ARK/Ark.h"
#include "json.hpp"

#include "Matching.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kPluginName = "GenesisHighWindsRemover";
    constexpr const char* kAddBuffHook = "APrimalBuff.AddBuff(APrimalCharacter*,AActor*)";
    constexpr const char* kBeginPlayHook = "AShooterGameMode.BeginPlay()";

    struct Settings
    {
        bool enabled = true;
        bool players_only = false;
        bool sweep_existing_player_buffs = true;
        bool log_first_blocked_class = true;
        bool log_unmatched_discovery_candidates = false;

        std::vector<std::string> allowed_maps;
        std::vector<std::string> exact_blocked_class_names_or_paths;
        std::vector<std::string> exact_cleanup_class_names_or_paths;
        std::vector<std::string> mounted_only_cleanup_class_names_or_paths;
        std::vector<std::string> normalized_blocked_class_name_contains;
        std::vector<std::string> discovery_normalized_name_contains;

        // Exact identifiers are reduced to their generated-class leaf and converted
        // once. The AddBuff hot path can then compare FNames without allocating.
        std::vector<FName> exact_blocked_class_names;
        std::vector<FName> exact_cleanup_class_names;
        std::vector<FName> mounted_only_cleanup_class_names;
    };

    struct BuffClassInfo
    {
        std::string name;
        std::string path;
    };

    struct SweepResult
    {
        std::uint64_t players = 0;
        std::uint64_t buffs_scanned = 0;
        std::uint64_t buffs_deactivated = 0;
    };

    enum class MapState : int
    {
        Unknown,
        Active,
        Inactive
    };

    std::shared_ptr<const Settings> g_settings;
    std::atomic<MapState> g_map_state{ MapState::Unknown };
    std::atomic<std::uint64_t> g_blocked_total{ 0 };
    std::atomic<std::uint64_t> g_deactivated_total{ 0 };
    std::atomic<bool> g_logged_first_block{ false };
    std::atomic<bool> g_operational{ false };

    bool g_add_buff_hooked = false;
    bool g_begin_play_hooked = false;

    // Pre-resolved UClass pointers for blocked buffs. Compared by pointer
    // in the AddBuff hook so the hot path does zero string work and never
    // touches for_character (which may be partially constructed during
    // zone transitions).
    std::mutex g_class_cache_mutex;
    std::vector<UClass*> g_cached_blocked_classes;

    std::mutex g_state_mutex;
    std::string g_current_map = "<not ready>";

    std::mutex g_discovery_mutex;
    std::unordered_set<std::string> g_logged_discovery_classes;


    Settings DefaultSettings()
    {
        Settings settings;
        settings.allowed_maps = { "Genesis_WP" };
        settings.exact_blocked_class_names_or_paths = {
            "Buff_SkiffBuffetedByWind_C"
        };
        settings.exact_cleanup_class_names_or_paths = {
            "Buff_SkiffBuffetedByWind_C"
        };
        settings.normalized_blocked_class_name_contains = {};
        settings.discovery_normalized_name_contains = {
            "skiffbuffetedbywind", "buffetedbywind", "wind"
        };
        return settings;
    }

    std::string Trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return {};

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::vector<std::string> ReadStringArray(
        const nlohmann::json& root,
        const char* key,
        const std::vector<std::string>& fallback)
    {
        if (!root.contains(key))
            return fallback;

        const auto& value = root.at(key);
        if (!value.is_array())
            throw std::runtime_error(std::string(key) + " must be a JSON array");

        std::vector<std::string> result;
        result.reserve(value.size());
        for (const auto& item : value)
        {
            if (!item.is_string())
                throw std::runtime_error(std::string(key) + " must contain only strings");

            std::string entry = Trim(item.get<std::string>());
            if (!entry.empty())
                result.push_back(std::move(entry));
        }

        return result;
    }

    void BuildFastClassNames(
        const std::vector<std::string>& identifiers,
        std::vector<FName>& class_names)
    {
        class_names.clear();
        class_names.reserve(identifiers.size());

        for (const std::string& identifier : identifiers)
        {
            const std::string leaf = gwh::ExtractGeneratedClassLeaf(identifier);
            if (leaf.empty())
                continue;

            const FName class_name(leaf.c_str());
            bool duplicate = false;
            for (const FName existing : class_names)
            {
                if (existing == class_name)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate)
                class_names.push_back(class_name);
        }
    }

    Settings ParseSettings(const nlohmann::json& root)
    {
        Settings settings = DefaultSettings();

        settings.enabled = root.value("Enabled", settings.enabled);
        settings.players_only = root.value("PlayersOnly", settings.players_only);
        settings.sweep_existing_player_buffs = root.value(
            "SweepExistingPlayerBuffs", settings.sweep_existing_player_buffs);
        settings.log_first_blocked_class = root.value(
            "LogFirstBlockedClass", settings.log_first_blocked_class);
        settings.log_unmatched_discovery_candidates = root.value(
            "LogUnmatchedDiscoveryCandidates", settings.log_unmatched_discovery_candidates);

        settings.allowed_maps = ReadStringArray(
            root, "AllowedMaps", settings.allowed_maps);
        settings.exact_blocked_class_names_or_paths = ReadStringArray(
            root,
            "ExactBlockedClassNamesOrPaths",
            settings.exact_blocked_class_names_or_paths);
        settings.exact_cleanup_class_names_or_paths = ReadStringArray(
            root,
            "ExactCleanupClassNamesOrPaths",
            settings.exact_cleanup_class_names_or_paths);
        settings.mounted_only_cleanup_class_names_or_paths = ReadStringArray(
            root,
            "MountedOnlyCleanupClassNamesOrPaths",
            settings.mounted_only_cleanup_class_names_or_paths);
        settings.normalized_blocked_class_name_contains = ReadStringArray(
            root,
            "NormalizedBlockedClassNameContains",
            settings.normalized_blocked_class_name_contains);
        settings.discovery_normalized_name_contains = ReadStringArray(
            root,
            "DiscoveryNormalizedNameContains",
            settings.discovery_normalized_name_contains);

        if (settings.allowed_maps.empty())
            throw std::runtime_error("AllowedMaps must contain at least one map name");

        if (settings.exact_blocked_class_names_or_paths.empty() &&
            settings.normalized_blocked_class_name_contains.empty() &&
            settings.exact_cleanup_class_names_or_paths.empty() &&
            settings.mounted_only_cleanup_class_names_or_paths.empty())
        {
            throw std::runtime_error(
                "At least one block or cleanup class matcher is required");
        }

        if (settings.exact_cleanup_class_names_or_paths.empty())
            settings.exact_cleanup_class_names_or_paths =
                settings.exact_blocked_class_names_or_paths;

        BuildFastClassNames(
            settings.exact_blocked_class_names_or_paths,
            settings.exact_blocked_class_names);
        BuildFastClassNames(
            settings.exact_cleanup_class_names_or_paths,
            settings.exact_cleanup_class_names);
        BuildFastClassNames(
            settings.mounted_only_cleanup_class_names_or_paths,
            settings.mounted_only_cleanup_class_names);
        return settings;
    }

    std::string GetConfigPath()
    {
        return AsaApi::Tools::GetCurrentDir() +
            "/ArkApi/Plugins/GenesisHighWindsRemover/config.json";
    }

    std::shared_ptr<const Settings> LoadSettingsFromDisk()
    {
        std::ifstream input(GetConfigPath());
        if (!input.is_open())
            throw std::runtime_error("Could not open " + GetConfigPath());

        nlohmann::json root;
        input >> root;

        return std::make_shared<const Settings>(ParseSettings(root));
    }

    std::shared_ptr<const Settings> GetSettings()
    {
        return std::atomic_load_explicit(&g_settings, std::memory_order_acquire);
    }

    BuffClassInfo GetBuffClassInfo(UClass* buff_class)
    {
        BuffClassInfo info;
        if (!buff_class)
            return info;

        info.name = buff_class->NamePrivateField().ToString().ToStringUTF8();
        info.path = buff_class->GetPathName(nullptr).ToStringUTF8();
        return info;
    }

    bool MatchesExactClass(
        UClass* buff_class,
        const std::vector<FName>& configured_names)
    {
        if (!buff_class)
            return false;

        const FName actual_name = buff_class->NamePrivateField();
        for (const FName configured_name : configured_names)
        {
            if (actual_name == configured_name)
                return true;
        }

        return false;
    }

    bool MatchesBlockedBuffClass(UClass* buff_class, const Settings& settings)
    {
        if (MatchesExactClass(buff_class, settings.exact_blocked_class_names))
            return true;

        if (!buff_class || settings.normalized_blocked_class_name_contains.empty())
            return false;

        const BuffClassInfo info = GetBuffClassInfo(buff_class);
        return gwh::MatchesBuffIdentifier(
            info.name,
            info.path,
            {},
            settings.normalized_blocked_class_name_contains);
    }

    bool IsEligibleCharacter(APrimalCharacter* character, const Settings& settings)
    {
        if (!character)
            return false;

        if (!settings.players_only)
            return true;

        UClass* character_class = character->ClassPrivateField();
        return character_class && character_class->IsChildOf(AShooterCharacter::StaticClass());
    }

    std::string ReadMapName(AShooterGameMode* game_mode)
    {
        FString map_name;

        if (game_mode)
            game_mode->GetMapName(&map_name);
        else
        {
            UWorld* world = AsaApi::GetApiUtils().GetWorld();
            if (world)
                world->GetMapName(&map_name);
        }

        return map_name.IsEmpty() ? std::string{} : map_name.ToStringUTF8();
    }

    void RefreshMapState(AShooterGameMode* game_mode, const bool always_log)
    {
        const std::shared_ptr<const Settings> settings = GetSettings();
        const std::string map_name = ReadMapName(game_mode);

        if (map_name.empty() || !settings)
        {
            g_map_state.store(MapState::Unknown, std::memory_order_release);
            std::lock_guard lock(g_state_mutex);
            g_current_map = "<not ready>";
            return;
        }

        const MapState next_state = gwh::IsAllowedMap(map_name, settings->allowed_maps)
            ? MapState::Active
            : MapState::Inactive;
        const MapState previous_state = g_map_state.exchange(next_state, std::memory_order_acq_rel);

        {
            std::lock_guard lock(g_state_mutex);
            g_current_map = map_name;
        }

        if (always_log || previous_state != next_state)
        {
            Log::GetLog()->info(
                "Map '{}' is {} for this plugin",
                map_name,
                next_state == MapState::Active ? "enabled" : "out of scope");
        }
    }

    bool IsActiveMap()
    {
        MapState state = g_map_state.load(std::memory_order_acquire);
        if (state == MapState::Unknown)
        {
            RefreshMapState(nullptr, false);
            state = g_map_state.load(std::memory_order_acquire);
        }

        return state == MapState::Active;
    }

    void MaybeLogDiscoveryCandidate(UClass* buff_class, const Settings& settings)
    {
        if (!settings.log_unmatched_discovery_candidates ||
            settings.discovery_normalized_name_contains.empty() ||
            !buff_class)
        {
            return;
        }

        const BuffClassInfo info = GetBuffClassInfo(buff_class);
        if (!gwh::MatchesBuffIdentifier(
                info.name,
                info.path,
                {},
                settings.discovery_normalized_name_contains))
        {
            return;
        }

        const std::string key = info.path.empty() ? info.name : info.path;
        bool first_observation = false;
        {
            std::lock_guard lock(g_discovery_mutex);
            first_observation = g_logged_discovery_classes.insert(key).second;
        }

        if (first_observation)
        {
            Log::GetLog()->info(
                "Discovery candidate (not blocked): class='{}', path='{}'",
                info.name,
                info.path);
        }
    }

    void LogFirstBlockedClass(UClass* buff_class, const Settings& settings)
    {
        if (!settings.log_first_blocked_class ||
            g_logged_first_block.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        const BuffClassInfo info = GetBuffClassInfo(buff_class);
        Log::GetLog()->info(
            "Blocked TEK Hover Skiff wind debuff: class='{}', path='{}'",
            info.name,
            info.path);
    }

    std::uint64_t DeactivateMatchingBuffsOnCharacter(
        APrimalCharacter* character,
        const std::vector<FName>& cleanup_names)
    {
        if (!character || cleanup_names.empty())
            return 0;

        const auto buffs = character->BuffsField();
        std::vector<APrimalBuff*> matches;
        for (APrimalBuff* buff : buffs)
        {
            if (buff && MatchesExactClass(
                    buff->ClassPrivateField(),
                    cleanup_names))
            {
                matches.push_back(buff);
            }
        }

        for (APrimalBuff* buff : matches)
            buff->Deactivate();

        if (!matches.empty())
        {
            g_deactivated_total.fetch_add(matches.size(), std::memory_order_relaxed);
        }

        return matches.size();
    }

    void RebuildClassCache()
    {
        const std::shared_ptr<const Settings> settings = GetSettings();
        if (!settings)
            return;

        std::vector<UClass*> resolved;
        for (const FName& class_name : settings->exact_blocked_class_names)
        {
            // Walk all UClass objects to find a match by FName.
            // This runs only on BeginPlay / Reload, never in the hot path.
            UWorld* world = AsaApi::GetApiUtils().GetWorld();
            if (!world)
                break;

            const auto& controllers = world->PlayerControllerListField();
            for (TWeakObjectPtr<APlayerController> weak : controllers)
            {
                APlayerController* ctrl = weak.Get();
                if (!ctrl)
                    continue;

                UClass* ctrl_class = ctrl->ClassPrivateField();
                if (!ctrl_class || !ctrl_class->IsChildOf(
                        AShooterPlayerController::StaticClass()))
                    continue;

                auto* sc = static_cast<AShooterPlayerController*>(ctrl);
                AShooterCharacter* ch = sc->GetPlayerCharacter();
                if (!ch)
                    continue;

                const auto buffs = ch->BuffsField();
                for (APrimalBuff* buff : buffs)
                {
                    if (!buff)
                        continue;
                    UClass* bc = buff->ClassPrivateField();
                    if (bc && bc->NamePrivateField() == class_name)
                    {
                        bool already = false;
                        for (UClass* r : resolved)
                            if (r == bc) { already = true; break; }
                        if (!already)
                            resolved.push_back(bc);
                    }
                }

                // Also check the possessed pawn (skiff etc.)
                APawn* pawn = ctrl->GetPawnOrSpectator();
                if (pawn && pawn != ch)
                {
                    UClass* pc = pawn->ClassPrivateField();
                    if (pc && pc->IsChildOf(APrimalCharacter::StaticClass()))
                    {
                        auto* mount = static_cast<APrimalCharacter*>(pawn);
                        const auto mount_buffs = mount->BuffsField();
                        for (APrimalBuff* buff : mount_buffs)
                        {
                            if (!buff)
                                continue;
                            UClass* bc = buff->ClassPrivateField();
                            if (bc && bc->NamePrivateField() == class_name)
                            {
                                bool already = false;
                                for (UClass* r : resolved)
                                    if (r == bc) { already = true; break; }
                                if (!already)
                                    resolved.push_back(bc);
                            }
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_class_cache_mutex);
            g_cached_blocked_classes = std::move(resolved);
        }

        Log::GetLog()->info("Class cache: resolved {} blocked UClass pointer(s)",
            g_cached_blocked_classes.size());
    }

    SweepResult SweepExistingPlayerBuffs()
    {
        SweepResult result;
        const std::shared_ptr<const Settings> settings = GetSettings();
        if (!settings || !settings->enabled || !settings->sweep_existing_player_buffs ||
            !IsActiveMap())
        {
            return result;
        }

        UWorld* world = AsaApi::GetApiUtils().GetWorld();
        if (!world)
            return result;

        const auto& controllers = world->PlayerControllerListField();
        for (TWeakObjectPtr<APlayerController> weak_controller : controllers)
        {
            APlayerController* base_controller = weak_controller.Get();
            if (!base_controller || !base_controller->ClassPrivateField() ||
                !base_controller->ClassPrivateField()->IsChildOf(
                    AShooterPlayerController::StaticClass()))
            {
                continue;
            }

            auto* controller = static_cast<AShooterPlayerController*>(base_controller);

            AShooterCharacter* character = controller->GetPlayerCharacter();
            if (!character)
                continue;

            ++result.players;

            APawn* possessed = base_controller->GetPawnOrSpectator();
            APrimalDinoCharacter* mount = character->MountedDinoField().Get();
            const bool is_riding = (possessed && possessed != character) || mount;

            const auto buffs = character->BuffsField();
            for (APrimalBuff* buff : buffs)
            {
                if (!buff)
                    continue;
                ++result.buffs_scanned;
            }

            result.buffs_deactivated += DeactivateMatchingBuffsOnCharacter(
                character, settings->exact_cleanup_class_names);
            if (is_riding)
            {
                result.buffs_deactivated += DeactivateMatchingBuffsOnCharacter(
                    character, settings->mounted_only_cleanup_class_names);
            }
        }

        if (result.buffs_deactivated > 0)
        {
            g_deactivated_total.fetch_add(result.buffs_deactivated, std::memory_order_relaxed);
            Log::GetLog()->info(
                "Deactivated {} existing high-winds buff instance(s) across {} online player(s)",
                result.buffs_deactivated,
                result.players);
        }

        return result;
    }

    void SendRconReply(
        RCONClientConnection* connection,
        RCONPacket* packet,
        const std::string& message)
    {
        if (!connection || !packet)
            return;

        FString reply = FString::FromStringUTF8(message);
        connection->SendMessageW(packet->Id, 0, &reply);
    }

    void ReloadRcon(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
    {
        try
        {
            const std::shared_ptr<const Settings> loaded = LoadSettingsFromDisk();
            std::atomic_store_explicit(&g_settings, loaded, std::memory_order_release);

            g_logged_first_block.store(false, std::memory_order_release);
            {
                std::lock_guard lock(g_discovery_mutex);
                g_logged_discovery_classes.clear();
            }

            g_map_state.store(MapState::Unknown, std::memory_order_release);
            RefreshMapState(nullptr, true);
            RebuildClassCache();
            const SweepResult sweep = SweepExistingPlayerBuffs();

            std::ostringstream response;
            response << "Reloaded GenesisHighWindsRemover config; deactivated "
                     << sweep.buffs_deactivated << " existing instance(s).";
            SendRconReply(connection, packet, response.str());
        }
        catch (const std::exception& error)
        {
            Log::GetLog()->error("Config reload failed: {}", error.what());
            SendRconReply(connection, packet, std::string("Reload failed: ") + error.what());
        }
    }

    void StatusRcon(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
    {
        const std::shared_ptr<const Settings> settings = GetSettings();
        std::string map_name;
        {
            std::lock_guard lock(g_state_mutex);
            map_name = g_current_map;
        }

        const MapState state = g_map_state.load(std::memory_order_acquire);
        std::ostringstream response;
        response << "GenesisHighWindsRemover: enabled="
                 << (settings && settings->enabled ? "true" : "false")
                 << ", operational="
                 << (g_operational.load(std::memory_order_acquire) ? "true" : "false")
                 << ", map=" << map_name
                 << ", mapActive=" << (state == MapState::Active ? "true" : "false")
                 << ", blockedSinceLoad=" << g_blocked_total.load(std::memory_order_relaxed)
                 << ", deactivatedSinceLoad=" << g_deactivated_total.load(std::memory_order_relaxed)
                 << ", exactBlockedTargets="
                 << (settings ? settings->exact_blocked_class_names.size() : 0)
                 << ", exactCleanupTargets="
                 << (settings ? settings->exact_cleanup_class_names.size() : 0)
                 << ", mountedOnlyCleanupTargets="
                 << (settings ? settings->mounted_only_cleanup_class_names.size() : 0)
                 << ", AsaApi=" << AsaApi::Tools::GetApiVersion();

        SendRconReply(connection, packet, response.str());
    }

    void DumpBuffsRcon(RCONClientConnection* connection, RCONPacket* packet, UWorld*)
    {
        UWorld* world = AsaApi::GetApiUtils().GetWorld();
        if (!world)
        {
            SendRconReply(connection, packet, "World is not ready.");
            return;
        }

        std::uint64_t players = 0;
        std::uint64_t instances = 0;
        std::unordered_set<std::string> unique_classes;

        const auto& controllers = world->PlayerControllerListField();
        for (TWeakObjectPtr<APlayerController> weak_controller : controllers)
        {
            APlayerController* base_controller = weak_controller.Get();
            if (!base_controller || !base_controller->ClassPrivateField() ||
                !base_controller->ClassPrivateField()->IsChildOf(
                    AShooterPlayerController::StaticClass()))
            {
                continue;
            }

            auto* controller = static_cast<AShooterPlayerController*>(base_controller);

            AShooterCharacter* character = controller->GetPlayerCharacter();
            if (!character)
                continue;

            ++players;
            const auto buffs = character->BuffsField();
            for (APrimalBuff* buff : buffs)
            {
                if (!buff)
                    continue;

                ++instances;
                const BuffClassInfo info = GetBuffClassInfo(buff->ClassPrivateField());
                const std::string key = info.path.empty() ? info.name : info.path;
                if (unique_classes.insert(key).second)
                {
                    Log::GetLog()->info(
                        "Active player buff: class='{}', path='{}'",
                        info.name,
                        info.path);
                }
            }
        }

        std::ostringstream response;
        response << "Logged " << unique_classes.size() << " unique active buff class(es) from "
                 << instances << " instance(s) across " << players
                 << " online player(s). See the ArkApi log or server console.";
        SendRconReply(connection, packet, response.str());
    }
}

DECLARE_HOOK(
    APrimalBuff_AddBuff,
    APrimalBuff*,
    APrimalBuff*,
    APrimalCharacter*,
    AActor*);

APrimalBuff* Hook_APrimalBuff_AddBuff(
    APrimalBuff* buff_template,
    APrimalCharacter* for_character,
    AActor* damage_causer)
{
    // Only compare the buff template's UClass* pointer against pre-resolved
    // cached pointers. The CDO (buff_template) is always fully constructed
    // so ClassPrivateField() is safe. We never touch for_character, never
    // call GetSettings(), never do string work — those crash during zone
    // transitions when UObjects are partially constructed.
    if (buff_template)
    {
        UClass* buff_class = buff_template->ClassPrivateField();
        if (buff_class)
        {
            std::lock_guard<std::mutex> lock(g_class_cache_mutex);
            for (UClass* blocked : g_cached_blocked_classes)
            {
                if (buff_class == blocked)
                {
                    g_blocked_total.fetch_add(1, std::memory_order_relaxed);
                    // Skip calling the original — the buff is never created.
                    // Return the CDO (buff_template) instead of nullptr so
                    // the caller has a valid pointer and won't null-deref.
                    return buff_template;
                }
            }
        }
    }

    return APrimalBuff_AddBuff_original(buff_template, for_character, damage_causer);
}

void TickSweep(float)
{
    if (!g_operational.load(std::memory_order_acquire) || !IsActiveMap())
        return;

    const std::shared_ptr<const Settings> settings = GetSettings();
    if (!settings || !settings->enabled || !settings->sweep_existing_player_buffs)
        return;

    UWorld* world = AsaApi::GetApiUtils().GetWorld();
    if (!world)
        return;

    const auto& controllers = world->PlayerControllerListField();
    for (TWeakObjectPtr<APlayerController> weak_controller : controllers)
    {
        APlayerController* base_controller = weak_controller.Get();
        if (!base_controller)
            continue;

        UClass* ctrl_class = base_controller->ClassPrivateField();
        if (!ctrl_class || !ctrl_class->IsChildOf(AShooterPlayerController::StaticClass()))
            continue;

        auto* controller = static_cast<AShooterPlayerController*>(base_controller);
        AShooterCharacter* character = controller->GetPlayerCharacter();

        // Determine whether the player is riding something (vehicle or dino).
        APawn* possessed = base_controller->GetPawnOrSpectator();
        APrimalDinoCharacter* mount = character
            ? character->MountedDinoField().Get() : nullptr;
        const bool is_riding = (possessed && possessed != character) || mount;

        // Always sweep standard cleanup buffs from the player character.
        // Only sweep mounted-only cleanup buffs (e.g. ocean zone debuff)
        // when the player is riding — swimming players keep those buffs.
        if (character)
        {
            DeactivateMatchingBuffsOnCharacter(
                character, settings->exact_cleanup_class_names);
            if (is_riding)
            {
                DeactivateMatchingBuffsOnCharacter(
                    character, settings->mounted_only_cleanup_class_names);
            }
        }

        // Sweep whatever the player is riding. For vehicles like the TEK
        // Hover Skiff the controller possesses the vehicle directly, so
        // GetPawnOrSpectator returns the skiff rather than the player.
        if (possessed && possessed != character)
        {
            UClass* pawn_class = possessed->ClassPrivateField();
            if (pawn_class && pawn_class->IsChildOf(APrimalCharacter::StaticClass()))
            {
                DeactivateMatchingBuffsOnCharacter(
                    static_cast<APrimalCharacter*>(possessed),
                    settings->exact_cleanup_class_names);
            }
        }

        // Also check MountedDino for traditional dino riding
        if (mount && mount != possessed)
        {
            DeactivateMatchingBuffsOnCharacter(
                mount, settings->exact_cleanup_class_names);
        }
    }
}

DECLARE_HOOK(AShooterGameMode_BeginPlay, void, AShooterGameMode*);

void Hook_AShooterGameMode_BeginPlay(AShooterGameMode* game_mode)
{
    if (!g_operational.load(std::memory_order_acquire))
    {
        AShooterGameMode_BeginPlay_original(game_mode);
        return;
    }

    // Cache the map before the original BeginPlay so a buff applied from inside the
    // original function cannot slip through while map identity is still unknown.
    RefreshMapState(game_mode, true);
    AShooterGameMode_BeginPlay_original(game_mode);
    RebuildClassCache();
    SweepExistingPlayerBuffs();
}

extern "C" __declspec(dllexport) void Plugin_Init()
{
    Log::Get().Init(kPluginName);

    try
    {
        std::atomic_store_explicit(
            &g_settings,
            LoadSettingsFromDisk(),
            std::memory_order_release);
    }
    catch (const std::exception& error)
    {
        Log::GetLog()->error(
            "Config load failed; plugin will remain disabled until a valid config is loaded: {}",
            error.what());

        Settings defaults = DefaultSettings();
        defaults.enabled = false;
        BuildFastClassNames(
            defaults.exact_blocked_class_names_or_paths,
            defaults.exact_blocked_class_names);
        BuildFastClassNames(
            defaults.exact_cleanup_class_names_or_paths,
            defaults.exact_cleanup_class_names);
        BuildFastClassNames(
            defaults.mounted_only_cleanup_class_names_or_paths,
            defaults.mounted_only_cleanup_class_names);
        std::atomic_store_explicit(
            &g_settings,
            std::make_shared<const Settings>(std::move(defaults)),
            std::memory_order_release);
    }

    g_add_buff_hooked = AsaApi::GetHooks().SetHook(
        kAddBuffHook,
        Hook_APrimalBuff_AddBuff,
        &APrimalBuff_AddBuff_original);
    g_begin_play_hooked = AsaApi::GetHooks().SetHook(
        kBeginPlayHook,
        Hook_AShooterGameMode_BeginPlay,
        &AShooterGameMode_BeginPlay_original);

    AsaApi::GetCommands().AddRconCommand("GWH.Status", StatusRcon);
    AsaApi::GetCommands().AddOnTickCallback("GWH.Tick", TickSweep);

    if (!g_add_buff_hooked || !g_begin_play_hooked)
    {
        Log::GetLog()->error(
            "Required hook setup failed (AddBuff={}, BeginPlay={}); plugin is inactive",
            g_add_buff_hooked,
            g_begin_play_hooked);

        if (g_add_buff_hooked)
        {
            AsaApi::GetHooks().DisableHook(kAddBuffHook, Hook_APrimalBuff_AddBuff);
            g_add_buff_hooked = false;
        }

        if (g_begin_play_hooked)
        {
            AsaApi::GetHooks().DisableHook(
                kBeginPlayHook,
                Hook_AShooterGameMode_BeginPlay);
            g_begin_play_hooked = false;
        }

        return;
    }

    g_operational.store(true, std::memory_order_release);
    AsaApi::GetCommands().AddRconCommand("GWH.Reload", ReloadRcon);
    AsaApi::GetCommands().AddRconCommand("GWH.DumpBuffs", DumpBuffsRcon);

    if (AsaApi::GetApiUtils().GetStatus() == AsaApi::ServerStatus::Ready)
    {
        RefreshMapState(nullptr, true);
        SweepExistingPlayerBuffs();
    }

    Log::GetLog()->info(
        "Loaded; targeting Buff_SkiffBuffetedByWind_C on Genesis_WP (AsaApi {})",
        AsaApi::Tools::GetApiVersion());
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
    g_operational.store(false, std::memory_order_release);

    AsaApi::GetCommands().RemoveOnTickCallback("GWH.Tick");
    AsaApi::GetCommands().RemoveRconCommand("GWH.Reload");
    AsaApi::GetCommands().RemoveRconCommand("GWH.Status");
    AsaApi::GetCommands().RemoveRconCommand("GWH.DumpBuffs");

    if (g_add_buff_hooked)
    {
        AsaApi::GetHooks().DisableHook(kAddBuffHook, Hook_APrimalBuff_AddBuff);
        g_add_buff_hooked = false;
    }

    if (g_begin_play_hooked)
    {
        AsaApi::GetHooks().DisableHook(
            kBeginPlayHook,
            Hook_AShooterGameMode_BeginPlay);
        g_begin_play_hooked = false;
    }

    g_map_state.store(MapState::Unknown, std::memory_order_release);
    Log::GetLog()->info("Unloaded");
}
