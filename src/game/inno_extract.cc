#include "game/inno_extract.h"

#if defined(__ANDROID__) || defined(OPENDE_HOST_INNO_TEST)

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <lzma.h>
#include <zlib.h>
#ifdef __ANDROID__
#include <android/log.h>
#define INNO_LOG(...) __android_log_print(ANDROID_LOG_INFO, "OpenDE_Inno", __VA_ARGS__)
#else
#define INNO_LOG(...) (void)0
#endif

namespace opende::inno {

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t innoVersion(unsigned a, unsigned b, unsigned c, unsigned d = 0) {
    return (static_cast<std::uint32_t>(a) << 24) |
           (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(c) << 8) |
           static_cast<std::uint32_t>(d);
}

enum CompressionMethod {
    CompUnknown = 0,
    CompStored = 1,
    CompZlib = 2,
    CompBZip2 = 3,
    CompLZMA1 = 4,
    CompLZMA2 = 5
};

enum ExeFilterType {
    FilterNone = 0,
    Filter4108 = 1,
    Filter5200 = 2,
    Filter5309 = 3
};

struct VersionInfo {
    std::uint32_t value = 0;
    bool isUnicode = false;
    bool isISX = false;
    std::uint8_t bits = 32;
};

struct OffsetsInfo {
    std::uint32_t headerOffset = 0;
    std::uint32_t dataOffset = 0;
};

struct HeaderInfo {
    std::string appName;
    std::string defaultDirName;
    std::uint32_t languageCount = 0;
    std::uint32_t messageCount = 0;
    std::uint32_t permissionCount = 0;
    std::uint32_t typeCount = 0;
    std::uint32_t componentCount = 0;
    std::uint32_t taskCount = 0;
    std::uint32_t directoryCount = 0;
    std::uint32_t fileCount = 0;
    std::uint32_t dataEntryCount = 0;
    std::uint32_t slicesPerDisk = 1;
    CompressionMethod compression = CompUnknown;
    bool encryptionUsed = false;
};

struct FileEntry {
    std::string source;
    std::string destination;
    std::string components;
    std::string tasks;
    std::uint32_t location = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t fileType = 0;
    bool uninsNoUninstall = false;
    bool gogZlib = false;
    std::uint32_t gogPartIndex = 0;
    std::uint32_t gogPartCount = 1;
};

struct DataEntry {
    std::uint32_t firstSlice = 0;
    std::uint32_t lastSlice = 0;
    std::uint32_t chunkOffset = 0;
    std::uint64_t chunkCompressedSize = 0;
    CompressionMethod chunkCompression = CompUnknown;
    bool chunkEncrypted = false;
    std::uint64_t fileOffset = 0;
    std::uint64_t fileSize = 0;
    ExeFilterType filter = FilterNone;
};

// --- Little-endian / stream helpers ---

class MemReader {
public:
    MemReader(const std::uint8_t *data, std::size_t size, bool unicode)
        : data_(data), size_(size), pos_(0), unicode_(unicode) {}

    bool eof() const { return pos_ >= size_; }
    std::size_t remaining() const { return size_ - pos_; }

    bool readBytes(void *dst, std::size_t n) {
        if (pos_ + n > size_) {
            return false;
        }
        if (n > 0) {
            std::memcpy(dst, data_ + pos_, n);
            pos_ += n;
        }
        return true;
    }

    bool skip(std::size_t n) {
        if (pos_ + n > size_) {
            return false;
        }
        pos_ += n;
        return true;
    }

