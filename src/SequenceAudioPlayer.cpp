#include <AYSequence/SequenceAudioPlayer.h>

#include <AYAudio/AudioEngine.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYResource/assetsDefs/IAudio.h>

#include <cmath>

namespace ayt::sequence {

SequenceAudioPlayer::SequenceAudioPlayer(ayt::audio::AudioSubSystem& audio) noexcept
    : _audio(&audio) {}

SequenceAudioPlayer::~SequenceAudioPlayer() { clear(); }

bool SequenceAudioPlayer::current() const noexcept {
    const auto* engine = _audio->engine();
    return engine && engine->isInitialized()
        && _audio->lifecycleSerial() == _audioSerial
        && engine->lifecycleGeneration() == _generation;
}

void SequenceAudioPlayer::stop() noexcept {
    if (current()) {
        auto* engine = _audio->engine();
        for (const auto voice : _voices) engine->stop(voice);
    }
    _voices.clear();
}

void SequenceAudioPlayer::clear() noexcept {
    stop();
    if (current()) {
        auto* engine = _audio->engine();
        for (const auto& [source, clip] : _clips) engine->releaseClip(clip);
    }
    _clips.clear();
    _cues.clear();
    _generation = 0;
    _audioSerial = 0;
}

bool SequenceAudioPlayer::prepare(std::shared_ptr<const CompiledSequence> sequence,
                                  const Loader& load, std::string* error) {
    clear();
    auto fail = [&](const std::string& reason) {
        clear();
        if (error) *error = reason;
        return false;
    };
    auto* engine = _audio->engine();
    if (!engine || !engine->isInitialized() || !sequence || !load)
        return fail("Expected initialized audio, compiled sequence and resource loader");
    _generation = engine->lifecycleGeneration();
    _audioSerial = _audio->lifecycleSerial();
    for (const auto& cue : sequence->data().audioCues) {
        if (!_clips.contains(cue.source)) {
            if (_clips.size() >= 256) return fail("Too many unique sequence audio sources");
            std::shared_ptr<ayt::resource::IAudio> resource;
            try { resource = load(cue.source); }
            catch (...) { return fail("Audio loader threw for " + cue.source); }
            if (!resource) return fail("Cannot load sequence audio " + cue.source);
            if (!current()) return fail("Audio engine changed during preparation");
            const auto clip = _audio->registerClipFromResource(resource.get());
            if (clip == ayt::audio::InvalidClipId)
                return fail("Cannot register sequence audio " + cue.source);
            _clips.emplace(cue.source, clip);
        }
        _cues.emplace(cue.id, PreparedCue{cue, _clips.at(cue.source)});
    }
    if (error) error->clear();
    return true;
}

void SequenceAudioPlayer::pruneVoices() {
    auto* engine = _audio->engine();
    std::erase_if(_voices, [&](const auto voice) {
        ayt::audio::VoiceRuntime state;
        return !engine->voiceState(voice, state);
    });
}

bool SequenceAudioPlayer::play(const AudioCue& cue, ayt::audio::AudioBus bus) {
    if (!current()) return false;
    if (static_cast<unsigned>(bus) >= static_cast<unsigned>(ayt::audio::AudioBus::Count))
        return false;
    const auto it = _cues.find(cue.id);
    if (it == _cues.end() || it->second.cue != cue) return false;
    if (!std::isfinite(cue.gain) || cue.gain < 0 || cue.gain > 4) return false;
    pruneVoices();
    if (_voices.size() >= 256) return false;
    const auto voice = _audio->engine()->play(it->second.clip, bus, {},
                                              static_cast<float>(cue.gain), false);
    if (voice == ayt::audio::InvalidVoice) return false;
    _voices.push_back(voice);
    return true;
}

} // namespace ayt::sequence
