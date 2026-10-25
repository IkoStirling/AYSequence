#pragma once

#include <AYSequence/Sequence.h>
#include <AYAudio/AudioTypes.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ayt::audio { class AudioSubSystem; }
namespace ayt::resource { class IAudio; }

namespace ayt::sequence {

/// Optional owner-thread bridge from validated sequence cues to AYAudio.
/// Prepare before playback: asset loading and clip registration never happen
/// inside the cue callback. AudioSubSystem must outlive this object.
class SequenceAudioPlayer final {
public:
    using Loader = std::function<std::shared_ptr<ayt::resource::IAudio>(const std::string&)>;
    explicit SequenceAudioPlayer(ayt::audio::AudioSubSystem& audio) noexcept;
    ~SequenceAudioPlayer();
    SequenceAudioPlayer(const SequenceAudioPlayer&) = delete;
    SequenceAudioPlayer& operator=(const SequenceAudioPlayer&) = delete;

    /// Clears the previous presentation, then loads at most 256 unique audio
    /// sources. Failure leaves this player empty and reports the source.
    bool prepare(std::shared_ptr<const CompiledSequence> sequence, const Loader& load,
                 std::string* error = nullptr);
    /// Call from dispatchSequenceCues' audio sink. Reject unknown or modified
    /// cues, stale audio generations, and more than 256 simultaneous voices.
    bool play(const AudioCue& cue, ayt::audio::AudioBus bus = ayt::audio::AudioBus::Sfx);
    /// Stop voices without discarding prepared clips; call on sequence stop/seek.
    void stop() noexcept;
    /// Stop voices and release owned clips. Also called by the destructor.
    void clear() noexcept;
    std::size_t activeVoices() const noexcept { return _voices.size(); }
    std::size_t preparedSources() const noexcept { return _clips.size(); }

private:
    struct PreparedCue { AudioCue cue; ayt::audio::AudioClipId clip = 0; };
    bool current() const noexcept;
    void pruneVoices();
    ayt::audio::AudioSubSystem* _audio;
    std::uint64_t _audioSerial = 0;
    std::uint64_t _generation = 0;
    std::unordered_map<std::string, ayt::audio::AudioClipId> _clips;
    std::unordered_map<std::string, PreparedCue> _cues;
    std::vector<ayt::audio::VoiceHandle> _voices;
};

} // namespace ayt::sequence
