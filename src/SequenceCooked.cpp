#include <AYSequence/SequenceCooked.h>
#include <AYSequence/SequenceAssetIO.h>
#include <AYSequence/SequenceDependencies.h>

#include <algorithm>
#include <array>
#include <new>
#include <string_view>

namespace ayt::sequence {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic{'A', 'Y', 'S', 'Q'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 32;
constexpr std::size_t kMaxJsonBytes = 16 * 1024 * 1024;
constexpr std::uint64_t kOffset = 14695981039346656037ull;
constexpr std::uint64_t kPrime = 1099511628211ull;

void put32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
void put64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
std::uint32_t get32(std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
    return value;
}
std::uint64_t get64(std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(bytes[offset + i]) << (8 * i);
    return value;
}
std::uint64_t hash(std::string_view bytes) noexcept {
    auto result = kOffset;
    for (const auto value : bytes) {
        result ^= static_cast<std::uint8_t>(value);
        result *= kPrime;
    }
    return result;
}
bool fail(Diagnostic* error, std::string message) {
    if (error) *error = {"cooked", std::move(message)};
    return false;
}
} // namespace

bool encodeCookedSequence(const Sequence& sequence,
    const std::filesystem::path& assetsRoot,
    std::vector<std::uint8_t>& output, Diagnostic* error) {
    if (error) *error = {};
    SequenceDependencyManifest manifest;
    if (!buildSequenceDependencyManifest(sequence, assetsRoot, manifest,
        error)) return false;
    std::string json;
    if (!encodeSequence(sequence, json, error)) return false;
    if (json.empty() || json.size() > kMaxJsonBytes)
        return fail(error, "Sequence JSON exceeds cooked envelope");
    try {
        std::vector<std::uint8_t> candidate;
        candidate.reserve(kHeaderBytes + json.size());
        candidate.insert(candidate.end(), kMagic.begin(), kMagic.end());
        put32(candidate, kVersion);
        put64(candidate, json.size());
        put64(candidate, hash(json));
        put64(candidate, manifest.signature);
        candidate.insert(candidate.end(), json.begin(), json.end());
        output.swap(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return fail(error, "Cooked sequence exceeds memory budget");
    }
}

bool decodeCookedSequence(std::span<const std::uint8_t> bytes,
    Sequence& output, SequenceCookedMetadata* metadata, Diagnostic* error) {
    if (error) *error = {};
    if (bytes.size() < kHeaderBytes || bytes.size() > kHeaderBytes + kMaxJsonBytes
        || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())
        || get32(bytes, 4) != kVersion || get64(bytes, 8) == 0
        || get64(bytes, 8) != bytes.size() - kHeaderBytes)
        return fail(error, "Invalid cooked sequence header or size");
    const auto json = std::string_view(
        reinterpret_cast<const char*>(bytes.data() + kHeaderBytes),
        bytes.size() - kHeaderBytes);
    if (hash(json) != get64(bytes, 16))
        return fail(error, "Cooked sequence checksum mismatch");
    Sequence candidate;
    if (!decodeSequence(json, candidate, error)) return false;
    output = std::move(candidate);
    if (metadata) *metadata = {get64(bytes, 24), get64(bytes, 16)};
    return true;
}

} // namespace ayt::sequence