    template <typename T>
    bool readVal(T &out) {
        if (pos_ + sizeof(T) > size_) {
            return false;
        }
        std::memcpy(&out, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    std::uint8_t u8() {
        std::uint8_t v = 0;
        if (!readVal(v)) {
            ok_ = false;
        }
        return v;
    }

    std::uint16_t u16() {
        std::uint16_t v = 0;
        if (!readVal(v)) {
            ok_ = false;
        }
        return v;
    }

    std::uint32_t u32() {
        std::uint32_t v = 0;
        if (!readVal(v)) {
            ok_ = false;
        }
        return v;
    }

    std::int32_t i32() {
        std::int32_t v = 0;
        if (!readVal(v)) {
            ok_ = false;
        }
        return v;
    }

    std::uint64_t u64() {
        std::uint64_t v = 0;
        if (!readVal(v)) {
            ok_ = false;
        }
        return v;
    }

    std::int64_t i64() {
        std::int64_t v = 0;
        if (!readVal(v)) {
            ok_ = false;
        }
        return v;
    }

    static std::string utf16LeToUtf8(const std::uint8_t *bytes, std::size_t byteLen) {
        std::string out;
        out.reserve(byteLen);
        std::size_t i = 0;
        while (i + 1 < byteLen) {
            std::uint32_t cp = static_cast<std::uint32_t>(bytes[i]) |
                               (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
            i += 2;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < byteLen) {
                std::uint32_t lo = static_cast<std::uint32_t>(bytes[i]) |
                                   (static_cast<std::uint32_t>(bytes[i + 1]) << 8);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                    i += 2;
                }
            }
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    static std::string ansiToUtf8(const std::uint8_t *bytes, std::size_t byteLen) {
        std::string out;
        out.reserve(byteLen);
        for (std::size_t i = 0; i < byteLen; ++i) {
            std::uint8_t c = bytes[i];
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    std::string binaryString() {
        std::int32_t rawLen = i32();
        if (!ok_ || rawLen <= 0) {
            return {};
        }
        std::size_t len = static_cast<std::size_t>(rawLen);
        if (pos_ + len > size_) {
            ok_ = false;
            return {};
        }
        std::string res = unicode_ ? utf16LeToUtf8(data_ + pos_, len)
                                   : ansiToUtf8(data_ + pos_, len);
        pos_ += len;
        return res;
    }

    void skipBinaryString() {
        std::int32_t rawLen = i32();
        if (!ok_ || rawLen <= 0) {
            return;
        }
        if (!skip(static_cast<std::size_t>(rawLen))) {
            ok_ = false;
        }
    }

    void skipAnsiString() { skipBinaryString(); }

    void skipFlags(std::size_t numFlags) {
        std::size_t bytes = (numFlags + 7) / 8;
        if (bytes == 0) {
            bytes = 1;
        }
        if (bytes == 3) {
            bytes = 4; // Inno Setup pads 3-byte flag sets to 4 bytes on 32-bit
        }
        if (!skip(bytes)) {
            ok_ = false;
        }
    }

    std::uint64_t readFlags(std::size_t numFlags) {
        std::size_t bytes = (numFlags + 7) / 8;
        if (bytes == 0) {
            bytes = 1;
        }
        if (bytes == 3) {
            bytes = 4;
        }
        std::uint64_t mask = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            std::uint8_t b = u8();
            if (!ok_) {
                return 0;
            }
            if (i < 8) {
                mask |= (static_cast<std::uint64_t>(b) << (i * 8));
            }
        }
        return mask;
    }

    bool ok() const { return ok_; }

private:
    const std::uint8_t *data_;
    std::size_t size_;
    std::size_t pos_;
    bool unicode_;
    bool ok_ = true;
};

// --- File helpers ---

std::uint64_t fileTell(std::FILE *f) {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_ftelli64(f));
#else
    return static_cast<std::uint64_t>(ftello(f));
#endif
}

bool fileSeek(std::FILE *f, std::uint64_t offset, int origin = SEEK_SET) {
#if defined(_WIN32)
    return _fseeki64(f, static_cast<__int64>(offset), origin) == 0;
#else
    return fseeko(f, static_cast<off_t>(offset), origin) == 0;
#endif
}

bool readExact(std::FILE *f, void *dst, std::size_t n) {
    return n == 0 || std::fread(dst, 1, n, f) == n;
}

template <typename T>
bool readLE(std::FILE *f, T &out) {
    return readExact(f, &out, sizeof(T));
}

// --- SetupLdrOffsetTable discovery ---

bool findSetupOffsets(std::FILE *f, std::uint64_t fileSize, OffsetsInfo &out) {
    static constexpr std::array<std::uint8_t, 12> kOffsetTableId = {
        'r', 'D', 'l', 'P', 't', 'S', 0xCD, 0xE6, 0xD7, 0x7B, 0x0B, '*'
    };

    // Known SetupLdrOffsetTable offsets for common Inno Setup versions (fast path)
    static constexpr std::uint32_t kKnownOffsets[] = {
        0x0002e36c, 0x0002e42c, 0x0002e800, 0x0002ee00,
        0x00018b00, 0x00019000, 0x0001c800, 0x00020000,
        0x00025000, 0x0002a000, 0x00030000, 0x00037000
    };

    auto tryAt = [&](std::uint64_t pos) -> bool {
        if (pos + 40 > fileSize) {
            return false;
        }
        if (!fileSeek(f, pos)) {
            return false;
        }
        std::array<std::uint8_t, 12> id{};
        if (!readExact(f, id.data(), id.size()) || id != kOffsetTableId) {
            return false;
        }
        std::uint32_t firstField = 0;
        if (!readLE(f, firstField)) {
            return false;
        }
        std::uint32_t totalSize = 0;
        std::uint32_t exeOffset = 0;
        std::uint32_t exeUncompSize = 0;
        std::uint32_t exeChecksum = 0;
        std::uint32_t offset0 = 0;
        std::uint32_t offset1 = 0;
        if (firstField == 1) {
            // Inno Setup >= 5.1.5: revision (1), totalSize, exeOffset, exeUncompSize, exeChecksum, headerOffset, dataOffset
            if (!readLE(f, totalSize) || !readLE(f, exeOffset) ||
                !readLE(f, exeUncompSize) || !readLE(f, exeChecksum) ||
                !readLE(f, offset0) || !readLE(f, offset1)) {
                return false;
            }
        } else {
            // Pre-5.1.5: firstField is totalSize
            totalSize = firstField;
            if (!readLE(f, exeOffset) || !readLE(f, exeUncompSize) ||
                !readLE(f, exeChecksum) || !readLE(f, offset0) || !readLE(f, offset1)) {
                return false;
            }
        }
        (void)totalSize;
        (void)exeOffset;
        (void)exeUncompSize;
        (void)exeChecksum;
        if (offset0 == 0 || static_cast<std::uint64_t>(offset0) >= fileSize) {
            return false;
        }
        out.headerOffset = offset0;
        out.dataOffset = offset1;
        return true;
    };

    for (std::uint32_t known : kKnownOffsets) {
        if (tryAt(known)) {
            return true;
        }
    }

    // Scan the first 8 MiB of the .exe for the 12-byte SetupLdrOffsetTable signature
    const std::uint64_t scanLimit = std::min<std::uint64_t>(fileSize, 8ULL * 1024ULL * 1024ULL);
    std::vector<std::uint8_t> buf(65536 + kOffsetTableId.size());
    std::uint64_t pos = 0;
    while (pos < scanLimit) {
        std::size_t toRead = static_cast<std::size_t>(
            std::min<std::uint64_t>(buf.size(), scanLimit - pos));
        if (!fileSeek(f, pos) || !readExact(f, buf.data(), toRead)) {
            break;
        }
        auto it = std::search(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(toRead),
                              kOffsetTableId.begin(), kOffsetTableId.end());
        if (it != buf.begin() + static_cast<std::ptrdiff_t>(toRead)) {
            std::uint64_t candidate = pos + static_cast<std::uint64_t>(it - buf.begin());
            if (tryAt(candidate)) {
                return true;
            }
        }
        if (toRead <= kOffsetTableId.size()) {
            break;
        }
        pos += toRead - kOffsetTableId.size();
    }
    return false;
}

// --- Version parser ---

bool parseVersionString(std::FILE *f, VersionInfo &ver) {
    char raw[64] = {};
    if (!readExact(f, raw, sizeof(raw))) {
        return false;
    }
    std::size_t len = 0;
    while (len < sizeof(raw) && raw[len] != '\0') {
        ++len;
    }
    std::string s(raw, len);
    if (s.find("Inno Setup") == std::string::npos) {
        return false;
    }

    ver.isISX = (s.find("ISX") != std::string::npos);
    ver.isUnicode = (s.find("(u)") != std::string::npos) ||
                    (s.find("(U)") != std::string::npos);

    std::size_t paren = s.find('(');
    if (paren == std::string::npos) {
        return false;
    }
    unsigned a = 0, b = 0, c = 0, d = 0;
    int parsed = std::sscanf(s.c_str() + paren + 1, "%u.%u.%u.%u", &a, &b, &c, &d);
    if (parsed < 3) {
        return false;
    }
    if (parsed == 3) {
        d = 0;
    }
    ver.value = innoVersion(a, b, c, d);
    if (ver.value >= innoVersion(6, 3, 0)) {
        ver.isUnicode = true;
    }
    return true;
}

// --- Decompressors ---

bool initLzma1Decoder(lzma_stream &strm, const std::uint8_t props[5]) {
    strm = LZMA_STREAM_INIT;
    lzma_filter filters[2] = {};
    filters[0].id = LZMA_FILTER_LZMA1;
    filters[1].id = LZMA_VLI_UNKNOWN;
    if (lzma_properties_decode(&filters[0], nullptr, props, 5) != LZMA_OK) {
        return false;
    }
    lzma_ret ret = lzma_raw_decoder(&strm, filters);
    std::free(filters[0].options);
    return ret == LZMA_OK;
}

bool initLzma2Decoder(lzma_stream &strm, std::uint8_t prop) {
    strm = LZMA_STREAM_INIT;
    lzma_filter filters[2] = {};
    filters[0].id = LZMA_FILTER_LZMA2;
    filters[1].id = LZMA_VLI_UNKNOWN;
    if (lzma_properties_decode(&filters[0], nullptr, &prop, 1) != LZMA_OK) {
        return false;
    }
    lzma_ret ret = lzma_raw_decoder(&strm, filters);
    std::free(filters[0].options);
    return ret == LZMA_OK;
}

bool readSetupBlock(std::FILE *f, const VersionInfo &ver, std::vector<std::uint8_t> &out) {
    std::uint32_t storedCrc = 0;
    std::uint32_t storedSize = 0;
    std::uint8_t compressed = 0;
    if (!readLE(f, storedCrc) || !readLE(f, storedSize) || !readLE(f, compressed)) {
        return false;
    }
    (void)storedCrc;

    // Read 4096-byte CRC32-framed payload
    std::vector<std::uint8_t> raw;
    raw.reserve(storedSize);
    std::uint32_t remaining = storedSize;
    std::array<std::uint8_t, 4096> frame{};
    while (remaining > 0) {
        std::uint32_t frameCrc = 0;
        if (remaining < 4 || !readLE(f, frameCrc)) {
            return false;
        }
        (void)frameCrc;
        remaining -= 4;
        std::uint32_t chunkLen = std::min<std::uint32_t>(remaining, static_cast<std::uint32_t>(frame.size()));
        if (!readExact(f, frame.data(), chunkLen)) {
            return false;
        }
        raw.insert(raw.end(), frame.data(), frame.data() + chunkLen);
        remaining -= chunkLen;
    }

    if (compressed == 0) {
        out = std::move(raw);
        return true;
    }

    if (ver.value >= innoVersion(4, 1, 6)) {
        if (raw.size() < 5) {
            return false;
        }
        lzma_stream strm = LZMA_STREAM_INIT;
        if (!initLzma1Decoder(strm, raw.data())) {
            return false;
        }
        strm.next_in = raw.data() + 5;
        strm.avail_in = raw.size() - 5;

        out.clear();
        std::array<std::uint8_t, 65536> tmp{};
        lzma_ret ret = LZMA_OK;
        while (ret == LZMA_OK) {
            strm.next_out = tmp.data();
            strm.avail_out = tmp.size();
            lzma_action action = (strm.avail_in == 0) ? LZMA_FINISH : LZMA_RUN;
            ret = lzma_code(&strm, action);
            std::size_t produced = tmp.size() - strm.avail_out;
            out.insert(out.end(), tmp.data(), tmp.data() + produced);
            if (ret == LZMA_STREAM_END) {
                break;
            }
            if (ret != LZMA_OK && ret != LZMA_BUF_ERROR) {
                lzma_end(&strm);
                return false;
            }
            if (ret == LZMA_BUF_ERROR && produced == 0 && strm.avail_in == 0) {
                break;
            }
        }
        lzma_end(&strm);
        return true;
    }

    // Zlib fallback (< 4.1.6)
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        return false;
    }
    zs.next_in = raw.data();
    zs.avail_in = static_cast<uInt>(raw.size());
    out.clear();
    std::array<std::uint8_t, 65536> tmp{};
    int zret = Z_OK;
    while (zret == Z_OK) {
        zs.next_out = tmp.data();
        zs.avail_out = static_cast<uInt>(tmp.size());
        zret = inflate(&zs, Z_NO_FLUSH);
        std::size_t produced = tmp.size() - zs.avail_out;
        out.insert(out.end(), tmp.data(), tmp.data() + produced);
        if (zret == Z_STREAM_END) {
            break;
        }
        if (zret != Z_OK) {
            inflateEnd(&zs);
            return false;
        }
    }
    inflateEnd(&zs);
    return true;
}

// --- Setup0 metadata parsers ---

CompressionMethod mapCompression(std::uint8_t idx, std::uint32_t ver) {
    if (ver >= innoVersion(5, 3, 9)) {
        switch (idx) {
            case 0: return CompStored;
            case 1: return CompZlib;
            case 2: return CompBZip2;
            case 3: return CompLZMA1;
            case 4: return CompLZMA2;
            default: return CompUnknown;
        }
    } else if (ver >= innoVersion(4, 2, 6)) {
        switch (idx) {
            case 0: return CompStored;
            case 1: return CompZlib;
            case 2: return CompBZip2;
            case 3: return CompLZMA1;
            default: return CompUnknown;
        }
    } else if (ver >= innoVersion(4, 2, 5)) {
        switch (idx) {
            case 0: return CompStored;
            case 1: return CompBZip2;
            case 2: return CompLZMA1;
            default: return CompUnknown;
        }
    } else if (ver >= innoVersion(4, 1, 5)) {
        switch (idx) {
            case 0: return CompZlib;
            case 1: return CompBZip2;
            case 2: return CompLZMA1;
            default: return CompUnknown;
        }
    }
    return CompZlib;
}

bool skipWindowsVersionRange(MemReader &r, std::uint32_t ver) {
    auto skipWinVer = [&]() {
        if (ver >= innoVersion(1, 3, 19)) {
            r.u32(); // win_version
            r.u32(); // nt_version
            r.u16(); // nt_service_pack
        } else {
            r.u16();
            r.u16();
            r.u16();
            r.u16();
        }
    };
    skipWinVer();
    skipWinVer();
    return r.ok();
}

std::size_t countHeaderFlags(const VersionInfo &v) {
    std::uint32_t ver = v.value;
    std::size_t n = 0;
    ++n; // DisableStartupPrompt
    if (ver < innoVersion(5, 3, 10)) ++n;
    ++n; // CreateAppDir
    if (ver < innoVersion(5, 3, 3)) ++n;
    if (ver < innoVersion(1, 3, 6)) ++n;
    if (ver < innoVersion(5, 3, 3)) ++n;
    ++n; // AllowNoIcons
    if (ver < innoVersion(3, 0, 0) || ver >= innoVersion(3, 0, 3)) ++n;
    if (ver < innoVersion(1, 3, 3)) ++n;
    ++n; // AlwaysUsePersonalGroup
    if (ver < innoVersion(6, 4, 0, 1)) n += 4;
    ++n; // EnableDirDoesntExistWarning
    if (ver < innoVersion(4, 1, 2)) ++n;
    ++n; // Password
    if (ver >= innoVersion(1, 2, 6)) ++n;
    if (ver >= innoVersion(1, 2, 14)) ++n;
    if (v.bits != 16) {
        if (ver < innoVersion(3, 0, 4)) ++n;
        if (ver < innoVersion(3, 0, 0)) ++n;
        if (ver < innoVersion(1, 3, 6)) ++n;
        if (ver < innoVersion(5, 6, 1)) ++n;
    }
    if (ver >= innoVersion(1, 3, 0) && ver < innoVersion(5, 3, 8)) ++n;
    if (ver >= innoVersion(1, 3, 1)) ++n;
    if (ver >= innoVersion(1, 3, 3) && ver < innoVersion(6, 4, 0, 1)) ++n;
    if (ver >= innoVersion(1, 3, 10)) ++n;
    if (ver >= innoVersion(1, 3, 20)) ++n;
    if (ver >= innoVersion(2, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 10))) ++n;
    if (ver >= innoVersion(2, 0, 0)) n += 6;
    if (ver >= innoVersion(2, 0, 7)) n += 2;
    if (ver >= innoVersion(2, 0, 17) && ver < innoVersion(4, 1, 5)) ++n;
    if (ver >= innoVersion(2, 0, 18)) ++n;
    if (ver >= innoVersion(3, 0, 0)) n += 2;
    if (ver >= innoVersion(3, 0, 1)) ++n;
    if (ver >= innoVersion(3, 0, 3)) ++n;
    if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(3, 0, 3))) ++n;
    if (ver >= innoVersion(4, 0, 0) && ver < innoVersion(4, 0, 10)) ++n;
    if (ver >= innoVersion(4, 0, 1) && ver < innoVersion(4, 0, 10)) ++n;
    if (ver >= innoVersion(4, 0, 9)) ++n;
    if (ver >= innoVersion(4, 1, 3)) ++n;
    if (ver >= innoVersion(4, 1, 8)) n += 2;
    if (ver >= innoVersion(4, 2, 2)) ++n; // EncryptionUsed
    if (ver >= innoVersion(5, 0, 4) && ver < innoVersion(5, 6, 1)) ++n;
    if (ver >= innoVersion(5, 1, 7) && !v.isUnicode) ++n;
    if (ver >= innoVersion(5, 1, 13)) ++n;
    if (ver >= innoVersion(5, 2, 1)) ++n;
    if (ver >= innoVersion(5, 3, 8)) ++n;
    if (ver >= innoVersion(5, 3, 9)) ++n;
    if (ver >= innoVersion(5, 5, 0)) n += 3;
    if (ver >= innoVersion(5, 5, 7)) ++n;
    if (ver >= innoVersion(6, 0, 0)) n += 3;
    if (ver >= innoVersion(6, 3, 0)) ++n;
    return n;
}

