#include <AYSequence/DialogueSequencePlayback.h>

#include <utility>

namespace ayt::sequence {
namespace {
bool fail(Diagnostic* error,const char* message) {
    if (error) *error={"dialogue sequence",message};
    return false;
}
}

bool DialogueSequencePlayback::apply(
    const progression::dialogue::Presentation& frame,
    const DialogueSequenceResolver& resolver,Diagnostic* error) {
    if (frame.deliveryKey.empty())
        return fail(error,"presentation delivery key is empty");
    if (!frame.startMedia || frame.sequenceCueKey.empty()) {
        _player.stop();
        _player.bind(nullptr);
        _sourceKey.clear();
        _deliveryKey=frame.deliveryKey;
        if (error) *error={};
        return true;
    }
    if (_deliveryKey==frame.deliveryKey) {
        if (error) *error={};
        return true;
    }
    if (!resolver) return fail(error,"dialogue sequence resolver is missing");
    try {
        auto sequence=resolver(frame.sequenceCueKey);
        if (!sequence)
            return fail(error,"dialogue sequence asset is unavailable");
        SequencePlayer candidate;
        candidate.bind(std::move(sequence));
        if (!candidate.play())
            return fail(error,"dialogue sequence could not start");
        auto key=frame.sequenceCueKey;
        auto delivery=frame.deliveryKey;
        _player=std::move(candidate);
        _sourceKey=std::move(key);
        _deliveryKey=std::move(delivery);
        if (error) *error={};
        return true;
    } catch (...) {
        return fail(error,"dialogue sequence resolution failed");
    }
}

bool DialogueSequencePlayback::advance(double deltaSeconds,
                                      PlaybackFrame& output,
                                      Diagnostic* error) {
    return _player.advance(deltaSeconds,output,error);
}

void DialogueSequencePlayback::stop() noexcept {
    _player.stop();
    _player.bind(nullptr);
    _sourceKey.clear();
    _deliveryKey.clear();
}

} // namespace ayt::sequence
