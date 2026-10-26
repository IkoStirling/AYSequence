#include <AYSequence/DialogueMediaSession.h>

namespace ayt::sequence {

bool DialogueMediaSession::present(
    const progression::dialogue::Presentation& frame,
    const DialogueSequenceResolver& sequenceResolver,
    const DialogueVoicePlayer::Loader& voiceLoader,
    const localization::Localization& localization,
    std::string* error) {
    Diagnostic sequenceError;
    if (!_sequence.apply(frame,sequenceResolver,&sequenceError)) {
        if (error) *error=sequenceError.message;
        return false;
    }
    if (!_voice.apply(frame,voiceLoader,error)) return false;
    if (!_text.apply(frame,localization)) {
        if (error) *error="dialogue subtitle labels are not bound";
        return false;
    }
    if (error) error->clear();
    return true;
}

} // namespace ayt::sequence
