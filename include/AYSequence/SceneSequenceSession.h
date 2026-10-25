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
using SequenceAudioSink = std::function<void(const AudioCue&)>;
/// Dispatch a mixed event/audio batch once in time order (events first at a
/// tie). The caller resolves audio assets and camera selection; callbacks run
/// after Transform writes and each is guarded against stale World/session.
bool dispatchSequenceCues(std::shared_ptr<SceneSequenceSession> session,
    SceneFrame frame, SequenceEventSink events, SequenceAudioSink audio,
    std::size_t& deliveredEvents, std::size_t& deliveredAudio,
    Diagnostic* error = nullptr);
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
    struct DebugBinding {std::string name;std::uint32_t entityId=0;bool ownsTransform=false;std::vector<std::string> activeSections;};
    struct DebugEvent {Event event;std::uint64_t batch=0,generation=0;bool delivered=false;};
    struct DebugSnapshot {std::uint64_t session=0;std::string sequence,error;double position=0,duration=0;PlaybackState state=PlaybackState::Stopped;std::vector<DebugBinding> bindings;std::vector<DebugEvent> events;};
    /// Owner-thread bounded observations of all manual/hosted sessions in this World. Never advance or drain frames.
    static std::vector<DebugSnapshot> debugSnapshots(const ayt::entity::World& world);
    /// Optional independent event observations,128 per session; defaults off.
    static void setDebugCaptureEnabled(bool enabled) noexcept;
    static void clearDebugHistory() noexcept;
    SceneSequenceSession();
    ~SceneSequenceSession();
    SceneSequenceSession(const SceneSequenceSession&) = delete;
    SceneSequenceSession& operator=(const SceneSequenceSession&) = delete;
    /// Explicit World-local Entity IDs, resolved now; not persistent GUIDs.
    /// Reject missing/extra bindings; animation tracks require the driver overload.
    /// Reject physics and both Fixed/binary32 Sim translation authorities.
    /// Failure leaves the prior session binding unchanged.
    bool bind(ayt::entity::World& world, std::shared_ptr<const CompiledSequence> sequence,
              const std::map<std::string, std::uint32_t>& entities, Diagnostic* error = nullptr);
    /// Link optional AYSequence::AnimationIntegration and pass its driver factory.
    /// Validate/pin immutable Clip snapshots before replacing the old binding.
    bool bind(ayt::entity::World& world, std::shared_ptr<const CompiledSequence> sequence,
              const std::map<std::string, std::uint32_t>& entities,
              std::unique_ptr<SceneAnimationDriver> animation, Diagnostic* error = nullptr);
    /// Opt in to World-owned 3D camera selection for authored cuts. Call after
    /// bind and before play; the lease is released on stop/completion/rebind or
    /// World teardown. Camera components are never edited or serialized.
    bool enableCameraCuts(std::int32_t viewportId = 0, Diagnostic* error = nullptr);
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
    friend bool dispatchSequenceCues(std::shared_ptr<SceneSequenceSession>, SceneFrame,
                                    SequenceEventSink, SequenceAudioSink,
                                    std::size_t&, std::size_t&, Diagnostic*);
};
} // namespace ayt::sequence
