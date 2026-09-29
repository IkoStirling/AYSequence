#pragma once
#include <AYSequence/Sequence.h>
#include <cstdint>
#include <functional>

namespace ayt::sequence {
/// UI-free authoring model. Every edit stages, validates and publishes one owned
/// snapshot atomically. No history, filesystem, World, UI or playback authority.
/// Hosts supply history; unchanged edits retain snapshot identity and revision.
class SequenceAuthoring final {
  public:
    SequenceAuthoring();
    const std::shared_ptr<const CompiledSequence> &snapshot() const noexcept { return _snapshot; }
    std::uint64_t revision() const noexcept { return _revision; }
    bool replace(Sequence sequence, Diagnostic *error = nullptr);
    bool edit(const std::function<bool(Sequence &)> &mutation, Diagnostic *error = nullptr);
    std::string nextId(const std::string &prefix) const;

  private:
    std::shared_ptr<const CompiledSequence> _snapshot;
    std::uint64_t _revision = 0;
    bool _editing = false;
};
// Stable-ID helpers intended to be called on an edit's staged data. They sort
// affected rows, but final whole-document validation still owns collision checks.
bool updateSequenceKey(Sequence &, const std::string &id, double time, Value value);
bool moveSequenceItems(Sequence &, const std::vector<std::string> &ids, double delta);
bool removeSequenceItems(Sequence &, const std::vector<std::string> &ids);
bool moveSequenceSection(Sequence &, const std::string &id, double start);
// Left trim preserves source sampling at the new start. Right trim leaves offset.
bool trimSequenceSection(Sequence &, const std::string &id, double start, double end);
} // namespace ayt::sequence
