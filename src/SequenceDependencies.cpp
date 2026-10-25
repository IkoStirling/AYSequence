#include <AYSequence/SequenceDependencies.h>
#include <AYSequence/SequenceAssetIO.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <string_view>

namespace ayt::sequence {
namespace {
constexpr std::uint64_t kOffset = 14695981039346656037ull;
constexpr std::uint64_t kPrime = 1099511628211ull;
constexpr std::uint64_t kMaxFileBytes = 64ull * 1024 * 1024;
constexpr std::uint64_t kMaxTotalBytes = 256ull * 1024 * 1024;
constexpr std::size_t kMaxDependencies = 16384;

void appendByte(std::uint64_t& hash, std::uint8_t byte) noexcept {
    hash ^= byte;
    hash *= kPrime;
}
void appendNumber(std::uint64_t& hash, std::uint64_t number) noexcept {
    for (unsigned i = 0; i < 8; ++i)
        appendByte(hash, static_cast<std::uint8_t>(number >> (8 * i)));
}
void appendText(std::uint64_t& hash, std::string_view value) noexcept {
    appendNumber(hash, value.size());
    for (const auto character : value)
        appendByte(hash, static_cast<std::uint8_t>(character));
}
bool fail(Diagnostic* error, std::string where, std::string message) {
    if (error) *error = {std::move(where), std::move(message)};
    return false;
}
bool underRoot(const std::filesystem::path& root,
    const std::filesystem::path& child) {
    auto r = root.begin(), c = child.begin();
    for (; r != root.end(); ++r, ++c)
        if (c == child.end() || *r != *c) return false;
    return c != child.end();
}
} // namespace

bool buildSequenceDependencyManifest(const Sequence& sequence,
    const std::filesystem::path& assetsRoot,
    SequenceDependencyManifest& output, Diagnostic* error) {
    if (error) *error = {};
    if (!validate(sequence, error)) return false;
    try {
        namespace fs = std::filesystem;
        if (!fs::is_directory(assetsRoot))
            return fail(error, "assetsRoot", "Assets root is not a directory");
        const auto root = fs::canonical(assetsRoot);
        std::map<std::pair<SequenceDependencyKind, std::string>, bool> paths;
        if (!sequence.scenePath.empty())
            paths.emplace(std::pair{SequenceDependencyKind::Scene,
                sequence.scenePath}, true);
        for (const auto& track : sequence.animations)
            for (const auto& section : track.sections)
                paths.emplace(std::pair{SequenceDependencyKind::Animation,
                    section.source}, true);
        for (const auto& cue : sequence.audioCues)
            paths.emplace(std::pair{SequenceDependencyKind::Audio,
                cue.source}, true);
        if (paths.size() > kMaxDependencies)
            return fail(error, "dependencies", "Too many referenced assets");
        std::string canonicalSequence;
        if (!encodeSequence(sequence, canonicalSequence, error)) return false;
        SequenceDependencyManifest candidate;
        candidate.entries.reserve(paths.size());
        candidate.signature = kOffset;
        appendText(candidate.signature, canonicalSequence);
        std::uint64_t total = 0;
        for (const auto& [key, ignored] : paths) {
            (void)ignored;
            const auto& [kind, portable] = key;
            const auto relative = fs::u8path(portable);
            auto current = root;
            for (const auto& component : relative) {
                current /= component;
                const auto status = fs::symlink_status(current);
                if (fs::is_symlink(status))
                    return fail(error, portable, "Linked dependency is forbidden");
            }
            if (!fs::is_regular_file(current))
                return fail(error, portable, "Dependency is not a regular file");
            const auto canonical = fs::canonical(current);
            if (!underRoot(root, canonical))
                return fail(error, portable, "Dependency escapes Assets root");
            const auto bytes = fs::file_size(current);
            if (!bytes || bytes > kMaxFileBytes || total > kMaxTotalBytes - bytes)
                return fail(error, portable, "Dependency size exceeds budget");
            total += bytes;
            const auto modified = fs::last_write_time(current);
            std::ifstream stream(current, std::ios::binary);
            if (!stream) return fail(error, portable, "Dependency cannot be opened");
            std::uint64_t hash = kOffset, remaining = bytes;
            std::array<char, 64 * 1024> buffer{};
            while (remaining) {
                const auto count = static_cast<std::streamsize>(
                    std::min<std::uint64_t>(remaining, buffer.size()));
                stream.read(buffer.data(), count);
                if (stream.gcount() != count)
                    return fail(error, portable, "Dependency changed during read");
                for (std::streamsize i = 0; i < count; ++i)
                    appendByte(hash, static_cast<std::uint8_t>(buffer[i]));
                remaining -= static_cast<std::uint64_t>(count);
            }
            if (fs::file_size(current) != bytes
                || fs::last_write_time(current) != modified)
                return fail(error, portable, "Dependency changed during read");
            candidate.entries.push_back({kind, portable, bytes, hash});
            appendNumber(candidate.signature, static_cast<std::uint64_t>(kind));
            appendText(candidate.signature, portable);
            appendNumber(candidate.signature, bytes);
            appendNumber(candidate.signature, hash);
        }
        output = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        return fail(error, "dependencies", exception.what());
    }
}

} // namespace ayt::sequence
