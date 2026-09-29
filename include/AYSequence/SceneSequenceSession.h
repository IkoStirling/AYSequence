#pragma once
#include <AYSequence/Sequence.h>
#include <cstdint>
#include <map>
#include <functional>
#include <AYSequence/SceneAnimationDriver.h>

namespace ayt::entity { class World; }
namespace ayt::sequence {
class SequenceSubSystem;
struct SceneFrame {
    PlaybackFrame playback;
    std::uint64_t generation = 0, owner = 0, batch = 0;
};
class SceneSequenceSession;
using SequenceEventSink = std::function<void(const Event&)>;
/// Consume an owned batch once, holding the session alive across callbacks.
/// Reject stale/foreign/replayed/nested batches. Recheck bindings and generation
/// before each callback; stop/seek/rebind/deletion/World teardown cancel the rest.
/// Catch callback exceptions, stop safely and return false with delivered count.
bool dispatchSequenceEvents(std::shared_ptr<SceneSequenceSession> session, SceneFrame frame,
                            SequenceEventSink sink, std::size_t& delivered, Diagnostic* error = nullptr);

/// Main-thread single-World Transform adapter. Stores World-scoped handles;
/// WorldLifecycle teardown revokes bindings before object destruction.
/// Exact field/revision checks detect external writes, not intercept setters.
class SceneSequenceSession final {
public:
    SceneSequenceSession();
    ~SceneSequenceSession();
    SceneSequenceSession(const SceneSequenceSession&) = delete;
    SceneSequenceSession& operator=(const SceneSequenceSession&) = delete;
    /// Explicit World-local Entity IDs, resolved now; not persistent GUIDs.
    /// Reject missing/extra bindings; animation tracks require the driver overload.
    /// Failure leaves the prior session binding unchanged.
    bool bind(ayt::entity::World& world, std::shared_ptr<const CompiledSequence> sequence,
              const std::map<std::string, std::uint32_t>& entities, Diagnostic* error = nullptr);
    /// Link optional AYSequence::AnimationIntegration and pass its driver factory.
    /// Validate/pin immutable Clip snapshots before replacing the old binding.
    bool bind(ayt::entity::World& world, std::shared_ptr<const CompiledSequence> sequence,
              const std::map<std::string, std::uint32_t>& entities,
              std::unique_ptr<SceneAnimationDriver> animation, Diagnostic* error = nullptr);
    /// Acquire exclusive Transform ownership, snapshot once and apply current time.
    bool play(Diagnostic* error = nullptr);
    void pause() noexcept;
    /// Restore only still-owned, unmodified Transforms; never overwrite a new writer.
    void stop() noexcept;
    /// Scrub takes ownership and evaluates without events, preserving the baseline.
    bool seek(double seconds, SceneFrame& output, Diagnostic* error = nullptr);
    /// Manual owner only. Hosted sessions reject this to prevent a double tick.
    bool advance(double seconds, SceneFrame& output, Diagnostic* error = nullptr);
    double position() const noexcept;
    PlaybackState state() const noexcept;
    /// Check before every event callback. Seek/stop/rebind/new play run/World
    /// revocation invalidate old batches; completion alone leaves its output valid.
    bool isCurrent(std::uint64_t generation) const noexcept;
    const Diagnostic& lastError() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    bool acquire(Diagnostic* error);
    bool apply(const Evaluation& evaluation, Diagnostic* error);
    bool check(Diagnostic* error);
    void release() noexcept;
    bool step(double seconds, SceneFrame& output, Diagnostic* error);
    void stamp(SceneFrame& output);
    static void beforeWorldShutdown(ayt::entity::World& world) noexcept;
    friend class SequenceSubSystem;
    friend bool dispatchSequenceEvents(std::shared_ptr<SceneSequenceSession>, SceneFrame,
                                      SequenceEventSink, std::size_t&, Diagnostic*);
};
} // namespace ayt::sequence
