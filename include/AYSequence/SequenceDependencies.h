#pragma once

#include <AYSequence/Sequence.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ayt::sequence {

enum class SequenceDependencyKind : std::uint8_t {
    Scene, Animation, Audio
};

struct SequenceDependency {
    SequenceDependencyKind kind = SequenceDependencyKind::Scene;
    std::string path; // Assets-relative, portable path.
    std::uint64_t bytes = 0;
    std::uint64_t contentHash = 0; // FNV-1a; cache identity, not authentication.
    bool operator==(const SequenceDependency&) const = default;
};

struct SequenceDependencyManifest {
    std::vector<SequenceDependency> entries; // Kind then path order, unique.
    std::uint64_t signature = 0; // Sequence JSON and dependency content identity.
};

/// Validate a Sequence and all referenced Scene/.anm/.aud files under one
/// trusted Assets root. Reject missing, nonregular, linked or escaping files;
/// read each bounded file to compute a reproducible content fingerprint.
/// On failure output is unchanged. Rerun immediately before publication to
/// detect changes since planning; this is not a Cook/Pak publisher.
[[nodiscard]] bool buildSequenceDependencyManifest(const Sequence& sequence,
    const std::filesystem::path& assetsRoot,
    SequenceDependencyManifest& output, Diagnostic* error = nullptr);

} // namespace ayt::sequence
