#include <AYSequence/DialogueTextPresenter.h>

#include <array>

namespace ayt::sequence {

bool DialogueTextPresenter::bind(ui::TextLabel& speaker,ui::TextLabel& body) {
    const std::array slots{SequenceTextSlot{&speaker,&body}};
    return _presenter.bind(slots);
}

bool DialogueTextPresenter::apply(
    const progression::dialogue::Presentation& frame,
    const localization::Localization& localization) {
    Evaluation evaluation;
    if (!frame.textKey.empty())
        evaluation.textCues.push_back({frame.deliveryKey,frame.textKey,
            frame.speakerKey,0,1});
    return _presenter.apply(evaluation,localization);
}

} // namespace ayt::sequence
