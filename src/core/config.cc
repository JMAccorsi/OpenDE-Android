#include "core/config.h"

#include "core/objects.h"
#include "engine/package.h"

#include <SDL_filesystem.h>
#include <SDL_stdinc.h>
#ifdef __ANDROID__
#include <SDL_system.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace dx {
    namespace {
        std::string trim(std::string_view text)
        {
            size_t begin = 0;
            size_t end = text.size();
            while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n')) {
                begin++;
            }
            while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n')) {
                end--;
            }
            return std::string(text.substr(begin, end - begin));
        }

        std::string lower(std::string_view text)
        {
            std::string result(text);
            for (char& c : result) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return result;
        }

        struct PropSnapshot {
            UProperty* property = nullptr;
            int32_t index = 0;
            std::string original;
        };

        struct ConfigState {
            std::filesystem::path systemDirectory;
            std::filesystem::path modDirectory;
            std::string activeLanguage = "int";
            std::string initialLanguage = "int";
            Ini system;
            Ini user;
            std::unordered_map<std::string, std::unique_ptr<Ini>> others;
            std::unordered_map<std::string, std::unique_ptr<Ini>> localizations;
            std::unordered_map<std::string, std::unique_ptr<Ini>> englishLocalizations;
            std::unordered_map<UClass*, std::vector<PropSnapshot>> classSnapshots;
            std::unordered_map<UObject*, std::vector<PropSnapshot>> objectSnapshots;
        };

        ConfigState& state()
        {
            static ConfigState instance;
            return instance;
        }

        std::string normalizeLanguage(std::string_view lang)
        {
            std::string normalized = lower(trim(lang));
            if (normalized.empty() || normalized == "en" || normalized == "en-us" || normalized == "english") {
                return "int";
            }
            if (normalized == "pt" || normalized == "pt-br" || normalized == "pt_br" || normalized == "portuguese") {
                return "ptb";
            }
            return normalized;
        }

        bool isStringProperty(const UProperty* property)
        {
            return property->type == PropType::Str || property->type == PropType::String;
        }

        std::string readPropertyValue(const UProperty* property, const uint8_t* addr)
        {
            if (isStringProperty(property)) {
                return *reinterpret_cast<const std::string*>(addr);
            }
            return property->toText(addr);
        }

        void restorePropertyValue(UProperty* property, uint8_t* addr, const std::string& original)
        {
            if (isStringProperty(property)) {
                *reinterpret_cast<std::string*>(addr) = original;
                return;
            }
            property->fromText(addr, original);
        }

        void ensureSnapshot(std::vector<PropSnapshot>& snapshots, UProperty* property, int32_t index, const uint8_t* base)
        {
            for (const PropSnapshot& s : snapshots) {
                if (s.property == property && s.index == index) {
                    return;
                }
            }
            const uint8_t* addr = base + property->offset + static_cast<size_t>(index) * property->elementSize();
            snapshots.push_back({ property, index, readPropertyValue(property, addr) });
        }

        void restoreSnapshots(const std::vector<PropSnapshot>& snapshots, uint8_t* base)
        {
            for (const PropSnapshot& s : snapshots) {
                if (s.property && s.index >= 0 && s.index < s.property->arrayDim) {
                    uint8_t* addr = base + s.property->offset + static_cast<size_t>(s.index) * s.property->elementSize();
                    restorePropertyValue(s.property, addr, s.original);
                }
            }
        }

        std::string utf8ToCp1252IfValid(std::string_view text)
        {
            bool hasBom = text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xef && static_cast<uint8_t>(text[1]) == 0xbb && static_cast<uint8_t>(text[2]) == 0xbf;
            size_t start = hasBom ? 3 : 0;
            bool sawMultibyte = false;
            std::string out;
            out.reserve(text.size());
            for (size_t i = start; i < text.size();) {
                uint8_t b0 = static_cast<uint8_t>(text[i]);
                if (b0 < 0x80) {
                    out.push_back(static_cast<char>(b0));
                    i++;
                    continue;
                }
                uint32_t cp = 0;
                size_t extra = 0;
                if ((b0 & 0xe0) == 0xc0 && i + 1 < text.size()) {
                    cp = b0 & 0x1f;
                    extra = 1;
                } else if ((b0 & 0xf0) == 0xe0 && i + 2 < text.size()) {
                    cp = b0 & 0x0f;
                    extra = 2;
                } else if ((b0 & 0xf8) == 0xf0 && i + 3 < text.size()) {
                    cp = b0 & 0x07;
                    extra = 3;
                } else {
                    return std::string(text);
                }
                for (size_t j = 1; j <= extra; j++) {
                    uint8_t bx = static_cast<uint8_t>(text[i + j]);
                    if ((bx & 0xc0) != 0x80) {
                        return std::string(text);
                    }
                    cp = (cp << 6) | (bx & 0x3f);
                }
                sawMultibyte = true;
                i += 1 + extra;
                if (cp < 256) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp == 0x2013 || cp == 0x2014) {
                    out.push_back('-');
                } else if (cp == 0x2018 || cp == 0x2019) {
                    out.push_back('\'');
                } else if (cp == 0x201c || cp == 0x201d) {
                    out.push_back('"');
                } else if (cp == 0x2026) {
                    out.append("...");
                } else {
                    out.push_back('?');
                }
            }
            return (hasBom || sawMultibyte) ? out : std::string(text);
        }

        std::string packageOf(const UObject* object)
        {
            const UObject* current = object;
            while (current->outer) {
                current = current->outer;
            }
            return current->name.str();
        }

        std::string configSection(const UProperty* property, const UClass* cls)
        {
            const UClass* owner = cast<UClass>(property->outer);
            if ((property->propertyFlags & CPF_GlobalConfig) && owner) {
                cls = owner;
            }
            return packageOf(cls) + "." + cls->name.str();
        }

        void applyProperty(UProperty* property, uint8_t* data, Ini& ini, const std::string& section)
        {
            if (property->arrayDim > 1) {
                for (int32_t i = 0; i < property->arrayDim; i++) {
                    auto value = ini.get(section, property->name.str() + "[" + std::to_string(i) + "]");
                    if (value) {
                        property->fromText(data + property->offset + static_cast<size_t>(i) * property->elementSize(), *value);
                    }
                }
                return;
            }
            auto value = ini.get(section, property->name.str());
            if (value) {
                property->fromText(data + property->offset, *value);
            }
        }

        void loadConfigPropagated(UObject* object, UClass* cls, Ini& ini)
        {
            if (!(cls->classFlags & CLASS_Config)) {
                return;
            }
            if (cls->superClass()) {
                loadConfigPropagated(object, cls->superClass(), ini);
            }
            for (UProperty* property : cls->properties) {
                if (property->propertyFlags & CPF_Config) {
                    applyProperty(property, object->data, ini, configSection(property, cls));
                }
            }
        }
    }

    bool Ini::load(const std::filesystem::path& filePath)
    {
        path = filePath;
        std::ifstream file(filePath, std::ios::binary);
        if (!file) {
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (content.size() >= 2 && static_cast<uint8_t>(content[0]) == 0xff && static_cast<uint8_t>(content[1]) == 0xfe) {
            std::string narrow;
            for (size_t i = 2; i + 1 < content.size(); i += 2) {
                uint16_t c = static_cast<uint16_t>(static_cast<uint8_t>(content[i]) | (static_cast<uint8_t>(content[i + 1]) << 8));
                narrow.push_back(c < 256 ? static_cast<char>(c) : '?');
            }
            content = std::move(narrow);
        } else {
            content = utf8ToCp1252IfValid(content);
        }
        Section* current = nullptr;
        size_t pos = 0;
        while (pos < content.size()) {
            size_t end = content.find('\n', pos);
            if (end == std::string::npos) {
                end = content.size();
            }
            std::string line = trim(std::string_view(content).substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line[0] == ';') {
                continue;
            }
            if (line.front() == '[' && line.back() == ']') {
                std::string sectionName = line.substr(1, line.size() - 2);
                current = nullptr;
                for (Section& existing : sections) {
                    if (iequals(existing.name, sectionName)) {
                        current = &existing;
                    }
                }
                if (!current) {
                    sections.push_back({ sectionName, {} });
                    current = &sections.back();
                }
                continue;
            }
            size_t equals = line.find('=');
            if (!current || equals == std::string::npos) {
                continue;
            }
            current->entries.emplace_back(trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
        }
        return true;
    }

    bool Ini::save() const
    {
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }
        for (const Section& section : sections) {
            file << "[" << section.name << "]\r\n";
            for (const auto& [key, value] : section.entries) {
                file << key << "=" << value << "\r\n";
            }
            file << "\r\n";
        }
        return true;
    }

    const Ini::Section* Ini::find(std::string_view name) const
    {
        for (const Section& section : sections) {
            if (iequals(section.name, name)) {
                return &section;
            }
        }
        return nullptr;
    }

    std::optional<std::string> Ini::get(std::string_view sectionName, std::string_view key) const
    {
        const Section* section = find(sectionName);
        if (!section) {
            return std::nullopt;
        }
        for (const auto& [entryKey, value] : section->entries) {
            if (iequals(entryKey, key)) {
                return value;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> Ini::getAll(std::string_view sectionName, std::string_view key) const
    {
        std::vector<std::string> values;
        const Section* section = find(sectionName);
        if (!section) {
            return values;
        }
        for (const auto& [entryKey, value] : section->entries) {
            if (iequals(entryKey, key)) {
                values.push_back(value);
            }
        }
        return values;
    }

    void Ini::set(std::string_view sectionName, std::string_view key, std::string_view value)
    {
        Section* section = nullptr;
        for (Section& existing : sections) {
            if (iequals(existing.name, sectionName)) {
                section = &existing;
            }
        }
        if (!section) {
            sections.push_back({ std::string(sectionName), {} });
            section = &sections.back();
        }
        for (auto& [entryKey, entryValue] : section->entries) {
            if (iequals(entryKey, key)) {
                entryValue = value;
                return;
            }
        }
        section->entries.emplace_back(std::string(key), std::string(value));
    }

    std::vector<std::pair<std::string, std::string>> Ini::section(std::string_view sectionName) const
    {
        const Section* found = find(sectionName);
        if (!found) {
            return {};
        }
        return found->entries;
    }

    namespace config {
        std::filesystem::path fromUtf8(char* text)
        {
            std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(text)));
            SDL_free(text);
            return path;
        }

        std::vector<std::filesystem::path> steamRoots()
        {
            std::vector<std::filesystem::path> roots;
#ifdef _WIN32
            for (const char* variable : { "ProgramFiles(x86)", "ProgramFiles" }) {
                if (const char* value = std::getenv(variable)) {
                    roots.push_back(std::filesystem::path(value) / "Steam");
                }
            }
#else
            if (const char* home = std::getenv("HOME")) {
                std::filesystem::path base(home);
                roots.push_back(base / "Library" / "Application Support" / "Steam");
                roots.push_back(base / ".steam" / "steam");
                roots.push_back(base / ".local" / "share" / "Steam");
                roots.push_back(base / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");
            }
#endif
            return roots;
        }

        std::vector<std::filesystem::path> steamLibraries(const std::filesystem::path& root)
        {
            std::vector<std::filesystem::path> libraries { root };
            std::ifstream file(root / "steamapps" / "libraryfolders.vdf");
            std::string line;
            while (std::getline(file, line)) {
                std::istringstream stream(line);
                std::string key;
                std::string value;
                if (stream >> std::quoted(key) >> std::quoted(value) && key == "path") {
                    libraries.emplace_back(value);
                }
            }
            return libraries;
        }

        std::filesystem::path resolveSubdir(const std::filesystem::path& root, std::string_view name)
        {
            std::error_code error;
            std::filesystem::path exact = root / std::string(name);
            if (std::filesystem::is_directory(exact, error)) {
                return exact;
            }
            if (std::filesystem::is_directory(root, error)) {
                for (const auto& item : std::filesystem::directory_iterator(root, error)) {
                    if (error) {
                        break;
                    }
                    if (item.is_directory(error) && iequals(item.path().filename().string(), name)) {
                        return item.path();
                    }
                }
            }
            return exact;
        }

        namespace {
            std::filesystem::path findFileCaseInsensitive(const std::filesystem::path& directory, std::string_view filename)
            {
                std::error_code ec;
                if (!std::filesystem::is_directory(directory, ec)) {
                    return {};
                }
                std::filesystem::path exact = directory / std::string(filename);
                if (std::filesystem::is_regular_file(exact, ec)) {
                    return exact;
                }
                for (const auto& item : std::filesystem::directory_iterator(directory, ec)) {
                    if (ec) {
                        break;
                    }
                    if (item.is_regular_file(ec) && iequals(item.path().filename().string(), filename)) {
                        return item.path();
                    }
                }
                return {};
            }

            bool isValidGameRoot(const std::filesystem::path& candidate)
            {
                if (candidate.empty()) {
                    return false;
                }
                std::error_code error;
                std::filesystem::path sysDir = resolveSubdir(candidate, "System");
                if (!std::filesystem::is_directory(sysDir, error)) {
                    return false;
                }
                return !findFileCaseInsensitive(sysDir, "DeusEx.u").empty();
            }

#ifdef __ANDROID__
            std::filesystem::path readCachedGameRoot(const char* baseDir)
            {
                if (!baseDir || !*baseDir) {
                    return {};
                }
                std::ifstream in(std::filesystem::path(baseDir) / "opende_game_root.txt", std::ios::binary);
                if (!in) {
                    return {};
                }
                std::string line;
                std::getline(in, line);
                std::string trimmed = trim(line);
                if (trimmed.empty()) {
                    return {};
                }
                std::filesystem::path candidate(trimmed);
                return isValidGameRoot(candidate) ? candidate : std::filesystem::path();
            }
#endif
        }

        std::filesystem::path findGameRoot(const std::filesystem::path& fallback)
        {
#ifdef __ANDROID__
            if (auto cached = readCachedGameRoot(SDL_AndroidGetExternalStoragePath()); !cached.empty()) {
                return cached;
            }
            if (auto cached = readCachedGameRoot(SDL_AndroidGetInternalStoragePath()); !cached.empty()) {
                return cached;
            }
#endif
            std::vector<std::filesystem::path> candidates;
#ifdef __ANDROID__
            candidates.emplace_back("/storage/emulated/0/OpenDE");
            candidates.emplace_back("/sdcard/OpenDE");
            candidates.emplace_back("/storage/emulated/0/DeusEx");
            candidates.emplace_back("/sdcard/DeusEx");
            if (const char* ext = SDL_AndroidGetExternalStoragePath()) {
                std::filesystem::path extPath(ext);
                candidates.push_back(extPath / "DeusEx");
                candidates.push_back(extPath);
                candidates.push_back(extPath.parent_path());
            }
#endif
            if (const char* value = std::getenv("DEUSEX_GAME_DATA")) {
                candidates.emplace_back(value);
            }
            if (const char* value = std::getenv("APPIMAGE")) {
                candidates.push_back(std::filesystem::path(value).parent_path() / "game-data");
            }
            if (char* base = SDL_GetBasePath()) {
                candidates.push_back(fromUtf8(base) / "game-data");
            }
            for (const std::filesystem::path& root : steamRoots()) {
                for (const std::filesystem::path& library : steamLibraries(root)) {
                    candidates.push_back(library / "steamapps" / "common" / "Deus Ex");
                }
            }
            for (const std::filesystem::path& candidate : candidates) {
                if (isValidGameRoot(candidate)) {
                    return candidate;
                }
            }
#ifdef __ANDROID__
            if (isValidGameRoot(fallback)) {
                return fallback;
            }
            return std::filesystem::path("/storage/emulated/0/OpenDE");
#else
            return fallback;
#endif
        }

        std::filesystem::path userDirectory()
        {
#ifdef __ANDROID__
            std::error_code error;
            std::filesystem::path directory("/storage/emulated/0/OpenDE/User");
            if (const char* ext = SDL_AndroidGetExternalStoragePath()) {
                std::filesystem::path extDir(ext);
                std::filesystem::create_directories(extDir, error);
                if (!error) {
                    directory = std::move(extDir);
                }
            }
            if (!state().modDirectory.empty()) {
                directory /= "mods";
                directory /= state().modDirectory.filename();
            }
            std::filesystem::create_directories(directory, error);
            return directory;
#else
            const char* xdg = std::getenv("XDG_DATA_HOME");
#ifdef _WIN32
            if (!xdg) {
                if (char* pref = SDL_GetPrefPath("", "deusex")) {
                    std::filesystem::path directory = fromUtf8(pref);
                    if (!state().modDirectory.empty()) {
                        directory /= "mods";
                        directory /= state().modDirectory.filename();
                    }
                    std::error_code error;
                    std::filesystem::create_directories(directory, error);
                    return directory;
                }
            }
#endif
            const char* home = std::getenv("HOME");
            std::filesystem::path base = xdg ? std::filesystem::path(xdg) : (home ? std::filesystem::path(home) / ".local" / "share" : std::filesystem::temp_directory_path());
            std::filesystem::path directory = base / "deusex";
            if (!state().modDirectory.empty()) {
                directory /= "mods";
                directory /= state().modDirectory.filename();
            }
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            return directory;
#endif
        }

        void useMod(const std::filesystem::path& directory)
        {
            state().modDirectory = directory;
        }

        const std::filesystem::path& modDirectory()
        {
            return state().modDirectory;
        }

        std::filesystem::path resolveCase(const std::filesystem::path& path)
        {
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                return path;
            }
            std::vector<std::string> parts;
            std::filesystem::path root;
            for (const auto& part : path) {
                if (part.empty()) {
                    continue;
                }
                if (parts.empty() && path.has_root_path() && (part == path.root_name() || part == path.root_directory() || part == "/")) {
                    root /= part;
                    continue;
                }
                parts.push_back(part.string());
            }
            if (root.empty()) {
                root = ".";
            }
            std::filesystem::path cur = root;
            for (const std::string& segment : parts) {
                if (segment == ".") {
                    continue;
                }
                if (segment == "..") {
                    cur /= "..";
                    continue;
                }
                std::filesystem::path exact = cur / segment;
                if (std::filesystem::exists(exact, ec)) {
                    cur = exact;
                    continue;
                }
                bool found = false;
                for (const auto& entry : std::filesystem::directory_iterator(cur, ec)) {
                    if (ec) {
                        break;
                    }
                    if (iequals(entry.path().filename().string(), segment)) {
                        cur = entry.path();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return path;
                }
            }
            return cur;
        }

        std::filesystem::path findFile(std::string relativeToSystem)
        {
            std::replace(relativeToSystem.begin(), relativeToSystem.end(), '\\', '/');
            std::vector<std::filesystem::path> systems;
            if (!state().modDirectory.empty()) {
                std::filesystem::path modSys = resolveSubdir(state().modDirectory, "System");
                if (!modSys.empty()) {
                    systems.push_back(modSys);
                }
            }
            systems.push_back(state().systemDirectory);
            std::error_code ec;
            for (const std::filesystem::path& system : systems) {
                std::filesystem::path candidate = resolveCase((system / relativeToSystem).lexically_normal());
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    return candidate;
                }
            }
            return {};
        }

        void loadPreferred(Ini& ini, const std::filesystem::path& userFile, const std::filesystem::path& gameFile, const std::filesystem::path& fallbackFile)
        {
            if (!ini.load(userFile) && !ini.load(gameFile)) {
                ini.load(fallbackFile);
            }
            ini.path = userFile;
        }

        namespace {
            std::filesystem::path findModIni(const std::filesystem::path& modSys, std::string_view name)
            {
                if (modSys.empty()) {
                    return {};
                }
                std::string exact = std::string(name) + ".ini";
                std::filesystem::path exactPath = resolveCase(modSys / exact);
                std::error_code ec;
                if (std::filesystem::is_regular_file(exactPath, ec)) {
                    return exactPath;
                }
                std::vector<std::filesystem::path> candidates;
                for (const auto& entry : std::filesystem::directory_iterator(modSys, ec)) {
                    if (ec || !entry.is_regular_file()) {
                        continue;
                    }
                    if (iequals(entry.path().extension().string(), ".ini")) {
                        candidates.push_back(entry.path());
                    }
                }
                std::sort(candidates.begin(), candidates.end());
                for (const auto& candidate : candidates) {
                    Ini probe;
                    if (probe.load(candidate) && probe.get("Engine.Engine", "GameRenderDevice").has_value()) {
                        return candidate;
                    }
                }
                return {};
            }

            std::filesystem::path findModUserIni(const std::filesystem::path& modSys)
            {
                if (modSys.empty()) {
                    return {};
                }
                std::filesystem::path direct = resolveCase(modSys / "User.ini");
                std::error_code ec;
                if (std::filesystem::is_regular_file(direct, ec)) {
                    return direct;
                }
                std::vector<std::filesystem::path> candidates;
                for (const auto& entry : std::filesystem::directory_iterator(modSys, ec)) {
                    if (ec || !entry.is_regular_file()) {
                        continue;
                    }
                    std::string stem = lower(entry.path().stem().string());
                    if (iequals(entry.path().extension().string(), ".ini") &&
                        stem.find("user") != std::string::npos) {
                        candidates.push_back(entry.path());
                    }
                }
                std::sort(candidates.begin(), candidates.end());
                return candidates.empty() ? std::filesystem::path{} : candidates.front();
            }

            std::string userCopyName(const std::filesystem::path& path, const char* fallback)
            {
                return path.empty() ? std::string(fallback) : path.filename().string();
            }
        }

        void initialize(const std::filesystem::path& systemDirectory,
                        const std::filesystem::path& systemIniPath,
                        const std::filesystem::path& userIniPath)
        {
            ConfigState& s = state();
            s.systemDirectory = systemDirectory;
            std::filesystem::path userDir = userDirectory();
            std::filesystem::path modSys = s.modDirectory.empty() ? std::filesystem::path{} : resolveSubdir(s.modDirectory, "System");

            std::filesystem::path gameSystemIni = !systemIniPath.empty() ? systemIniPath : findModIni(modSys, "DeusEx");
            if (gameSystemIni.empty()) {
                gameSystemIni = resolveCase(systemDirectory / "DeusEx.ini");
            }
            std::filesystem::path gameUserIni = !userIniPath.empty() ? userIniPath : findModUserIni(modSys);
            if (gameUserIni.empty()) {
                gameUserIni = resolveCase(systemDirectory / "User.ini");
            }

            std::filesystem::path userSysDir = s.modDirectory.empty() ? userDir : (userDir / "System");
            std::error_code ec;
            std::filesystem::create_directories(userSysDir, ec);
            loadPreferred(s.system,
                          userSysDir / userCopyName(systemIniPath, "DeusEx.ini"),
                          gameSystemIni,
                          resolveCase(systemDirectory / "Default.ini"));
            loadPreferred(s.user,
                          userSysDir / userCopyName(userIniPath, "User.ini"),
                          gameUserIni,
                          resolveCase(systemDirectory / "DefUser.ini"));
            if (auto lang = s.system.get("Engine.Engine", "Language")) {
                s.activeLanguage = normalizeLanguage(*lang);
            } else {
                s.activeLanguage = "int";
            }
            s.initialLanguage = s.activeLanguage;
        }

        Ini& system()
        {
            return state().system;
        }

        Ini& user()
        {
            return state().user;
        }

        Ini* forName(Name configName)
        {
            if (configName.none() || iequals(configName.str(), "System") || iequals(configName.str(), "DeusEx")) {
                return &state().system;
            }
            if (iequals(configName.str(), "User")) {
                return &state().user;
            }
            std::string key = lower(configName.str());
            auto it = state().others.find(key);
            if (it != state().others.end()) {
                return it->second.get();
            }
            auto ini = std::make_unique<Ini>();
            std::filesystem::path userSysDir = state().modDirectory.empty() ? userDirectory() : (userDirectory() / "System");
            std::filesystem::path modSys = state().modDirectory.empty() ? std::filesystem::path{} : resolveSubdir(state().modDirectory, "System");
            std::filesystem::path iniFile = std::string(configName.str()) + ".ini";
            std::filesystem::path modIni = modSys.empty() ? std::filesystem::path{} : resolveCase(modSys / iniFile);
            std::filesystem::path sysIni = resolveCase(state().systemDirectory / iniFile);
            loadPreferred(*ini, userSysDir / iniFile, modIni.empty() ? sysIni : modIni, sysIni);
            Ini* result = ini.get();
            state().others.emplace(key, std::move(ini));
            return result;
        }

        namespace {
            bool loadCaseInsensitive(Ini& ini, const std::filesystem::path& directory, std::string_view filename)
            {
                std::error_code ec;
                std::filesystem::path exact = directory / std::string(filename);
                if (std::filesystem::is_regular_file(exact, ec) && ini.load(exact)) {
                    return true;
                }
                if (!std::filesystem::is_directory(directory, ec)) {
                    return false;
                }
                for (const auto& item : std::filesystem::directory_iterator(directory, ec)) {
                    if (ec) {
                        break;
                    }
                    if (iequals(item.path().filename().string(), filename) && ini.load(item.path())) {
                        return true;
                    }
                }
                return false;
            }

            Ini* localizationFor(std::string_view package, std::string_view lang)
            {
                ConfigState& s = state();
                std::string key = lower(package);
                auto& cache = iequals(lang, "int") ? s.englishLocalizations : s.localizations;
                auto it = cache.find(key);
                if (it != cache.end()) {
                    return it->second.get();
                }
                auto ini = std::make_unique<Ini>();
                std::filesystem::path userSysDir = resolveSubdir(userDirectory(), "System");
                std::filesystem::path modSysDir = s.modDirectory.empty() ? std::filesystem::path{} : resolveSubdir(s.modDirectory, "System");
                bool loaded = false;
                if (!iequals(lang, "int")) {
                    std::string normLang = normalizeLanguage(lang);
                    loaded = loadCaseInsensitive(*ini, resolveSubdir(userSysDir, normLang), std::string(package) + ".int")
                        || loadCaseInsensitive(*ini, userSysDir, std::string(package) + "." + normLang)
                        || (!modSysDir.empty() && loadCaseInsensitive(*ini, resolveSubdir(modSysDir, normLang), std::string(package) + ".int"))
                        || (!modSysDir.empty() && loadCaseInsensitive(*ini, modSysDir, std::string(package) + "." + normLang))
                        || (!modSysDir.empty() && loadCaseInsensitive(*ini, modSysDir, std::string(package) + ".int"))
                        || loadCaseInsensitive(*ini, resolveSubdir(s.systemDirectory, normLang), std::string(package) + ".int")
                        || loadCaseInsensitive(*ini, s.systemDirectory, std::string(package) + "." + normLang);
                } else {
                    loaded = loadCaseInsensitive(*ini, userSysDir, std::string(package) + ".int")
                        || (!modSysDir.empty() && loadCaseInsensitive(*ini, modSysDir, std::string(package) + ".int"))
                        || loadCaseInsensitive(*ini, s.systemDirectory, std::string(package) + ".int");
                }
                (void)loaded;
                Ini* result = ini.get();
                cache.emplace(key, std::move(ini));
                return result;
            }

            const PropSnapshot* findSnapshot(const std::vector<PropSnapshot>& snapshots, const UProperty* property, int32_t index)
            {
                for (const PropSnapshot& s : snapshots) {
                    if (s.property == property && s.index == index) {
                        return &s;
                    }
                }
                return nullptr;
            }

            std::optional<std::string> lookupLocalizedEntry(UClass* cls, UProperty* property, int32_t index, std::string_view lang, const std::string& clsOriginal)
            {
                const ConfigState& s = state();
                std::string key = property->arrayDim > 1
                    ? property->name.str() + "[" + std::to_string(index) + "]"
                    : property->name.str();
                for (UClass* owner = cls; owner; owner = owner->superClass()) {
                    if (owner != cls) {
                        auto it = s.classSnapshots.find(owner);
                        if (it != s.classSnapshots.end()) {
                            if (const PropSnapshot* parentSnap = findSnapshot(it->second, property, index)) {
                                if (parentSnap->original != clsOriginal) {
                                    break;
                                }
                            }
                        }
                    }
                    if (Ini* ini = localizationFor(packageOf(owner), lang)) {
                        if (auto val = ini->get(owner->name.str(), key)) {
                            return val;
                        }
                    }
                }
                return std::nullopt;
            }

            void snapshotClassDefault(UClass* cls, std::vector<PropSnapshot>& snapshots, UProperty* property, int32_t index, const uint8_t* data)
            {
                if (findSnapshot(snapshots, property, index)) {
                    return;
                }
                const size_t offset = property->offset + static_cast<size_t>(index) * property->elementSize();
                std::string original = readPropertyValue(property, data + offset);
                // Subclasses link against their parent's already-localized defaults at boot; recover
                // the pre-localization original when the subclass inherited the parent's current value.
                if (UClass* parent = cls->superClass(); parent && parent->defaultObject && parent->defaultObject->data) {
                    const ConfigState& s = state();
                    if (auto parentIt = s.classSnapshots.find(parent); parentIt != s.classSnapshots.end()) {
                        if (const PropSnapshot* parentSnap = findSnapshot(parentIt->second, property, index)) {
                            if (offset + property->elementSize() <= parent->size &&
                                original == readPropertyValue(property, parent->defaultObject->data + offset)) {
                                original = parentSnap->original;
                            }
                        }
                    }
                }
                snapshots.push_back({ property, index, std::move(original) });
            }

            void applyClassLocalization(UClass* cls)
            {
                ConfigState& s = state();
                uint8_t* data = cls->defaultObject->data;
                std::vector<PropSnapshot>& snapshots = s.classSnapshots[cls];
                const bool checkEnglishFallback = !iequals(s.activeLanguage, "int");
                for (UProperty* property : cls->properties) {
                    if (!(property->propertyFlags & CPF_Localized)) {
                        continue;
                    }
                    for (int32_t i = 0; i < property->arrayDim; i++) {
                        snapshotClassDefault(cls, snapshots, property, i, data);
                        const PropSnapshot* snap = findSnapshot(snapshots, property, i);
                        const std::string orig = snap ? snap->original : std::string();
                        std::optional<std::string> val = lookupLocalizedEntry(cls, property, i, s.activeLanguage, orig);
                        if (!val && checkEnglishFallback) {
                            val = lookupLocalizedEntry(cls, property, i, "int", orig);
                        }
                        if (val) {
                            property->fromText(data + property->offset + static_cast<size_t>(i) * property->elementSize(), *val);
                        }
                    }
                }
            }
        }

        Ini* localization(std::string_view package)
        {
            return localizationFor(package, state().activeLanguage);
        }

        std::string language()
        {
            return state().activeLanguage;
        }

        std::string bootLanguage()
        {
            return state().initialLanguage;
        }

        void setLanguage(std::string_view lang)
        {
            ConfigState& s = state();
            std::string normalized = normalizeLanguage(lang);
            if (s.activeLanguage != normalized) {
                s.activeLanguage = normalized;
                s.localizations.clear();
            }
            s.system.set("Engine.Engine", "Language", s.activeLanguage);
            s.system.save();
        }

        std::filesystem::path localizedPackagePath(std::string_view package)
        {
            ConfigState& s = state();
            std::string filename = std::string(package) + ".u";
            std::filesystem::path userSysDir = resolveSubdir(userDirectory(), "System");
            std::filesystem::path modSysDir = s.modDirectory.empty() ? std::filesystem::path{} : resolveSubdir(s.modDirectory, "System");
            if (!iequals(s.activeLanguage, "int")) {
                if (auto p = findFileCaseInsensitive(resolveSubdir(userSysDir, s.activeLanguage), filename); !p.empty()) {
                    return p;
                }
                if (!modSysDir.empty()) {
                    if (auto p = findFileCaseInsensitive(resolveSubdir(modSysDir, s.activeLanguage), filename); !p.empty()) {
                        return p;
                    }
                }
                if (auto p = findFileCaseInsensitive(resolveSubdir(s.systemDirectory, s.activeLanguage), filename); !p.empty()) {
                    return p;
                }
            }
            if (auto p = findFileCaseInsensitive(userSysDir, filename); !p.empty()) {
                return p;
            }
            if (!modSysDir.empty()) {
                if (auto p = findFileCaseInsensitive(modSysDir, filename); !p.empty()) {
                    return p;
                }
            }
            return findFileCaseInsensitive(s.systemDirectory, filename);
        }

        std::string localize(std::string_view section, std::string_view key, std::string_view package)
        {
            std::optional<std::string> value;
            if (Ini* ini = localizationFor(package, state().activeLanguage)) {
                value = ini->get(section, key);
            }
            if (!value && !iequals(state().activeLanguage, "int")) {
                if (Ini* en = localizationFor(package, "int")) {
                    value = en->get(section, key);
                }
            }
            if (value) {
                std::string text = *value;
                if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
                    text = text.substr(1, text.size() - 2);
                }
                return text;
            }
            return "<?int?" + std::string(package) + "." + std::string(section) + "." + std::string(key) + "?>";
        }

        Name configNameOf(const UClass* cls)
        {
            for (const UClass* current = cls; current; current = current->superClass()) {
                if (!current->configName.none()) {
                    return current->configName;
                }
            }
            return Name("System");
        }

        namespace {
            bool& classDefaultsEnabled()
            {
                static bool enabled = true;
                return enabled;
            }
        }

        void setApplyClassDefaults(bool enabled)
        {
            classDefaultsEnabled() = enabled;
        }

        void applyClassDefaults(UClass* cls)
        {
            if (!classDefaultsEnabled()) {
                return;
            }
            uint8_t* data = cls->defaultObject->data;
            Ini* ini = forName(configNameOf(cls));
            if (ini) {
                for (UProperty* property : cls->properties) {
                    if (property->propertyFlags & CPF_Config) {
                        applyProperty(property, data, *ini, configSection(property, cls));
                    }
                }
            }
            applyClassLocalization(cls);
        }

        void applyObjectLocalization(UObject* object, std::string_view package)
        {
            if (!object || !object->cls || !object->data) {
                return;
            }
            ConfigState& s = state();
            std::string section = object->name.str();
            if (auto snapIt = s.objectSnapshots.find(object); snapIt != s.objectSnapshots.end()) {
                restoreSnapshots(snapIt->second, object->data);
            }
            Ini* loc = localizationFor(package, s.activeLanguage);
            Ini* enLoc = iequals(s.activeLanguage, "int") ? nullptr : localizationFor(package, "int");
            auto applyEntries = [&](Ini* source) {
                if (!source) {
                    return;
                }
                for (const auto& [entryKey, entryVal] : source->section(section)) {
                    std::string propName = entryKey;
                    int32_t index = 0;
                    if (size_t bracket = propName.find('['); bracket != std::string::npos) {
                        index = std::atoi(propName.c_str() + bracket + 1);
                        propName = propName.substr(0, bracket);
                    }
                    UProperty* prop = object->cls->findProperty(Name(propName));
                    if (!prop || index < 0 || index >= prop->arrayDim) {
                        continue;
                    }
                    if (!(prop->propertyFlags & CPF_Localized) && !isStringProperty(prop) && prop->type != PropType::Struct) {
                        continue;
                    }
                    ensureSnapshot(s.objectSnapshots[object], prop, index, object->data);
                    prop->fromText(object->data + prop->offset + static_cast<size_t>(index) * prop->elementSize(), entryVal);
                }
            };
            applyEntries(enLoc);
            if (loc != enLoc) {
                applyEntries(loc);
            }
        }

        void clearObjectLocalizationSnapshots()
        {
            state().objectSnapshots.clear();
        }

        void reloadClassLocalization(const std::function<void(UClass* cls, UProperty* prop, int32_t index, std::string_view oldVal, std::string_view newVal)>& onChanged)
        {
            ConfigState& s = state();
            for (UObject* candidate : objects::all()) {
                UClass* cls = cast<UClass>(candidate);
                if (!cls || !cls->defaultObject || !cls->defaultObject->data) {
                    continue;
                }
                uint8_t* data = cls->defaultObject->data;
                std::vector<PropSnapshot> oldValues;
                if (onChanged) {
                    for (UProperty* prop : cls->properties) {
                        if (!(prop->propertyFlags & CPF_Localized)) {
                            continue;
                        }
                        for (int32_t i = 0; i < prop->arrayDim; i++) {
                            const uint8_t* addr = data + prop->offset + static_cast<size_t>(i) * prop->elementSize();
                            oldValues.push_back({ prop, i, readPropertyValue(prop, addr) });
                        }
                    }
                }
                if (auto snapIt = s.classSnapshots.find(cls); snapIt != s.classSnapshots.end()) {
                    restoreSnapshots(snapIt->second, data);
                }
                applyClassLocalization(cls);
                if (onChanged) {
                    for (const PropSnapshot& prev : oldValues) {
                        const uint8_t* addr = data + prev.property->offset + static_cast<size_t>(prev.index) * prev.property->elementSize();
                        std::string updated = readPropertyValue(prev.property, addr);
                        if (updated != prev.original) {
                            onChanged(cls, prev.property, prev.index, prev.original, updated);
                        }
                    }
                }
            }
        }

        void applyObjectConfig(UObject* object)
        {
            UClass* cls = object->cls;
            Ini* ini = forName(configNameOf(cls));
            if (!ini) {
                return;
            }
            for (UProperty* property : cls->properties) {
                if (property->propertyFlags & (CPF_Config | CPF_GlobalConfig)) {
                    applyProperty(property, object->data, *ini, configSection(property, cls));
                }
            }
        }

        void saveObjectConfig(UObject* object, UClass* cls)
        {
            Ini* ini = forName(configNameOf(cls));
            if (!ini) {
                return;
            }
            for (UProperty* property : cls->properties) {
                if (!(property->propertyFlags & (CPF_Config | CPF_GlobalConfig))) {
                    continue;
                }
                std::string section = configSection(property, cls);
                if (property->arrayDim > 1) {
                    for (int32_t i = 0; i < property->arrayDim; i++) {
                        ini->set(section, property->name.str() + "[" + std::to_string(i) + "]", property->toText(object->data + property->offset + static_cast<size_t>(i) * property->elementSize()));
                    }
                } else {
                    ini->set(section, property->name.str(), property->toText(object->data + property->offset));
                }
            }
            ini->save();
            UClass* top = object->cls;
            while (top->superClass() && (top->superClass()->classFlags & CLASS_Config)) {
                top = top->superClass();
            }
            for (UObject* candidate : objects::all()) {
                UClass* loaded = cast<UClass>(candidate);
                if (loaded && loaded->defaultObject && loaded->isChildOf(top)) {
                    Ini* defaults = forName(configNameOf(loaded));
                    if (defaults) {
                        loadConfigPropagated(loaded->defaultObject, loaded, *defaults);
                    }
                }
            }
        }
    }
}
