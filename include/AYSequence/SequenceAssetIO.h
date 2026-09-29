#pragma once
#include <AYSequence/Sequence.h>
#include <string_view>

namespace ayt::sequence {
/// JSON schema 1, type authoring.scene-sequence. Strict fields and duplicate-key
/// rejection; 16 MiB / 32-depth limit. Failure leaves output unchanged.
bool decodeSequence(std::string_view bytes, Sequence& output, Diagnostic* error = nullptr);
/// Validate and produce canonical JSON; failure leaves output unchanged.
bool encodeSequence(const Sequence& sequence, std::string& output, Diagnostic* error = nullptr);
/// Loose authoring file only; caller supplies explicit .ayseq path. Not a Resource loader.
bool loadSequence(const std::string& path, Sequence& output, Diagnostic* error = nullptr);
/// Validated single-file atomic publication via AYIO, not a multi-file transaction.
bool saveSequence(const std::string& path, const Sequence& sequence, Diagnostic* error = nullptr);
} // namespace ayt::sequence
