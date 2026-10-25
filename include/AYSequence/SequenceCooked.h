#pragma once

#include <AYSequence/Sequence.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ayt::sequence {

struct SequenceCookedMetadata {
    std::uint64_t dependencySignature = 0;
    std::uint64_t sourceHash = 0;
};

/// Encode schema 1 .sqc with validated canonical Sequence JSON and the
/// content signature of all referenced files under Assets. No dependencies
/// are copied into the output; the publisher must stage them separately.
/// Failure leaves output unchanged.
[[nodiscard]] bool encodeCookedSequence(const Sequence& sequence,
    const std::filesystem::path& assetsRoot,
    std::vector<std::uint8_t>& output, Diagnostic* error = nullptr);

/// Decode a bounded, checksummed .sqc envelope, then validate the embedded
/// Sequence. Metadata is owned; a signature is a cache identity, not a trust
/// or current-dependency proof. Failure leaves outputs unchanged.
[[nodiscard]] bool decodeCookedSequence(std::span<const std::uint8_t> bytes,
    Sequence& output, SequenceCookedMetadata* metadata = nullptr,
    Diagnostic* error = nullptr);

} // namespace ayt::sequence
