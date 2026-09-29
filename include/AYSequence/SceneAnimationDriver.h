#pragma once
#include <AYSequence/Sequence.h>
#include <cstdint>
#include <map>

namespace ayt::entity { class World; }
namespace ayt::sequence {
/// Optional adapter protocol owned exclusively by SceneSequenceSession.
/// prepare may resolve resources but never mutate World or dispatch events;
/// sample must not write World or invoke callbacks. acquire
/// preflights every target atomically. apply/release are callback-free; release
/// restores only still-owned unchanged state, before World teardown.
class SceneAnimationDriver {
public:
    virtual ~SceneAnimationDriver() = default;
    virtual bool prepare(ayt::entity::World&, const CompiledSequence&,
                         const std::map<std::string, std::uint32_t>&, Diagnostic*) = 0;
    virtual bool acquire(Diagnostic*) = 0;
    virtual bool check(Diagnostic*) = 0;
    virtual bool sample(const Evaluation&, Diagnostic*) = 0;
    virtual void apply() noexcept = 0;
    virtual void release() noexcept = 0;
};
} // namespace ayt::sequence
