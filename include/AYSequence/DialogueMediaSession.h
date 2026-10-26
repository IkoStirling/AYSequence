#pragma once

#include <AYSequence/DialogueSequencePlayback.h>
#include <AYSequence/DialogueVoicePlayer.h>
#include <AYSequence/DialogueTextPresenter.h>

#include <string>

namespace ayt::sequence {

/// Optional composition of the three dialogue presentation sinks. It is
/// single-owner and holds only UI label borrows plus an AudioSubSystem borrow.
/// Audio and labels must outlive it. The Host still owns content lookup, one
/// Sequence tick, World/camera application and durable dialogue commits.
class DialogueMediaSession {
public:
    explicit DialogueMediaSession(audio::AudioSubSystem& audio) noexcept
        : _voice(audio) {}
    [[nodiscard]] bool bindText(ui::TextLabel& speaker,ui::TextLabel& body) {
        return _text.bind(speaker,body);
    }
    /// Apply a committed Presentation. Sequence resolution runs first, then
    /// voice playback, then localized text. A later failure may leave an
    /// earlier sink applied; retry the same delivery key. Sequence/voice
    /// adapters suppress duplicate starts, while text may safely refresh.
    [[nodiscard]] bool present(const progression::dialogue::Presentation& frame,
        const DialogueSequenceResolver& sequenceResolver,
        const DialogueVoicePlayer::Loader& voiceLoader,
        const localization::Localization& localization,
        std::string* error=nullptr);
    [[nodiscard]] bool advance(double deltaSeconds,PlaybackFrame& output,
                               Diagnostic* error=nullptr) {
        return _sequence.advance(deltaSeconds,output,error);
    }
    void stop() noexcept {
        _sequence.stop();
        _voice.clear();
        _text.clear();
    }
    [[nodiscard]] const DialogueSequencePlayback& sequence() const noexcept {
        return _sequence;
    }
    [[nodiscard]] const DialogueVoicePlayer& voice() const noexcept {
        return _voice;
    }
private:
    DialogueSequencePlayback _sequence;
    DialogueVoicePlayer _voice;
    DialogueTextPresenter _text;
};

} // namespace ayt::sequence
