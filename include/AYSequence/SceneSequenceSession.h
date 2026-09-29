#pragma once
#include <AYSequence/Sequence.h>
#include <cstdint>
#include <map>

namespace ayt::entity { class World; }
namespace ayt::sequence {
class SequenceSubSystem;
struct SceneFrame { PlaybackFrame playback; std::uint64_t generation = 0; };

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
    /// Reject missing/extra bindings and animation tracks (pose integration is S3).
    /// Failure leaves the prior session binding unchanged.
    bool bind(ayt::entity::World& world, std::shared_ptr<const CompiledSequence> sequence,
              const std::map<std::string, std::uint32_t>& entities, Diagnostic* error = nullptr);
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
    static void beforeWorldShutdown(ayt::entity::World& world) noexcept;
    friend class SequenceSubSystem;
};
} // namespace ayt::sequence
