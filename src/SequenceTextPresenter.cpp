#include <AYSequence/SequenceTextPresenter.h>
#include <AYLocalization.h>
#include <AYUI/TextLabel.h>
#include <AYUI/UnicodeText.h>
#include <set>

namespace ayt::sequence {
namespace {
void show(ayt::ui::TextLabel& label,const std::wstring& text,bool visible) {
    if (label.getText()!=text) label.setText(text);
    if (label.isAuthoredVisible()!=visible) label.setVisible(visible);
}
}
bool SequenceTextPresenter::bind(std::span<const SequenceTextSlot> slots) {
    if (slots.size()>MaxSlots) return false;
    std::set<ayt::ui::TextLabel*> unique;
    for (const auto& slot:slots)
        if (!slot.speaker || !slot.body ||
            !unique.insert(slot.speaker).second ||
            !unique.insert(slot.body).second) return false;
    std::vector<SequenceTextSlot> replacement(slots.begin(),slots.end());
    clear();
    _slots.swap(replacement);
    return true;
}
bool SequenceTextPresenter::apply(const Evaluation& evaluation,
                                  const ayt::localization::Localization& localization) {
    if (evaluation.textCues.size()>_slots.size()) return false;
    struct Resolved {std::wstring speaker,body;};
    std::vector<Resolved> resolved;
    resolved.reserve(evaluation.textCues.size());
    for (const auto& cue:evaluation.textCues) {
        const auto speaker=cue.speakerKey.empty() ? std::string{}
            : localization.get(cue.speakerKey,cue.speakerKey);
        const auto body=localization.get(cue.textKey,cue.textKey);
        resolved.push_back({ayt::ui::decodeUtf8Text(speaker),
                            ayt::ui::decodeUtf8Text(body)});
    }
    for (std::size_t i=0;i<_slots.size();++i) {
        const bool active=i<resolved.size();
        show(*_slots[i].speaker,active ? resolved[i].speaker : L"",
             active && !resolved[i].speaker.empty());
        show(*_slots[i].body,active ? resolved[i].body : L"",active);
    }
    return true;
}
bool SequenceTextPresenter::seek(const CompiledSequence& sequence,double time,
                                 const ayt::localization::Localization& localization) {
    Evaluation evaluation;
    return sequence.evaluate(time,evaluation) && apply(evaluation,localization);
}
void SequenceTextPresenter::clear() {
    for (const auto& slot:_slots) {
        show(*slot.speaker,L"",false);
        show(*slot.body,L"",false);
    }
}
} // namespace ayt::sequence
