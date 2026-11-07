// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Private checkpoint format for RLToolsLearner.cpp. Include it only there, after the RLtools operation headers.
#pragma once

#include <rl_tools/persist/backends/tar/operations_cpu.h>
#include <rl_tools/nn/layers/dense/persist.h>
#include <rl_tools/nn/layers/sample_and_squash/persist.h>
#include <rl_tools/nn/layers/td3_sampling/persist.h>
#include <rl_tools/nn/layers/standardize/persist.h>
#include <rl_tools/nn/optimizers/adam/instance/persist.h>
#include <rl_tools/nn/optimizers/adam/persist.h>
#include <rl_tools/nn_models/mlp/persist.h>
#include <rl_tools/nn_models/mlp_unconditional_stddev/persist.h>
#include <rl_tools/nn_models/sequential/persist.h>
#include <rl_tools/random/persist.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if __has_include(<bit>)
#include <bit>
#endif
#ifdef _WIN32
#include <filesystem>
#include <system_error>
#endif
#include "RLToolsLearner.h"

namespace Schola::RLTools::Checkpoint
{
namespace tar = rl_tools::persist::backends::tar;

/** File layout: FHeader, then PayloadSize bytes of an RLtools in-memory tar archive.
 * All integers and floats are stored in native byte order; supported platforms are little-endian. */
#if defined(__cpp_lib_endian)
static_assert(std::endian::native == std::endian::little, "Schola RLtools checkpoints assume a little-endian host.");
#endif
constexpr char Magic[8] = {'S', 'C', 'H', 'R', 'L', 'T', 'C', 'K'};
constexpr std::uint32_t Format = 1;
struct FHeader
{
    char Magic[8];
    std::uint32_t Format, Mode, Algorithm, Observations, Actions, Reserved;
    char ProfileId[64];
    char Revision[48];
    std::uint64_t PreprocessingFingerprint; // 0 means identity preprocessing.
    std::uint64_t Version, Updates, ReplaySize, PayloadSize, PayloadChecksum;
};
static_assert(sizeof(FHeader) == 192, "The checkpoint header is a fixed on-disk layout.");

inline std::uint64_t Fnv1a(const void* Bytes, std::size_t Size, std::uint64_t Hash = 14695981039346656037ull)
{
    const unsigned char* Data = static_cast<const unsigned char*>(Bytes);
    for (std::size_t Index = 0; Index < Size; ++Index) { Hash ^= Data[Index]; Hash *= 1099511628211ull; }
    return Hash;
}
/** Covers the header (with its checksum field zeroed) and the payload, so a corrupt version or counter is refused too. */
inline std::uint64_t Checksum(const FHeader& Header, const void* Payload, std::size_t Size)
{
    // Raw bytes, as written to disk (padding included); only the checksum field itself is zeroed.
    unsigned char Bytes[sizeof(FHeader)];
    std::memcpy(Bytes, &Header, sizeof(Bytes));
    std::memset(Bytes + offsetof(FHeader, PayloadChecksum), 0, sizeof(Header.PayloadChecksum));
    return Fnv1a(Payload, Size, Fnv1a(Bytes, sizeof(Bytes)));
}
inline std::uint64_t PreprocessingFingerprint(const FSettings& Settings)
{
    if (Settings.ObservationMean.empty() && Settings.ObservationStd.empty()) return 0;
    std::uint64_t Hash = Fnv1a(Settings.ObservationMean.data(), Settings.ObservationMean.size() * sizeof(float));
    Hash = Fnv1a(Settings.ObservationStd.data(), Settings.ObservationStd.size() * sizeof(float), Hash);
    return Hash ? Hash : 1;
}
inline bool Terminated(const char* Text, std::size_t Size) { return std::memchr(Text, '\0', Size) != nullptr; }

struct FTarEntry { std::string Name; std::size_t Header, Offset, Size; };
// Independent, non-aborting walk over a ustar archive. Returns false on any malformed header or out-of-bounds entry.
inline bool ListTar(const char* Data, std::size_t Size, std::vector<FTarEntry>& Entries)
{
    constexpr std::size_t Block = 512;
    std::size_t At = 0;
    for (;;)
    {
        if (At + Block > Size) return false;
        const char* H = Data + At;
        if (H[0] == '\0') return true;
        if (std::memcmp(H + 257, "ustar", 5) != 0) return false;
        std::size_t Length = 0;
        for (int Index = 0; Index < 12 && H[124 + Index] >= '0' && H[124 + Index] <= '7'; ++Index)
            Length = Length * 8 + static_cast<std::size_t>(H[124 + Index] - '0');
        const std::size_t Header = At;
        At += Block;
        if (Length > Size - At) return false;
        Entries.push_back({std::string(H, strnlen(H, 100)), Header, At, Length});
        At += (Length + Block - 1) / Block * Block;
    }
}
inline bool EndsWith(const std::string& Text, const char* Suffix)
{
    const std::size_t Length = std::strlen(Suffix);
    return Text.size() >= Length && Text.compare(Text.size() - Length, Length, Suffix) == 0;
}
/** RLtools' tar loader calls std::abort on a missing key or size mismatch. The file must therefore be
 * structurally identical to what this build writes: same total size, byte-identical tar headers (names,
 * sizes, checksums), byte-identical metadata entries and end-of-archive blocks. Only numeric data may differ. */
inline bool SameStructure(const std::vector<char>& Reference, const char* Data, std::size_t Size)
{
    std::vector<FTarEntry> Entries;
    if (Size != Reference.size() || !ListTar(Reference.data(), Reference.size(), Entries)) return false;
    std::size_t End = 0;
    for (const auto& Entry : Entries)
    {
        if (std::memcmp(Reference.data() + Entry.Header, Data + Entry.Header, 512) != 0) return false;
        if (EndsWith(Entry.Name, "meta") && std::memcmp(Reference.data() + Entry.Offset, Data + Entry.Offset, Entry.Size) != 0) return false;
        End = Entry.Offset + (Entry.Size + 511) / 512 * 512;
    }
    return std::memcmp(Reference.data() + End, Data + End, Size - End) == 0;
}

/** Root group of an archive being written. Not copyable: the group points at the owned writer. */
template <typename TDevice>
struct TWriter
{
    using TI = typename TDevice::index_t;
    TDevice& Device;
    tar::Writer Writer;
    tar::WriterGroup<tar::WriterGroupSpecification<TI, tar::Writer>> Root;
    explicit TWriter(TDevice& InDevice) : Device(InDevice) { Root.writer = &Writer; }
    TWriter(const TWriter&) = delete;
    TWriter& operator=(const TWriter&) = delete;
    template <typename TValue> void Group(const char* Name, TValue& Value)
    { auto G = rl_tools::create_group(Device, Root, Name); rl_tools::save(Device, Value, G); }
    template <typename TTensor> void Tensor(const char* Name, TTensor& Value) { rl_tools::save(Device, Value, Root, Name); }
    std::vector<char> Finish() { tar::finalize(Device, Writer); return std::move(Writer.buffer); }
};
/** Root group of an archive being read. Callers must have checked SameStructure first. */
template <typename TDevice>
struct TReader
{
    using TI = typename TDevice::index_t;
    TDevice& Device;
    tar::ReaderGroup<tar::ReaderGroupSpecification<TI>> Root;
    TReader(TDevice& InDevice, const char* Data, std::size_t Size) : Device(InDevice)
    { Root.data.data = Data; Root.data.size = static_cast<TI>(Size); }
    template <typename TValue> bool Group(const char* Name, TValue& Value)
    { auto G = rl_tools::get_group(Device, Root, Name); return rl_tools::load(Device, Value, G); }
    template <typename TTensor> bool Tensor(const char* Name, TTensor& Value) { return rl_tools::load(Device, Value, Root, Name); }
};

// Counter tensor layout shared by every algorithm.
enum ECounter : unsigned { ActorUpdates, CriticUpdates, TargetUpdates, Stored, Position, Full, EpisodeStart, CounterCount };

inline bool Seek(std::FILE* File, std::int64_t Offset, int Origin)
{
#ifdef _WIN32
    return _fseeki64(File, Offset, Origin) == 0;
#else
    return fseeko(File, static_cast<off_t>(Offset), Origin) == 0;
#endif
}
inline std::int64_t Tell(std::FILE* File)
{
#ifdef _WIN32
    return _ftelli64(File);
#else
    return static_cast<std::int64_t>(ftello(File));
#endif
}
#ifdef _WIN32
/** Paths arrive as UTF-8 (TCHAR_TO_UTF8); narrow Windows APIs would read them in the active code page. */
inline std::filesystem::path NativePath(const std::string& Utf8)
{ return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(Utf8.data()), Utf8.size())); }
inline std::FILE* Open(const std::string& Path, const char* Mode)
{
    std::wstring WideMode(Mode, Mode + std::strlen(Mode));
    return _wfopen(NativePath(Path).c_str(), WideMode.c_str());
}
inline void Remove(const std::string& Path) { std::error_code Code; std::filesystem::remove(NativePath(Path), Code); }
#else
inline std::FILE* Open(const std::string& Path, const char* Mode) { return std::fopen(Path.c_str(), Mode); }
inline void Remove(const std::string& Path) { std::remove(Path.c_str()); }
#endif
struct FFile
{
    std::FILE* Handle = nullptr;
    FFile(const std::string& Path, const char* Mode) : Handle(Open(Path, Mode)) {}
    ~FFile() { if (Handle) std::fclose(Handle); }
    FFile(const FFile&) = delete;
    FFile& operator=(const FFile&) = delete;
    bool Close() { const bool Ok = !Handle || std::fclose(Handle) == 0; Handle = nullptr; return Ok; }
};

