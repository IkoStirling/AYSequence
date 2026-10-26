#pragma once

#include <AYProgression/DialoguePresentation.h>
#include <AYSequence/Sequence.h>

#include <functional>
#include <string>
#include <string_view>

namespace ayt::sequence {

/// The Host resolves a dialogue sequence key to an already compiled immutable
/// asset. Resolution may fail; it must not mutate the active player.
using DialogueSequenceResolver=std::function<
    std::shared_ptr<const CompiledSequence>(std::string_view)>;

/// Value-only dialogue-to-Sequence handoff. It does not bind World entities,
/// perform camera cuts or dispatch audio: the Host owns those presentation
/// sinks. Apply a committed dialogue Presentation, then advance this player
/// from the same single presentation tick owner as other Sequence players.
class DialogueSequencePlayback {
public:
    /// On a fresh media-bearing cursor, resolve and start its Sequence Once.
    /// Duplicate delivery keys do not restart. Restore-only frames stop the
    /// prior Sequence and never replay. Resolution failure preserves the old
    /// playback and key, so the Host may retry the same presentation delivery.
    [[nodiscard]] bool apply(const progression::dialogue::Presentation& frame,
        const DialogueSequenceResolver& resolver,Diagnostic* error=nullptr);
    [[nodiscard]] bool advance(double deltaSeconds,PlaybackFrame& output,
                               Diagnostic* error=nullptr);
    void stop() noexcept;
    [[nodiscard]] const std::string& sourceKey() const noexcept { return _sourceKey; }
    [[nodiscard]] PlaybackState state() const noexcept { return _player.state(); }
    [[nodiscard]] double position() const noexcept { return _player.position(); }
private:
    SequencePlayer _player;
    std::string _sourceKey;
    std::string _deliveryKey;
};

} // namespace ayt::sequence