bool parseHeader(MemReader &r, const VersionInfo &v, HeaderInfo &h) {
    std::uint32_t ver = v.value;
    h.appName = r.binaryString();
    r.skipBinaryString(); // app_versioned_name
    if (ver >= innoVersion(1, 3, 0)) r.skipBinaryString(); // app_id
    r.skipBinaryString(); // app_copyright
    if (ver >= innoVersion(1, 3, 0)) {
        r.skipBinaryString(); // app_publisher
        r.skipBinaryString(); // app_publisher_url
    }
    if (ver >= innoVersion(5, 1, 13)) r.skipBinaryString(); // app_support_phone
    if (ver >= innoVersion(1, 3, 0)) {
        r.skipBinaryString(); // app_support_url
        r.skipBinaryString(); // app_updates_url
        r.skipBinaryString(); // app_version
    }
    h.defaultDirName = r.binaryString();
    r.skipBinaryString(); // default_group_name
    if (ver < innoVersion(3, 0, 0)) r.skipAnsiString(); // uninstall_icon_name
    r.skipBinaryString(); // base_filename
    if (ver >= innoVersion(1, 3, 0) && ver < innoVersion(5, 2, 5)) {
        r.skipAnsiString();
        r.skipAnsiString();
        r.skipAnsiString();
    }
    if (ver >= innoVersion(1, 3, 3)) r.skipBinaryString(); // uninstall_files_dir
    if (ver >= innoVersion(1, 3, 6)) {
        r.skipBinaryString(); // uninstall_name
        r.skipBinaryString(); // uninstall_icon
    }
    if (ver >= innoVersion(1, 3, 14)) r.skipBinaryString(); // app_mutex
    if (ver >= innoVersion(3, 0, 0)) {
        r.skipBinaryString(); // default_user_name
        r.skipBinaryString(); // default_user_organisation
    }
    if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(3, 0, 6, 1))) {
        r.skipBinaryString(); // default_serial
    }
    if ((ver >= innoVersion(4, 0, 0) && ver < innoVersion(5, 2, 5)) ||
        (v.isISX && ver >= innoVersion(1, 3, 24))) {
        r.skipBinaryString(); // compiled_code
    }
    if (ver >= innoVersion(4, 2, 4)) {
        r.skipBinaryString(); // app_readme_file
        r.skipBinaryString(); // app_contact
        r.skipBinaryString(); // app_comments
        r.skipBinaryString(); // app_modify_path
    }
    if (ver >= innoVersion(5, 3, 8)) r.skipBinaryString(); // create_uninstall_registry_key
    if (ver >= innoVersion(5, 3, 10)) r.skipBinaryString(); // uninstallable
    if (ver >= innoVersion(5, 5, 0)) r.skipBinaryString(); // close_applications_filter
    if (ver >= innoVersion(5, 5, 6)) r.skipBinaryString(); // setup_mutex
    if (ver >= innoVersion(5, 6, 1)) {
        r.skipBinaryString(); // changes_environment
        r.skipBinaryString(); // changes_associations
    }
    if (ver >= innoVersion(6, 3, 0)) {
        r.skipBinaryString(); // architectures_allowed_expr
        r.skipBinaryString(); // architectures_installed_in_64bit_mode_expr
    }
    if (ver >= innoVersion(5, 2, 5)) {
        r.skipAnsiString(); // license_text
        r.skipAnsiString(); // info_before
        r.skipAnsiString(); // info_after
    }
    if (ver >= innoVersion(5, 2, 1) && ver < innoVersion(5, 3, 10)) {
        r.skipBinaryString(); // uninstaller_signature
    }
    if (ver >= innoVersion(5, 2, 5)) {
        r.skipBinaryString(); // compiled_code
    }
    if (ver >= innoVersion(2, 0, 6) && !v.isUnicode) {
        r.skip(32); // lead_bytes (char set = 256 bits = 32 bytes)
    }

    h.languageCount = (ver >= innoVersion(4, 0, 0)) ? r.u32()
                    : (ver >= innoVersion(2, 0, 1)) ? 1u : 0u;
    h.messageCount = (ver >= innoVersion(4, 2, 1)) ? r.u32() : 0u;
    h.permissionCount = (ver >= innoVersion(4, 1, 0)) ? r.u32() : 0u;
    if (ver >= innoVersion(2, 0, 0) || v.isISX) {
        h.typeCount = r.u32();
        h.componentCount = r.u32();
    }
    if (ver >= innoVersion(2, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 17))) {
        h.taskCount = r.u32();
    }
    h.directoryCount = r.u32();
    h.fileCount = r.u32();
    h.dataEntryCount = r.u32();
    r.u32(); // icon_count
    r.u32(); // ini_entry_count
    r.u32(); // registry_entry_count
    r.u32(); // delete_entry_count
    r.u32(); // uninstall_delete_entry_count
    r.u32(); // run_entry_count
    r.u32(); // uninstall_run_entry_count

    if (!skipWindowsVersionRange(r, ver)) return false;

    if (ver < innoVersion(6, 4, 0, 1)) r.u32(); // back_color
    if (ver >= innoVersion(1, 3, 3) && ver < innoVersion(6, 4, 0, 1)) r.u32(); // back_color2
    if (ver < innoVersion(5, 5, 7)) r.u32(); // image_back_color
    if ((ver >= innoVersion(2, 0, 0) && ver < innoVersion(5, 0, 4)) || v.isISX) {
        r.u32(); // small_image_back_color
    }
    if (ver >= innoVersion(6, 0, 0)) {
        r.u8(); // wizard_style
        r.u32(); // wizard_resize_percent_x
        r.u32(); // wizard_resize_percent_y
    }
    if (ver >= innoVersion(5, 5, 7)) {
        r.u8(); // image_alpha_format
    }
    if (ver >= innoVersion(6, 4, 0)) {
        r.skip(4 + 44);
    } else if (ver >= innoVersion(5, 3, 9)) {
        r.skip(20 + 8); // sha1 + 8-byte salt
    } else if (ver >= innoVersion(4, 2, 2)) {
        r.skip(16 + 8); // md5 + 8-byte salt
    } else if (ver >= innoVersion(4, 2, 0)) {
        r.skip(16);
    } else {
        r.u32();
    }

    if (ver >= innoVersion(4, 0, 0)) {
        r.i64(); // extra_disk_space_required
        h.slicesPerDisk = r.u32();
    } else {
        r.i32();
        h.slicesPerDisk = 1;
    }

    if ((ver >= innoVersion(2, 0, 0) && ver < innoVersion(5, 0, 0)) ||
        (v.isISX && ver >= innoVersion(1, 3, 4))) {
        r.u8(); // install_mode
    }
    if (ver >= innoVersion(1, 3, 0)) r.u8(); // uninstall_log_mode
    if (ver < innoVersion(5, 0, 0) && (ver >= innoVersion(2, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 13)))) {
        r.u8(); // uninstall_style
    }
    if (ver >= innoVersion(1, 3, 6)) r.u8(); // dir_exists_warning
    if (v.isISX && ver >= innoVersion(2, 0, 10) && ver < innoVersion(3, 0, 0)) r.i32();
    if (ver >= innoVersion(3, 0, 0) && ver < innoVersion(3, 0, 3)) r.u8();
    if (ver >= innoVersion(3, 0, 4) || (v.isISX && ver >= innoVersion(3, 0, 3))) {
        r.u8(); // privileges_required
    }
    if (ver >= innoVersion(5, 7, 0)) r.u8(); // privileges_required_override_allowed
    if (ver >= innoVersion(4, 0, 10)) {
        r.u8(); // show_language_dialog
        r.u8(); // language_detection
    }
    if (ver >= innoVersion(4, 1, 5)) {
        h.compression = mapCompression(r.u8(), ver);
    } else {
        h.compression = CompZlib;
    }
    if (ver >= innoVersion(5, 1, 0) && ver < innoVersion(6, 3, 0)) {
        r.u8(); // architectures_allowed
        r.u8(); // architectures_installed_in_64bit_mode
    }
    if (ver >= innoVersion(5, 2, 1) && ver < innoVersion(5, 3, 10)) {
        r.u32();
        r.u32();
    }
    if (ver >= innoVersion(5, 3, 3)) {
        r.u8(); // disable_dir_page
        r.u8(); // disable_program_group_page
    }
    if (ver >= innoVersion(5, 5, 0)) {
        r.u64(); // uninstall_display_size
    } else if (ver >= innoVersion(5, 3, 6)) {
        r.u32();
    }
    if (ver == innoVersion(5, 3, 10, 1) || ver == innoVersion(5, 4, 2, 1) ||
        ver == innoVersion(5, 5, 0, 1)) {
        r.u8();
    }

    r.skipFlags(countHeaderFlags(v));
    return r.ok();
}

