#include <AYSequence/DialogueVoicePlayer.h>

#include <AYAudio/AudioEngine.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYResource/assetsDefs/IAudio.h>

#include <utility>

namespace ayt::sequence {
namespace {
bool fail(std::string* error,const char* message) {
    if (error) *error=message;
    return false;
}
}

DialogueVoicePlayer::DialogueVoicePlayer(audio::AudioSubSystem& audio) noexcept
    : _audio(&audio) {}
DialogueVoicePlayer::~DialogueVoicePlayer() { clear(); }

bool DialogueVoicePlayer::current() const noexcept {
    const auto* engine=_audio->engine();
    return engine && engine->isInitialized() &&
        _audioSerial==_audio->lifecycleSerial() &&
        _generation==engine->lifecycleGeneration();
}
bool DialogueVoicePlayer::playing() const noexcept {
    if (!current() || _voice==audio::InvalidVoice) return false;
    audio::VoiceRuntime state;
    return _audio->engine()->voiceState(_voice,state);
}
void DialogueVoicePlayer::clear() noexcept {
    if (current()) {
        auto* engine=_audio->engine();
        if (_voice!=audio::InvalidVoice) engine->stop(_voice);
        if (_clip!=audio::InvalidClipId) engine->releaseClip(_clip);
    }
    _clip=audio::InvalidClipId;
    _voice=audio::InvalidVoice;
    _audioSerial=_generation=0;
    _sourceKey.clear();
    _deliveryKey.clear();
}

bool DialogueVoicePlayer::apply(
    const progression::dialogue::Presentation& frame,
    const Loader& load,std::string* error) {
    if (frame.deliveryKey.empty())
        return fail(error,"dialogue voice delivery key is empty");
    if (!frame.startMedia || frame.voiceCueKey.empty()) {
        clear();
        _deliveryKey=frame.deliveryKey;
        if (error) error->clear();
        return true;
    }
    if (_deliveryKey==frame.deliveryKey && current()) {
        if (error) error->clear();
        return true;
    }
    auto* engine=_audio->engine();
    if (!engine || !engine->isInitialized() || !load)
        return fail(error,"dialogue voice needs audio and a resource loader");
    const auto serial=_audio->lifecycleSerial();
    const auto generation=engine->lifecycleGeneration();
    auto pendingClip=audio::InvalidClipId;
    auto pendingVoice=audio::InvalidVoice;
    try {
        auto source=frame.voiceCueKey;
        auto delivery=frame.deliveryKey;
        auto resource=load(frame.voiceCueKey);
        if (!resource)
            return fail(error,"dialogue voice resource is unavailable");
        if (_audio->lifecycleSerial()!=serial ||
            engine->lifecycleGeneration()!=generation || !engine->isInitialized())
            return fail(error,"audio device changed during voice loading");
        pendingClip=_audio->registerClipFromResource(resource.get());
        if (pendingClip==audio::InvalidClipId)
            return fail(error,"dialogue voice clip registration failed");
        pendingVoice=engine->play(pendingClip,audio::AudioBus::Voice,{},1.0f,false);
        if (pendingVoice==audio::InvalidVoice) {
            engine->releaseClip(pendingClip);
            return fail(error,"dialogue voice playback failed");
        }
        clear();
        _clip=pendingClip;
        _voice=pendingVoice;
        _audioSerial=serial;
        _generation=generation;
        _sourceKey=std::move(source);
        _deliveryKey=std::move(delivery);
        if (error) error->clear();
        return true;
    } catch (...) {
        if (engine->isInitialized() &&
            _audio->lifecycleSerial()==serial &&
            engine->lifecycleGeneration()==generation) {
            if (pendingVoice!=audio::InvalidVoice) engine->stop(pendingVoice);
            if (pendingClip!=audio::InvalidClipId) engine->releaseClip(pendingClip);
        }
        return fail(error,"dialogue voice loading failed");
    }
}

} // namespace ayt::sequence
