#pragma once

#include <AYProgression/DialoguePresentation.h>
#include <AYAudio/AudioTypes.h>

#include <functional>
#include <memory>
#include <string>

namespace ayt::audio { class AudioSubSystem; }
namespace ayt::resource { class IAudio; }

namespace ayt::sequence {

/// Optional AYAudio sink for one dialogue voice at a time. The Host resolves
/// game-owned voice keys; use with PresentationController after durable choice
/// commit. AudioSubSystem must outlive this object.
class DialogueVoicePlayer {
public:
    using Loader=std::function<std::shared_ptr<resource::IAudio>(const std::string&)>;
    explicit DialogueVoicePlayer(audio::AudioSubSystem& audio) noexcept;
    ~DialogueVoicePlayer();
    DialogueVoicePlayer(const DialogueVoicePlayer&)=delete;
    DialogueVoicePlayer& operator=(const DialogueVoicePlayer&)=delete;

    /// Start a fresh node on the Voice bus. Duplicate current delivery keys
    /// do not replay. Restore-only or empty voice keys stop and release the
    /// prior clip. Loading/registration/play failure preserves live playback
    /// and permits retry. A restarted audio device invalidates old handles.
    [[nodiscard]] bool apply(const progression::dialogue::Presentation& frame,
                             const Loader& load,std::string* error=nullptr);
    void clear() noexcept;
    [[nodiscard]] const std::string& sourceKey() const noexcept { return _sourceKey; }
    [[nodiscard]] bool playing() const noexcept;
private:
    [[nodiscard]] bool current() const noexcept;
    audio::AudioSubSystem* _audio;
    std::uint64_t _audioSerial=0, _generation=0;
    audio::AudioClipId _clip=audio::InvalidClipId;
    audio::VoiceHandle _voice=audio::InvalidVoice;
    std::string _sourceKey, _deliveryKey;
};

} // namespace ayt::sequence