bool skipLanguages(MemReader &r, const VersionInfo &v, std::uint32_t count) {
    std::uint32_t ver = v.value;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (ver >= innoVersion(4, 0, 0)) r.skipBinaryString(); // name
        r.skipBinaryString(); // language_name
        r.skipBinaryString(); // dialog_font
        r.skipBinaryString(); // title_font
        r.skipBinaryString(); // welcome_font
        r.skipBinaryString(); // copyright_font
        if (ver >= innoVersion(4, 0, 0)) r.skipBinaryString(); // data
        if (ver >= innoVersion(4, 0, 1)) {
            r.skipBinaryString(); // license_text
            r.skipBinaryString(); // info_before
            r.skipBinaryString(); // info_after
        }
        r.u32(); // language_id
        if (ver >= innoVersion(4, 2, 2) && ver < innoVersion(5, 3, 0)) {
            r.u32(); // codepage
        } else if (ver >= innoVersion(5, 3, 0) && !v.isUnicode) {
            r.u32(); // codepage
        }
        r.u32(); // dialog_font_size
        if (ver < innoVersion(4, 1, 0)) r.u32();
        r.u32(); // title_font_size
        r.u32(); // welcome_font_size
        r.u32(); // copyright_font_size
        if (ver >= innoVersion(5, 2, 3)) r.u8(); // right_to_left
        if (!r.ok()) return false;
    }
    return true;
}

bool skipMessages(MemReader &r, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        r.skipBinaryString(); // name
        r.skipBinaryString(); // value
        r.u32(); // lang_index
        if (!r.ok()) return false;
    }
    return true;
}

bool skipPermissions(MemReader &r, std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        r.skipBinaryString();
        if (!r.ok()) return false;
    }
    return true;
}

bool skipTypes(MemReader &r, const VersionInfo &v, std::uint32_t count) {
    std::uint32_t ver = v.value;
    for (std::uint32_t i = 0; i < count; ++i) {
        r.skipBinaryString(); // name
        r.skipBinaryString(); // description
        if (ver >= innoVersion(4, 0, 1)) r.skipBinaryString(); // languages
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 24))) {
            r.skipBinaryString(); // check
        }
        skipWindowsVersionRange(r, ver);
        r.skipFlags(1); // CustomType
        r.u8(); // setup_type
        if (ver >= innoVersion(4, 0, 0)) {
            r.u64(); // size
        } else {
            r.u32();
        }
        if (!r.ok()) return false;
    }
    return true;
}

bool skipComponents(MemReader &r, const VersionInfo &v, std::uint32_t count) {
    std::uint32_t ver = v.value;
    for (std::uint32_t i = 0; i < count; ++i) {
        r.skipBinaryString(); // name
        r.skipBinaryString(); // description
        r.skipBinaryString(); // types
        if (ver >= innoVersion(4, 0, 1)) r.skipBinaryString(); // languages
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 24))) {
            r.skipBinaryString(); // check
        }
        if (ver >= innoVersion(4, 0, 0)) {
            r.u64(); // extra_disk_space_required
        } else {
            r.u32();
        }
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(3, 0, 3))) {
            r.u32(); // level
            r.u8();  // used
        }
        skipWindowsVersionRange(r, ver);
        std::size_t nFlags = 3; // Fixed, Restart, DisableNoUninstallWarning
        if (ver >= innoVersion(4, 2, 3)) ++nFlags; // Exclusive
        r.skipFlags(nFlags);
        if (ver >= innoVersion(4, 0, 0)) {
            r.u64(); // size
        } else {
            r.u32();
        }
        if (!r.ok()) return false;
    }
    return true;
}

bool skipTasks(MemReader &r, const VersionInfo &v, std::uint32_t count) {
    std::uint32_t ver = v.value;
    for (std::uint32_t i = 0; i < count; ++i) {
        r.skipBinaryString(); // name
        r.skipBinaryString(); // description
        r.skipBinaryString(); // group_description
        r.skipBinaryString(); // components
        if (ver >= innoVersion(4, 0, 1)) r.skipBinaryString(); // languages
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 24))) {
            r.skipBinaryString(); // check
        }
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(3, 0, 3))) {
            r.u32(); // level
            r.u8();  // used
        }
        skipWindowsVersionRange(r, ver);
        std::size_t nFlags = 1; // Exclusive
        if (ver >= innoVersion(2, 0, 5)) ++nFlags; // Unchecked
        if (ver >= innoVersion(2, 0, 6)) ++nFlags; // Restart
        if (ver >= innoVersion(2, 0, 17)) ++nFlags; // CheckedOnce
        if (ver >= innoVersion(4, 2, 3)) ++nFlags; // DontInheritCheck
        r.skipFlags(nFlags);
        if (!r.ok()) return false;
    }
    return true;
}

bool skipDirectories(MemReader &r, const VersionInfo &v, std::uint32_t count) {
    std::uint32_t ver = v.value;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (ver < innoVersion(1, 3, 0)) r.u32(); // uncompressed_size
        r.skipBinaryString(); // name
        // load_condition_data
        if (ver >= innoVersion(2, 0, 0) || v.isISX) r.skipBinaryString(); // components
        if (ver >= innoVersion(2, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 17))) {
            r.skipBinaryString(); // tasks
        }
        if (ver >= innoVersion(4, 0, 1)) r.skipBinaryString(); // languages
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 24))) {
            r.skipBinaryString(); // check
        }
        if (ver >= innoVersion(4, 1, 0)) {
            r.skipBinaryString(); // after_install
            r.skipBinaryString(); // before_install
        }
        if (ver >= innoVersion(4, 0, 11) && ver < innoVersion(4, 1, 0)) {
            r.skipBinaryString(); // permissions
        }
        if (ver >= innoVersion(2, 0, 11)) r.u32(); // attributes
        skipWindowsVersionRange(r, ver);
        if (ver >= innoVersion(4, 1, 0)) r.u16(); // permission (int16)
        std::size_t nFlags = (ver >= innoVersion(5, 2, 0)) ? 5u : 3u;
        r.skipFlags(nFlags);
        if (!r.ok()) return false;
    }
    return true;
}