/** Opens Path and validates header + file size; leaves the stream positioned at the payload. */
inline ECheckpointResult ReadHeader(FFile& File, const std::string& Path, FHeader& Header, std::string& Error)
{
    if (!File.Handle) { Error = "Cannot open checkpoint file: " + Path; return ECheckpointResult::IoError; }
    if (!Seek(File.Handle, 0, SEEK_END)) { Error = "Cannot read checkpoint file size."; return ECheckpointResult::IoError; }
    const std::int64_t FileSize = Tell(File.Handle);
    if (FileSize < 0 || !Seek(File.Handle, 0, SEEK_SET)) { Error = "Cannot read checkpoint file size."; return ECheckpointResult::IoError; }
    if (static_cast<std::uint64_t>(FileSize) < sizeof(FHeader)) { Error = "File is too small to be a checkpoint."; return ECheckpointResult::BadFormat; }
    if (std::fread(&Header, sizeof(Header), 1, File.Handle) != 1) { Error = "Cannot read checkpoint header."; return ECheckpointResult::IoError; }
    if (std::memcmp(Header.Magic, Magic, sizeof(Magic)) != 0) { Error = "Not a Schola RLtools checkpoint."; return ECheckpointResult::BadFormat; }
    if (Header.Format != Format) { Error = "Unknown checkpoint format version " + std::to_string(Header.Format) + "."; return ECheckpointResult::BadFormat; }
    if (Header.Mode < 1 || Header.Mode > 3 || Header.Algorithm > 2
        || !Terminated(Header.ProfileId, sizeof(Header.ProfileId)) || !Terminated(Header.Revision, sizeof(Header.Revision)))
    { Error = "Corrupt checkpoint header."; return ECheckpointResult::BadFormat; }
    if (Header.PayloadSize != static_cast<std::uint64_t>(FileSize) - sizeof(FHeader))
    { Error = "Checkpoint payload size does not match the file size (truncated or corrupt)."; return ECheckpointResult::BadFormat; }
    return ECheckpointResult::Ok;
}
inline void ToInfo(const FHeader& Header, FCheckpointInfo& Info)
{
    Info.Format = Header.Format;
    Info.Mode = static_cast<ECheckpointMode>(Header.Mode);
    Info.ProfileId = Header.ProfileId;
    Info.Revision = Header.Revision;
    Info.Algorithm = static_cast<EAlgorithm>(Header.Algorithm);
    Info.Observations = Header.Observations;
    Info.Actions = Header.Actions;
    Info.Version = Header.Version;
    Info.Updates = Header.Updates;
    Info.ReplaySize = Header.ReplaySize;
    Info.PayloadSize = Header.PayloadSize;
    Info.PreprocessingIncluded = Header.PreprocessingFingerprint != 0;
}
inline bool Rename(const std::string& From, const std::string& To)
{
#ifdef _WIN32
    // std::rename fails on Windows when To exists; filesystem::rename replaces it (MoveFileExW).
    std::error_code Code;
    std::filesystem::rename(NativePath(From), NativePath(To), Code);
    return !Code;
#else
    return std::rename(From.c_str(), To.c_str()) == 0;
#endif
}
/** Writes Path + ".tmp" and renames it over Path. On failure the temporary file is removed. */
inline ECheckpointResult WriteFile(const std::string& Path, const FHeader& Header, const std::vector<char>& Payload, std::string& Error)
{
    const std::string Temporary = Path + ".tmp";
    bool Ok;
    {
        FFile File(Temporary, "wb");
        if (!File.Handle) { Error = "Cannot create checkpoint file: " + Temporary; return ECheckpointResult::IoError; }
        Ok = std::fwrite(&Header, sizeof(Header), 1, File.Handle) == 1
            && std::fwrite(Payload.data(), 1, Payload.size(), File.Handle) == Payload.size();
        Ok = File.Close() && Ok;
    }
    if (!Ok || !Rename(Temporary, Path))
    {
        Remove(Temporary);
        Error = "Cannot write checkpoint file: " + Path;
        return ECheckpointResult::IoError;
    }
    return ECheckpointResult::Ok;
}
}
