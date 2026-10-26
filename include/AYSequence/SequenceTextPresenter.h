#pragma once

#include <AYSequence/Sequence.h>
#include <cstddef>
#include <span>
#include <vector>

namespace ayt::localization { class Localization; }
namespace ayt::ui { class TextLabel; }
namespace ayt::sequence {

struct SequenceTextSlot {
    ayt::ui::TextLabel* speaker=nullptr;
    ayt::ui::TextLabel* body=nullptr;
};

/// Optional AYUI bridge for active localized text intervals. Labels remain
/// owned by the caller's UI tree, which must outlive this presenter. Apply an
/// Evaluation after each play/seek/pause/language change; this does not tick
/// sequence time or dispatch events/audio. Content owns layout and styling.
class SequenceTextPresenter {
public:
    static constexpr std::size_t MaxSlots=16;
    /// Validate all non-null, distinct label pairs before replacing binding.
    bool bind(std::span<const SequenceTextSlot> slots);
    /// Render active cues in author order. Overflow rejects without changing
    /// any label; missing localization uses the stable key visibly.
    bool apply(const Evaluation& evaluation,
               const ayt::localization::Localization& localization);
    /// Event-free direct seek convenience; invalid time preserves UI state.
    bool seek(const CompiledSequence& sequence,double time,
              const ayt::localization::Localization& localization);
    void clear();
    std::size_t slotCount() const noexcept { return _slots.size(); }
private:
    std::vector<SequenceTextSlot> _slots;
};

} // namespace ayt::sequence