static std::string extractGogTempDest(const std::string &scriptCall, std::uint32_t &partCountOut) {
    // Matches GOG Inno Setup calls like:
    // before_install('f0eb60a99b994e4281eb6d3fcb467805', 'Maps\00_Intro.dx', 1)
    partCountOut = 1;
    std::size_t q1 = scriptCall.find('\'');
    if (q1 == std::string::npos) return {};
    std::size_t q2 = scriptCall.find('\'', q1 + 1);
    if (q2 == std::string::npos) return {};
    std::size_t q3 = scriptCall.find('\'', q2 + 1);
    if (q3 == std::string::npos) return {};
    std::size_t q4 = scriptCall.find('\'', q3 + 1);
    if (q4 == std::string::npos || q4 <= q3 + 1) return {};
    std::size_t comma = scriptCall.find(',', q4 + 1);
    if (comma != std::string::npos) {
        std::size_t p = comma + 1;
        while (p < scriptCall.size() && (scriptCall[p] == ' ' || scriptCall[p] == '\t')) ++p;
        std::uint32_t val = 0;
        while (p < scriptCall.size() && std::isdigit(static_cast<unsigned char>(scriptCall[p]))) {
            val = val * 10 + static_cast<std::uint32_t>(scriptCall[p] - '0');
            ++p;
        }
        if (val > 1) {
            partCountOut = val;
        }
    }
    return "{app}/" + scriptCall.substr(q3 + 1, q4 - q3 - 1);
}

bool parseFiles(MemReader &r, const VersionInfo &v, std::uint32_t count,
                std::vector<FileEntry> &files) {
    std::uint32_t ver = v.value;
    files.resize(count);
    std::string pendingGogDest;
    std::uint32_t pendingGogPartsRemaining = 0;
    std::uint32_t pendingGogNextPart = 0;
    std::uint32_t pendingGogTotalParts = 1;
    for (std::uint32_t i = 0; i < count; ++i) {
        FileEntry &fe = files[i];
        if (ver < innoVersion(1, 3, 0)) r.u32();
        fe.source = r.binaryString();
        fe.destination = r.binaryString();
        r.skipBinaryString(); // install_font_name
        if (ver >= innoVersion(5, 2, 5)) r.skipBinaryString(); // strong_assembly_name
        // load_condition_data
        if (ver >= innoVersion(2, 0, 0) || v.isISX) fe.components = r.binaryString();
        if (ver >= innoVersion(2, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 17))) {
            fe.tasks = r.binaryString();
        }
        if (ver >= innoVersion(4, 0, 1)) r.skipBinaryString(); // languages
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(1, 3, 24))) {
            r.skipBinaryString(); // check
        }
        std::string afterInstall;
        std::string beforeInstall;
        if (ver >= innoVersion(4, 1, 0)) {
            afterInstall = r.binaryString(); // after_install
            beforeInstall = r.binaryString(); // before_install
        }
        if (fe.destination.size() >= 6) {
            std::string pfx = fe.destination.substr(0, 5);
            for (char &c : pfx) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (pfx == "{tmp}") {
                std::uint32_t partCount = 1;
                std::string gogDest = extractGogTempDest(beforeInstall, partCount);
                if (gogDest.empty()) {
                    gogDest = extractGogTempDest(afterInstall, partCount);
                }
                if (!gogDest.empty()) {
                    fe.destination = gogDest;
                    fe.gogZlib = true;
                    fe.gogPartIndex = 0;
                    fe.gogPartCount = partCount;
                    if (partCount > 1) {
                        pendingGogDest = std::move(gogDest);
                        pendingGogPartsRemaining = partCount - 1;
                        pendingGogNextPart = 1;
                        pendingGogTotalParts = partCount;
                    } else {
                        pendingGogPartsRemaining = 0;
                    }
                } else if (pendingGogPartsRemaining > 0 && beforeInstall.empty() && !afterInstall.empty()) {
                    fe.destination = pendingGogDest;
                    fe.gogZlib = true;
                    fe.gogPartIndex = pendingGogNextPart++;
                    fe.gogPartCount = pendingGogTotalParts;
                    --pendingGogPartsRemaining;
                }
            }
        }

        skipWindowsVersionRange(r, ver);

        fe.location = r.u32();
        r.u32(); // attributes
        if (ver >= innoVersion(4, 0, 0)) {
            r.u64(); // external_size
        } else {
            r.u32();
        }
        if (ver < innoVersion(3, 0, 5)) {
            r.u8(); // copy_mode
        }
        if (ver >= innoVersion(4, 1, 0)) r.u16(); // permission (int16)

        // File flags (exact count from setup/file.cpp)
        std::size_t nFlags = 4; // ConfirmOverwrite, NeverUninstall, RestartReplace, DeleteAfterInstall
        if (v.bits != 16) nFlags += 3; // RegisterServer, RegisterTypeLib, SharedFile
        if (ver < innoVersion(2, 0, 0) && !v.isISX) ++nFlags; // IsReadmeFile
        nFlags += 2; // CompareTimeStamp, FontIsNotTrueType
        if (ver >= innoVersion(1, 2, 5)) ++nFlags;
        if (ver >= innoVersion(1, 2, 6)) ++nFlags;
        if (ver >= innoVersion(1, 3, 21)) nFlags += 2;
        if (ver >= innoVersion(1, 3, 25)) ++nFlags;
        if (ver >= innoVersion(2, 0, 5)) ++nFlags;
        if (ver >= innoVersion(3, 0, 1)) ++nFlags;
        if (ver >= innoVersion(3, 0, 5)) nFlags += 3;
        if (ver >= innoVersion(4, 0, 0) || (v.isISX && ver >= innoVersion(3, 0, 6, 1))) ++nFlags;
        if (ver >= innoVersion(4, 0, 5)) ++nFlags;
        if (ver >= innoVersion(4, 1, 8)) ++nFlags;
        if (ver >= innoVersion(4, 2, 1)) ++nFlags;
        if (ver >= innoVersion(4, 2, 5)) ++nFlags;
        if (ver >= innoVersion(5, 0, 3)) ++nFlags;
        if (ver >= innoVersion(5, 1, 0)) ++nFlags;
        if (ver >= innoVersion(5, 1, 2)) nFlags += 2;
        if (ver >= innoVersion(5, 2, 0)) nFlags += 3;
        if (ver >= innoVersion(5, 2, 5)) ++nFlags;
        r.skipFlags(nFlags);

        fe.fileType = r.u8(); // file_type
        if (!r.ok()) return false;
    }
    return true;
}

bool parseDataEntries(MemReader &r, const VersionInfo &v, const HeaderInfo &h,
                      std::uint32_t count, std::vector<DataEntry> &entries) {
    std::uint32_t ver = v.value;
    entries.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        DataEntry &de = entries[i];
        de.firstSlice = r.u32();
        de.lastSlice = r.u32();
        if (ver < innoVersion(4, 0, 0)) {
            de.firstSlice -= 1;
            de.lastSlice -= 1;
        }
        de.chunkOffset = r.u32();
        if (ver >= innoVersion(4, 0, 1)) {
            de.fileOffset = r.u64();
        } else {
            de.fileOffset = r.u32();
        }
        if (ver >= innoVersion(4, 0, 0)) {
            de.fileSize = r.u64();
            de.chunkCompressedSize = r.u64();
        } else {
            de.fileSize = r.u32();
            de.chunkCompressedSize = r.u32();
        }
        if (ver >= innoVersion(6, 4, 0)) {
            r.skip(32); // sha256
        } else if (ver >= innoVersion(5, 3, 9)) {
            r.skip(20); // sha1
        } else if (ver >= innoVersion(4, 2, 0)) {
            r.skip(16); // md5
        } else {
            r.u32(); // adler32/crc32
        }
        r.u64(); // timestamp (FILETIME)
        r.u32(); // file_version_ms
        r.u32(); // file_version_ls

        // DataEntry flags (bit order matches setup/data.cpp)
        std::size_t nFlags = 5;
        if (ver >= innoVersion(4, 0, 1)) nFlags += 2;
        if (ver >= innoVersion(4, 0, 10)) ++nFlags;
        if (ver >= innoVersion(4, 1, 6)) ++nFlags;
        if (ver >= innoVersion(5, 1, 7)) ++nFlags;
        if (ver >= innoVersion(5, 3, 9)) ++nFlags;
        if (ver >= innoVersion(5, 5, 7)) ++nFlags;

        std::uint64_t flags = r.readFlags(nFlags);
        if (!r.ok()) return false;

        // Decode bit positions matching setup/data.cpp:
        // Bit 0: VersionInfoValid
        // Bit 1: VersionInfoNotValid
        // Bit 2: TimeStampInUTC (if >= 4.0.1? wait: let's check exact bit index!)
        std::size_t bit = 0;
        ++bit; // VersionInfoValid
        ++bit; // VersionInfoNotValid
        if (ver >= innoVersion(4, 0, 1)) ++bit; // TimeStampInUTC
        if (ver >= innoVersion(4, 0, 1)) ++bit; // IsUninstallerExe
        bool callOpt = (flags & (1ULL << bit++)) != 0; // CallInstructionOptimized
        if (ver >= innoVersion(4, 0, 10)) ++bit; // Touch
        bool chunkEncrypted = (flags & (1ULL << bit++)) != 0; // ChunkEncrypted
        bool chunkCompressed = (flags & (1ULL << bit++)) != 0; // ChunkCompressed
        if (ver >= innoVersion(5, 1, 7)) ++bit; // ChunkBzipped / SolidBreak
        (void)bit;

        de.chunkEncrypted = chunkEncrypted;
        de.chunkCompression = chunkCompressed ? h.compression : CompStored;
        if (callOpt) {
            if (ver < innoVersion(5, 2, 0)) {
                de.filter = Filter4108;
            } else if (ver < innoVersion(5, 3, 9)) {
                de.filter = Filter5200;
            } else {
                de.filter = Filter5309;
            }
        } else {
            de.filter = FilterNone;
        }
    }
    return true;
}

