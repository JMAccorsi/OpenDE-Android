#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace opende::inno {

struct ExtractProgress {
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;
    std::uint32_t filesDone = 0;
    std::uint32_t filesTotal = 0;
    std::string currentFile;
};

struct ExtractResult {
    bool ok = false;
    std::string error;
    std::string appName;
    std::uint32_t filesExtracted = 0;
    std::uint64_t bytesExtracted = 0;
};

using ProgressCallback = std::function<bool(const ExtractProgress &)>;

// Extracts Deus Ex game files ({app}/System, Maps, Textures, Sounds, Music)
// from a GOG / Inno Setup installer (.exe + optional setup-*.bin slices).
// ProgressCallback may return false to cancel extraction.
ExtractResult extractGameFromInstaller(
    const std::string &installerPath,
    const std::string &outputDir,
    ProgressCallback progressCb = {});

} // namespace opende::inno
