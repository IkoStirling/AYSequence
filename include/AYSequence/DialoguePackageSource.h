#pragma once

#include <AYProgression/DialogueBindings.h>
#include <AYProgression/DialogueJson.h>
#include <AYSequence/DialogueSequencePlayback.h>
#include <AYSequence/DialogueVoicePlayer.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ayt::localization { class Localization; }

namespace ayt::sequence {

/// One published dialogue set backed by a ProjectBuild Pak. Opening pins the
/// manifest and every mapped entry's indexed size/hash, without decoding media.
/// Each later lookup opens its own reader and rejects a replaced entry. The
/// caller owns this object for the lifetime of resolver callbacks and still
/// owns dialogue commits, AudioSubSystem, UI labels and Sequence frame output.
class DialoguePackageSource {
public:
    DialoguePackageSource() = default;
    DialoguePackageSource(const DialoguePackageSource&) = delete;
    DialoguePackageSource& operator=(const DialoguePackageSource&) = delete;
    DialoguePackageSource(DialoguePackageSource&&) = delete;
    DialoguePackageSource& operator=(DialoguePackageSource&&) = delete;
    /// Failure preserves a previously opened source. All paths are portable
    /// Assets-relative names; documents/locales are <=1 MiB, cooked sequences
    /// <=16 MiB + header, voices <=128 MiB. No media starts here.
    [[nodiscard]] bool open(std::string packagePath,
        std::string bindingsEntry, std::string* error = nullptr);
    [[nodiscard]] bool ready() const noexcept { return _ready; }
    [[nodiscard]] const progression::dialogue::Bindings* bindings() const noexcept {
        return _ready ? &_bindings : nullptr;
    }
    /// Resolve only documents/locales declared by the pinned manifest.
    [[nodiscard]] std::optional<progression::dialogue::Document> loadDocument(
        std::string_view path, std::string* error = nullptr) const;
    [[nodiscard]] bool loadLocale(std::string_view path,
        localization::Localization& target, std::string* error = nullptr) const;
    /// Resolve media keys to owned runtime assets. Missing, invalid or changed
    /// entries fail without changing a current DialogueMediaSession player.
    [[nodiscard]] std::shared_ptr<const CompiledSequence> loadSequence(
        std::string_view key, std::string* error = nullptr) const;
    [[nodiscard]] std::shared_ptr<resource::IAudio> loadVoice(
        std::string_view key, std::string* error = nullptr) const;
    /// Decode every distinct voice and sequence cue used by one bound
    /// document before presenting it. Fails on missing keys, invalid media or
    /// changed Pak entries; never starts playback or changes this source.
    /// This is optional and may perform one bounded read/decode per media key.
    [[nodiscard]] bool preflightDocumentMedia(
        std::string_view documentPath, std::string* error = nullptr) const;
    /// Borrow this object in PresentationController/DialogueMediaSession sinks.
    /// These functions do IO only when a fresh media-bearing frame requests it.
    [[nodiscard]] DialogueSequenceResolver sequenceResolver() const;
    [[nodiscard]] DialogueVoicePlayer::Loader voiceLoader() const;

private:
    struct PinnedEntry { std::uint64_t size = 0; std::uint32_t hash = 0; };
    [[nodiscard]] bool read(std::string_view path, std::vector<std::uint8_t>& out,
        std::string* error) const;
    std::string _packagePath;
    std::string _bindingsEntry;
    progression::dialogue::Bindings _bindings;
    std::map<std::string, PinnedEntry, std::less<>> _pinned;
    bool _ready = false;
};

} // namespace ayt::sequence
