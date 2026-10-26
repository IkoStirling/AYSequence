#pragma once

#include <AYProgression/DialoguePresentation.h>
#include <AYSequence/SequenceTextPresenter.h>

namespace ayt::sequence {

/// Localized one-line dialogue UI bridge. The Host owns both TextLabels and
/// layout; apply the latest committed or restored Presentation on changes and
/// again after a language switch. This never starts media or advances a cursor.
class DialogueTextPresenter {
public:
    [[nodiscard]] bool bind(ui::TextLabel& speaker,ui::TextLabel& body);
    [[nodiscard]] bool apply(const progression::dialogue::Presentation& frame,
                             const localization::Localization& localization);
    void clear() { _presenter.clear(); }
private:
    SequenceTextPresenter _presenter;
};

} // namespace ayt::sequence