// --- Path helpers ---

std::string toLowerAscii(std::string s) {
    for (char &ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool extractGameRelativePath(const std::string &dest, std::string &relOut) {
    if (dest.size() < 6) {
        return false;
    }
    std::string prefix = toLowerAscii(dest.substr(0, 6));
    if (prefix != "{app}\\" && prefix != "{app}/") {
        return false;
    }
    std::string rel = dest.substr(6);
    std::replace(rel.begin(), rel.end(), '\\', '/');
    while (!rel.empty() && rel.front() == '/') {
        rel.erase(rel.begin());
    }
    if (rel.empty() || rel.find("..") != std::string::npos) {
        return false;
    }

    std::size_t slash = rel.find('/');
    if (slash == std::string::npos) {
        return false;
    }
    std::string top = toLowerAscii(rel.substr(0, slash));
    if (top != "system" && top != "maps" && top != "textures" &&
        top != "sounds" && top != "music") {
        return false;
    }

    // Skip Windows executables/DLLs inside System/ to save space on Android
    std::string lowerRel = toLowerAscii(rel);
    auto endsWith = [&](const char *ext) {
        std::size_t elen = std::strlen(ext);
        return lowerRel.size() >= elen &&
               lowerRel.compare(lowerRel.size() - elen, elen, ext) == 0;
    };
    if (endsWith(".exe") || endsWith(".dll") || endsWith(".bat")) {
        return false;
    }

    relOut = std::move(rel);
    return true;
}

// --- Slice reader for single-file (.exe) and multi-file (setup-*.bin) ---

class SliceStream {
public:
    SliceStream(const fs::path &installerPath, std::FILE *exeFile,
                std::uint64_t exeSize, std::uint32_t dataOffset,
                std::uint32_t slicesPerDisk)
        : installerDir_(installerPath.parent_path()),
          installerStem_(installerPath.stem().string()),
          exeFile_(exeFile),
          exeSize_(exeSize),
          dataOffset_(dataOffset),
          slicesPerDisk_(std::max(1u, slicesPerDisk)) {}

    ~SliceStream() {
        if (extFile_ != nullptr) {
            std::fclose(extFile_);
        }
    }

    bool seekChunk(std::uint32_t slice, std::uint32_t chunkOffset) {
        if (!openSlice(slice)) {
            return false;
        }
        std::uint64_t absOffset = (dataOffset_ != 0)
            ? (static_cast<std::uint64_t>(dataOffset_) + chunkOffset)
            : static_cast<std::uint64_t>(chunkOffset);
        return fileSeek(currentFile_, absOffset);
    }

    bool readChunkBytes(std::uint8_t *dst, std::size_t want, std::size_t &got) {
        got = 0;
        while (got < want) {
            std::uint64_t curPos = fileTell(currentFile_);
            if (curPos >= currentSliceEnd_) {
                if (!openSlice(currentSlice_ + 1)) {
                    return got > 0;
                }
                curPos = fileTell(currentFile_);
            }
            std::uint64_t avail = currentSliceEnd_ - curPos;
            std::size_t n = static_cast<std::size_t>(
                std::min<std::uint64_t>(want - got, avail));
            std::size_t r = std::fread(dst + got, 1, n, currentFile_);
            got += r;
            if (r == 0) {
                return got > 0;
            }
        }
        return true;
    }

    std::FILE *file() const { return currentFile_; }

private:
    bool openSlice(std::uint32_t slice) {
        if (currentFile_ != nullptr && currentSlice_ == slice) {
            return true;
        }
        if (dataOffset_ != 0) {
            if (slice != 0) {
                return false;
            }
            currentSlice_ = 0;
            currentFile_ = exeFile_;
            currentSliceEnd_ = exeSize_;
            return true;
        }

        if (extFile_ != nullptr) {
            std::fclose(extFile_);
            extFile_ = nullptr;
        }

        std::ostringstream oss;
        oss << installerStem_ << '-';
        if (slicesPerDisk_ == 1) {
            oss << (slice + 1);
        } else {
            std::uint32_t major = (slice / slicesPerDisk_) + 1;
            std::uint32_t minor = slice % slicesPerDisk_;
            oss << major << static_cast<char>('a' + static_cast<int>(minor));
        }
        oss << ".bin";
        std::string targetName = oss.str();

        fs::path candidate = installerDir_ / targetName;
        if (!fs::exists(candidate)) {
            std::string lowerTarget = toLowerAscii(targetName);
            std::error_code ec;
            for (const auto &entry : fs::directory_iterator(installerDir_, ec)) {
                if (toLowerAscii(entry.path().filename().string()) == lowerTarget) {
                    candidate = entry.path();
                    break;
                }
            }
        }

        extFile_ = std::fopen(candidate.string().c_str(), "rb");
        if (extFile_ == nullptr) {
            return false;
        }
        char magic[8] = {};
        std::uint32_t sliceSize = 0;
        if (!readExact(extFile_, magic, 8) || !readLE(extFile_, sliceSize)) {
            std::fclose(extFile_);
            extFile_ = nullptr;
            return false;
        }
        if (std::memcmp(magic, "idska16\x1a", 8) != 0 &&
            std::memcmp(magic, "idska32\x1a", 8) != 0) {
            std::fclose(extFile_);
            extFile_ = nullptr;
            return false;
        }
        currentSlice_ = slice;
        currentFile_ = extFile_;
        currentSliceEnd_ = sliceSize;
        return true;
    }

    fs::path installerDir_;
    std::string installerStem_;
    std::FILE *exeFile_;
    std::uint64_t exeSize_;
    std::uint32_t dataOffset_;
    std::uint32_t slicesPerDisk_;

    std::FILE *extFile_ = nullptr;
    std::FILE *currentFile_ = nullptr;
    std::uint32_t currentSlice_ = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t currentSliceEnd_ = 0;
};

struct ExtractTarget {
    std::string relPath;
    std::uint64_t fileOffset = 0;
    std::uint64_t fileSize = 0;
    bool gogZlib = false;
    std::uint32_t gogPartIndex = 0;
    std::uint32_t gogPartCount = 1;
};

} // namespace

ExtractResult extractGameFromInstaller(
    const std::string &installerPath,
    const std::string &outputDir,
    ProgressCallback progressCb) {

    ExtractResult result;

    std::FILE *exeFile = std::fopen(installerPath.c_str(), "rb");
    if (exeFile == nullptr) {
        result.error = "Nao foi possivel abrir o instalador.";
        return result;
    }
    auto closeExe = [&]() {
        if (exeFile != nullptr) {
            std::fclose(exeFile);
            exeFile = nullptr;
        }
    };

    if (!fileSeek(exeFile, 0, SEEK_END)) {
        closeExe();
        result.error = "Erro ao ler tamanho do instalador.";
        return result;
    }
    std::uint64_t exeSize = fileTell(exeFile);

    OffsetsInfo offsets;
    if (!findSetupOffsets(exeFile, exeSize, offsets)) {
        closeExe();
        result.error = "Assinatura Inno Setup nao encontrada neste .exe.";
        return result;
    }

    if (!fileSeek(exeFile, offsets.headerOffset)) {
        closeExe();
        result.error = "Falha ao posicionar no cabecalho Inno Setup.";
        return result;
    }

    VersionInfo ver;
    if (!parseVersionString(exeFile, ver)) {
        closeExe();
        result.error = "Versao do Inno Setup nao reconhecida.";
        return result;
    }
    if (ver.value < innoVersion(4, 0, 0)) {
        closeExe();
        result.error = "Versao do Inno Setup muito antiga (< 4.0).";
        return result;
    }

    std::vector<std::uint8_t> block0;
    if (!readSetupBlock(exeFile, ver, block0)) {
        closeExe();
        result.error = "Falha ao descomprimir bloco de metadados (Setup0).";
        return result;
    }

    std::vector<std::uint8_t> block1;
    if (!readSetupBlock(exeFile, ver, block1)) {
        closeExe();
        result.error = "Falha ao descomprimir bloco de dados (Setup1).";
        return result;
    }

    MemReader r0(block0.data(), block0.size(), ver.isUnicode);
    HeaderInfo header;
    if (!parseHeader(r0, ver, header)) {
        closeExe();
        result.error = "Falha ao analisar cabecalho do instalador.";
        return result;
    }
    result.appName = header.appName;

    if (!skipLanguages(r0, ver, header.languageCount) ||
        !skipMessages(r0, header.messageCount) ||
        !skipPermissions(r0, header.permissionCount) ||
        !skipTypes(r0, ver, header.typeCount) ||
        !skipComponents(r0, ver, header.componentCount) ||
        !skipTasks(r0, ver, header.taskCount) ||
        !skipDirectories(r0, ver, header.directoryCount)) {
        closeExe();
        result.error = "Falha ao ler tabelas de entrada do instalador.";
        return result;
    }

    std::vector<FileEntry> files;
    if (!parseFiles(r0, ver, header.fileCount, files)) {
        closeExe();
        result.error = "Falha ao ler lista de arquivos do instalador.";
        return result;
    }
    INNO_LOG("Parsed %zu files (remaining=%zu), dataEntryCount=%u",
             files.size(), r0.remaining(), header.dataEntryCount);
    for (std::size_t dbg = 0; dbg < std::min<std::size_t>(5, files.size()); ++dbg) {
        INNO_LOG("File[%zu]: dest='%s' loc=%u", dbg, files[dbg].destination.c_str(), files[dbg].location);
    }

    MemReader r1(block1.data(), block1.size(), ver.isUnicode);
    std::vector<DataEntry> dataEntries;
    if (!parseDataEntries(r1, ver, header, header.dataEntryCount, dataEntries)) {
        closeExe();
        result.error = "Falha ao ler tabela de chunks (DataEntries).";
        return result;
    }

    // Group target files by chunk (firstSlice, chunkOffset)
    struct ChunkKey {
        std::uint32_t slice = 0;
        std::uint32_t offset = 0;
        bool operator<(const ChunkKey &other) const {
            if (slice != other.slice) return slice < other.slice;
            return offset < other.offset;
        }
    };
    struct ChunkGroup {
        CompressionMethod compression = CompUnknown;
        std::uint64_t compressedSize = 0;
        std::vector<ExtractTarget> targets;
    };

    std::map<ChunkKey, ChunkGroup> chunks;
    std::map<std::string, std::uint32_t> multiPartFiles;
    std::uint64_t totalBytes = 0;
    std::uint32_t totalFiles = 0;

    for (const FileEntry &fe : files) {
        if (fe.location >= dataEntries.size()) {
            continue;
        }
        std::string relPath;
        if (!extractGameRelativePath(fe.destination, relPath)) {
            continue;
        }
        const DataEntry &de = dataEntries[fe.location];
        if (de.chunkEncrypted) {
            closeExe();
            result.error = "Instalador criptografado com senha nao suportado.";
            return result;
        }

        ChunkKey key{de.firstSlice, de.chunkOffset};
        ChunkGroup &grp = chunks[key];
        grp.compression = de.chunkCompression;
        grp.compressedSize = de.chunkCompressedSize;
        grp.targets.push_back(ExtractTarget{
            relPath, de.fileOffset, de.fileSize, fe.gogZlib, fe.gogPartIndex, fe.gogPartCount});
        if (fe.gogPartCount > 1) {
            multiPartFiles[relPath] = fe.gogPartCount;
        }

        totalBytes += de.fileSize;
        ++totalFiles;
    }

    if (totalFiles == 0) {
        closeExe();
        result.error = "Nenhum arquivo de Deus Ex (System/Maps/Textures/Sounds/Music) encontrado no instalador.";
        return result;
    }

    fs::path outBase(outputDir);
    std::error_code ec;
    fs::create_directories(outBase, ec);

    SliceStream slices(fs::path(installerPath), exeFile, exeSize,
                       offsets.dataOffset, header.slicesPerDisk);

    ExtractProgress prog;
    prog.bytesTotal = totalBytes;
    prog.filesTotal = totalFiles;

    std::array<std::uint8_t, 65536> inBuf{};
    std::array<std::uint8_t, 65536> outBuf{};

    for (auto &kv : chunks) {
        const ChunkKey &key = kv.first;
        ChunkGroup &grp = kv.second;

        std::sort(grp.targets.begin(), grp.targets.end(),
                  [](const ExtractTarget &a, const ExtractTarget &b) {
                      if (a.fileOffset != b.fileOffset) return a.fileOffset < b.fileOffset;
                      return a.fileSize < b.fileSize;
                  });

        // Deduplicate targets sharing the same (fileOffset, fileSize) in the same chunk
        std::vector<std::pair<std::string, std::string>> duplicateCopies;
        std::vector<ExtractTarget> uniqueTargets;
        uniqueTargets.reserve(grp.targets.size());
        for (const auto &t : grp.targets) {
            if (!uniqueTargets.empty() &&
                uniqueTargets.back().fileOffset == t.fileOffset &&
                uniqueTargets.back().fileSize == t.fileSize) {
                duplicateCopies.emplace_back(uniqueTargets.back().relPath, t.relPath);
            } else {
                uniqueTargets.push_back(t);
            }
        }
        grp.targets = std::move(uniqueTargets);

        if (!slices.seekChunk(key.slice, key.offset)) {
            closeExe();
            result.error = "Falha ao localizar chunk de dados no instalador.";
            return result;
        }

        char chunkMagic[4] = {};
        std::size_t gotMagic = 0;
        if (!slices.readChunkBytes(reinterpret_cast<std::uint8_t *>(chunkMagic), 4, gotMagic) ||
            gotMagic != 4 || std::memcmp(chunkMagic, "zlb\x1a", 4) != 0) {
            closeExe();
            result.error = "Cabecalho de chunk invalido (esperado zlb\\x1a).";
            return result;
        }

        // Stream state
        lzma_stream lzStrm = LZMA_STREAM_INIT;
        z_stream zStrm{};
        bool useLzma = false;
        bool useZlib = false;

        if (grp.compression == CompLZMA1) {
            std::uint8_t props[5] = {};
            std::size_t gotProps = 0;
            if (!slices.readChunkBytes(props, 5, gotProps) || gotProps != 5 ||
                !initLzma1Decoder(lzStrm, props)) {
                closeExe();
                result.error = "Falha ao inicializar decodificador LZMA1.";
                return result;
            }
            useLzma = true;
        } else if (grp.compression == CompLZMA2) {
            std::uint8_t prop = 0;
            std::size_t gotProp = 0;
            if (!slices.readChunkBytes(&prop, 1, gotProp) || gotProp != 1 ||
                !initLzma2Decoder(lzStrm, prop)) {
                closeExe();
                result.error = "Falha ao inicializar decodificador LZMA2.";
                return result;
            }
            useLzma = true;
        } else if (grp.compression == CompZlib) {
            if (inflateInit(&zStrm) != Z_OK) {
                closeExe();
                result.error = "Falha ao inicializar decodificador Zlib.";
                return result;
            }
            useZlib = true;
        } else if (grp.compression != CompStored) {
            closeExe();
            result.error = "Metodo de compressao nao suportado neste chunk.";
            return result;
        }

        auto cleanupStream = [&]() {
            if (useLzma) lzma_end(&lzStrm);
            if (useZlib) inflateEnd(&zStrm);
        };

        std::uint64_t chunkOutPos = 0;
        std::size_t targetIdx = 0;
        std::FILE *activeOut = nullptr;
        std::uint64_t activeWritten = 0;
        z_stream fileZStrm{};
        bool fileZActive = false;
        bool fileZChecked = false;
        bool fileZHasPending = false;
        std::uint8_t fileZPending = 0;
        std::array<std::uint8_t, 65536> fileZOut{};

        auto closeActive = [&]() {
            if (fileZActive) {
                inflateEnd(&fileZStrm);
                fileZActive = false;
            }
            if (fileZHasPending && activeOut != nullptr) {
                std::fwrite(&fileZPending, 1, 1, activeOut);
            }
            fileZHasPending = false;
            fileZChecked = false;
            if (activeOut != nullptr) {
                std::fclose(activeOut);
                activeOut = nullptr;
            }
        };

        auto isZlibHeader = [](std::uint8_t b0, std::uint8_t b1) -> bool {
            return (b0 & 0x0F) == 8 && (((static_cast<unsigned>(b0) << 8) | b1) % 31) == 0;
        };

        auto writeActiveBytes = [&](const std::uint8_t *buf, std::size_t n, const std::string &relPath, bool isGogZlib) -> bool {
            if (n == 0) return true;
            std::size_t off = 0;
            while (off < n) {
                if (isGogZlib && !fileZChecked) {
                    if (fileZHasPending) {
                        std::uint8_t b0 = fileZPending;
                        std::uint8_t b1 = buf[off];
                        fileZHasPending = false;
                        fileZChecked = true;
                        if (isZlibHeader(b0, b1)) {
                            fileZStrm = z_stream{};
                            if (inflateInit(&fileZStrm) == Z_OK) {
                                fileZActive = true;
                                fileZStrm.next_in = &b0;
                                fileZStrm.avail_in = 1;
                                fileZStrm.next_out = fileZOut.data();
                                fileZStrm.avail_out = static_cast<uInt>(fileZOut.size());
                                inflate(&fileZStrm, Z_NO_FLUSH);
                            }
                        } else {
                            if (std::fwrite(&b0, 1, 1, activeOut) != 1) {
                                result.error = "Erro ao gravar arquivo: " + relPath;
                                return false;
                            }
                        }
                    } else if (n - off < 2) {
                        fileZPending = buf[off];
                        fileZHasPending = true;
                        return true;
                    } else {
                        fileZChecked = true;
                        if (isZlibHeader(buf[off], buf[off + 1])) {
                            fileZStrm = z_stream{};
                            if (inflateInit(&fileZStrm) == Z_OK) {
                                fileZActive = true;
                            }
                        }
                    }
                }
                if (!fileZActive) {
                    std::size_t rem = n - off;
                    if (std::fwrite(buf + off, 1, rem, activeOut) != rem) {
                        result.error = "Erro ao gravar arquivo: " + relPath;
                        return false;
                    }
                    return true;
                }
                fileZStrm.next_in = const_cast<Bytef *>(buf + off);
                fileZStrm.avail_in = static_cast<uInt>(n - off);
                bool streamEnded = false;
                while (fileZStrm.avail_in > 0) {
                    fileZStrm.next_out = fileZOut.data();
                    fileZStrm.avail_out = static_cast<uInt>(fileZOut.size());
                    int zr = inflate(&fileZStrm, Z_NO_FLUSH);
                    std::size_t outN = fileZOut.size() - fileZStrm.avail_out;
                    if (outN > 0 && std::fwrite(fileZOut.data(), 1, outN, activeOut) != outN) {
                        result.error = "Erro ao gravar arquivo descomprimido: " + relPath;
                        return false;
                    }
                    if (zr == Z_STREAM_END) {
                        off = n - fileZStrm.avail_in;
                        inflateEnd(&fileZStrm);
                        fileZActive = false;
                        fileZChecked = false;
                        streamEnded = true;
                        break;
                    }
                    if (zr != Z_OK && zr != Z_BUF_ERROR) {
                        result.error = "Erro zlib interno em: " + relPath;
                        return false;
                    }
                }
                if (!streamEnded) {
                    break;
                }
            }
            return true;
        };

        auto consumeDecompressed = [&](const std::uint8_t *data, std::size_t len) -> bool {
            std::size_t pos = 0;
            while (pos < len && targetIdx < grp.targets.size()) {
                const ExtractTarget &t = grp.targets[targetIdx];
                std::uint64_t streamPos = chunkOutPos + pos;

                if (streamPos < t.fileOffset) {
                    std::size_t skipN = static_cast<std::size_t>(
                        std::min<std::uint64_t>(len - pos, t.fileOffset - streamPos));
                    pos += skipN;
                    continue;
                }

                if (activeOut == nullptr) {
                    std::string diskRel = t.gogPartCount > 1
                        ? (t.relPath + ".gogpart" + std::to_string(t.gogPartIndex))
                        : t.relPath;
                    fs::path dstPath = outBase / diskRel;
                    fs::create_directories(dstPath.parent_path(), ec);
                    activeOut = std::fopen(dstPath.string().c_str(), "wb");
                    if (activeOut == nullptr) {
                        result.error = "Erro ao criar arquivo: " + dstPath.string();
                        return false;
                    }
                    activeWritten = 0;
                    fileZChecked = false;
                    fileZActive = false;
                    prog.currentFile = t.relPath;
                }

                std::uint64_t need = t.fileSize - activeWritten;
                std::size_t writeN = static_cast<std::size_t>(
                    std::min<std::uint64_t>(len - pos, need));
                if (writeN > 0) {
                    if (!writeActiveBytes(data + pos, writeN, t.relPath, t.gogZlib)) {
                        closeActive();
                        return false;
                    }
                    pos += writeN;
                    activeWritten += writeN;
                    prog.bytesDone += writeN;
                    result.bytesExtracted += writeN;
                }

                if (activeWritten >= t.fileSize) {
                    closeActive();
                    ++targetIdx;
                    ++prog.filesDone;
                    ++result.filesExtracted;
                    if (progressCb && !progressCb(prog)) {
                        result.error = "Extracao cancelada pelo usuario.";
                        return false;
                    }
                }
            }
            chunkOutPos += len;
            return true;
        };

        // Handle zero-byte files at start
        while (targetIdx < grp.targets.size() && grp.targets[targetIdx].fileSize == 0) {
            if (!consumeDecompressed(nullptr, 0)) {
                closeActive();
                cleanupStream();
                closeExe();
                return result;
            }
        }

        const std::uint64_t maxTargetEnd = grp.targets.back().fileOffset + grp.targets.back().fileSize;
        bool streamDone = false;

        while (!streamDone && targetIdx < grp.targets.size() && chunkOutPos < maxTargetEnd) {
            if (grp.compression == CompStored) {
                std::size_t want = static_cast<std::size_t>(
                    std::min<std::uint64_t>(outBuf.size(), maxTargetEnd - chunkOutPos));
                std::size_t got = 0;
                if (!slices.readChunkBytes(outBuf.data(), want, got) || got == 0) {
                    break;
                }
                if (!consumeDecompressed(outBuf.data(), got)) {
                    closeActive();
                    cleanupStream();
                    closeExe();
                    return result;
                }
                continue;
            }

            std::size_t gotIn = 0;
            if (!slices.readChunkBytes(inBuf.data(), inBuf.size(), gotIn)) {
                gotIn = 0;
            }

            if (useLzma) {
                lzStrm.next_in = inBuf.data();
                lzStrm.avail_in = gotIn;
                lzma_action action = (gotIn == 0) ? LZMA_FINISH : LZMA_RUN;

                while (lzStrm.avail_in > 0 || action == LZMA_FINISH) {
                    lzStrm.next_out = outBuf.data();
                    lzStrm.avail_out = outBuf.size();
                    lzma_ret lret = lzma_code(&lzStrm, action);
                    std::size_t produced = outBuf.size() - lzStrm.avail_out;
                    if (produced > 0 && !consumeDecompressed(outBuf.data(), produced)) {
                        closeActive();
                        cleanupStream();
                        closeExe();
                        return result;
                    }
                    if (lret == LZMA_STREAM_END) {
                        streamDone = true;
                        break;
                    }
                    if (lret != LZMA_OK && lret != LZMA_BUF_ERROR) {
                        closeActive();
                        cleanupStream();
                        closeExe();
                        result.error = "Erro durante descompressao LZMA.";
                        return result;
                    }
                    if (targetIdx >= grp.targets.size() || chunkOutPos >= maxTargetEnd) {
                        streamDone = true;
                        break;
                    }
                    if (lret == LZMA_BUF_ERROR && produced == 0 && lzStrm.avail_in == 0) {
                        if (action == LZMA_FINISH) {
                            streamDone = true;
                        }
                        break;
                    }
                }
            } else if (useZlib) {
                zStrm.next_in = inBuf.data();
                zStrm.avail_in = static_cast<uInt>(gotIn);
                if (gotIn == 0) {
                    streamDone = true;
                    break;
                }
                while (zStrm.avail_in > 0) {
                    zStrm.next_out = outBuf.data();
                    zStrm.avail_out = static_cast<uInt>(outBuf.size());
                    int zret = inflate(&zStrm, Z_NO_FLUSH);
                    std::size_t produced = outBuf.size() - zStrm.avail_out;
                    if (produced > 0 && !consumeDecompressed(outBuf.data(), produced)) {
                        closeActive();
                        cleanupStream();
                        closeExe();
                        return result;
                    }
                    if (zret == Z_STREAM_END) {
                        streamDone = true;
                        break;
                    }
                    if (zret != Z_OK && zret != Z_BUF_ERROR) {
                        closeActive();
                        cleanupStream();
                        closeExe();
                        result.error = "Erro durante descompressao Zlib.";
                        return result;
                    }
                    if (targetIdx >= grp.targets.size() || chunkOutPos >= maxTargetEnd) {
                        streamDone = true;
                        break;
                    }
                }
            }
        }

        closeActive();
        cleanupStream();

        if (targetIdx < grp.targets.size()) {
            const ExtractTarget &remT = grp.targets[targetIdx];
            INNO_LOG("Chunk failed slice=%u off=%u comp=%d targetIdx=%zu/%zu chunkOutPos=%llu maxEnd=%llu next='%s' fileOff=%llu fileSz=%llu",
                     key.slice, key.offset, static_cast<int>(grp.compression),
                     targetIdx, grp.targets.size(),
                     static_cast<unsigned long long>(chunkOutPos),
                     static_cast<unsigned long long>(maxTargetEnd),
                     remT.relPath.c_str(),
                     static_cast<unsigned long long>(remT.fileOffset),
                     static_cast<unsigned long long>(remT.fileSize));
            closeExe();
            result.error = "Chunk terminou antes de extrair todos os arquivos esperados.";
            return result;
        }

        for (const auto &cp : duplicateCopies) {
            fs::path srcP = outBase / cp.first;
            fs::path dstP = outBase / cp.second;
            fs::create_directories(dstP.parent_path(), ec);
            if (fs::copy_file(srcP, dstP, fs::copy_options::overwrite_existing, ec)) {
                std::uint64_t fsz = fs::file_size(dstP, ec);
                prog.bytesDone += fsz;
                result.bytesExtracted += fsz;
                ++prog.filesDone;
                ++result.filesExtracted;
                prog.currentFile = cp.second;
            }
        }

        if (progressCb && !progressCb(prog)) {
            closeExe();
            result.error = "Extracao cancelada pelo usuario.";
            return result;
        }
    }

    for (const auto &mp : multiPartFiles) {
        const std::string &relPath = mp.first;
        std::uint32_t partCount = mp.second;
        fs::path finalPath = outBase / relPath;
        fs::create_directories(finalPath.parent_path(), ec);
        std::FILE *out = std::fopen(finalPath.string().c_str(), "wb");
        if (out == nullptr) {
            closeExe();
            result.error = "Erro ao montar arquivo multi-parte: " + relPath;
            return result;
        }
        std::array<std::uint8_t, 65536> copyBuf{};
        for (std::uint32_t pi = 0; pi < partCount; ++pi) {
            fs::path partPath = outBase / (relPath + ".gogpart" + std::to_string(pi));
            std::FILE *in = std::fopen(partPath.string().c_str(), "rb");
            if (in == nullptr) {
                std::fclose(out);
                closeExe();
                result.error = "Parte ausente ao montar arquivo: " + partPath.string();
                return result;
            }
            while (true) {
                std::size_t nr = std::fread(copyBuf.data(), 1, copyBuf.size(), in);
                if (nr > 0 && std::fwrite(copyBuf.data(), 1, nr, out) != nr) {
                    std::fclose(in);
                    std::fclose(out);
                    closeExe();
                    result.error = "Erro ao gravar arquivo montado: " + relPath;
                    return result;
                }
                if (nr < copyBuf.size()) {
                    break;
                }
            }
            std::fclose(in);
            fs::remove(partPath, ec);
        }
        std::fclose(out);
    }

    closeExe();
    result.ok = true;
    return result;
}

} // namespace opende::inno

#endif
