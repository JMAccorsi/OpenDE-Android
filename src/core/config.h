#pragma once

#include "core/object.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dx {
    class Ini {
    public:
        bool load(const std::filesystem::path& path);
        bool save() const;
        std::optional<std::string> get(std::string_view section, std::string_view key) const;
        std::vector<std::string> getAll(std::string_view section, std::string_view key) const;
        void set(std::string_view section, std::string_view key, std::string_view value);
        void clearSection(std::string_view section);
        std::vector<std::pair<std::string, std::string>> section(std::string_view section) const;

        std::filesystem::path path;

    private:
        struct Section {
            std::string name;
            std::vector<std::pair<std::string, std::string>> entries;
        };

        const Section* find(std::string_view name) const;
        std::vector<Section> sections;
    };

    namespace config {
        std::filesystem::path findGameRoot(const std::filesystem::path& fallback);
        std::filesystem::path resolveSubdir(const std::filesystem::path& root, std::string_view name);
        std::filesystem::path userDirectory();
        void useMod(const std::filesystem::path& directory);
        const std::filesystem::path& modDirectory();
        std::filesystem::path resolveCase(const std::filesystem::path& path);
        std::filesystem::path findFile(std::string relativeToSystem);
        void initialize(const std::filesystem::path& systemDirectory,
                        const std::filesystem::path& systemIniPath = {},
                        const std::filesystem::path& userIniPath = {});
        Ini& system();
        Ini& user();
        Ini* forName(Name configName);
        Ini* localization(std::string_view package);
        std::string localize(std::string_view section, std::string_view key, std::string_view package);
        std::string language();
        std::string bootLanguage();
        void setLanguage(std::string_view lang);
        std::filesystem::path localizedPackagePath(std::string_view package);
        void applyClassDefaults(UClass* cls);
        void applyObjectLocalization(UObject* object, std::string_view package);
        void clearObjectLocalizationSnapshots();
        void reloadClassLocalization(const std::function<void(UClass* cls, UProperty* prop, int32_t index, std::string_view oldVal, std::string_view newVal)>& onChanged = {});
        void setApplyClassDefaults(bool enabled);
        void applyObjectConfig(UObject* object);
        void saveObjectConfig(UObject* object, UClass* cls);
        Name configNameOf(const UClass* cls);
    }
}
