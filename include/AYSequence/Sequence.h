#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ayt::sequence {

using Value = std::array<double, 4>;
enum class Channel { Position, Rotation, Scale };
struct Binding { std::string id; std::string entityName; bool operator==(const Binding&) const = default; };
struct Key { std::string id; double time = 0; Value value{}; bool operator==(const Key&) const = default; };
struct TransformTrack {
    std::string id, binding;
    Channel channel = Channel::Position;
    std::vector<Key> keys;
    bool operator==(const TransformTrack&) const = default;
};
struct AnimationSection {
    std::string id, source;
    double start = 0, end = 0, sourceOffset = 0, speed = 1, sourceDuration = 0;
    bool operator==(const AnimationSection&) const = default;
};
struct AnimationTrack { std::string id, binding; std::vector<AnimationSection> sections; bool operator==(const AnimationTrack&) const = default; };
struct Event { std::string id; double time = 0; std::string name, payload; bool operator==(const Event&) const = default; };
struct EventTrack { std::string id; std::vector<Event> events; bool operator==(const EventTrack&) const = default; };
/// Select one declared camera binding at the given time. The camera's pose
/// may be authored with ordinary TransformTracks on that binding.
struct CameraCut {
    std::string id, binding;
    double time = 0, blendSeconds = 0;
    bool operator==(const CameraCut&) const = default;
};
/// Start an audio asset at its sequence time; the game owns actual playback.
struct AudioCue {
    std::string id, source;
    double time = 0, gain = 1;
    bool operator==(const AudioCue&) const = default;
};
/// Localized text interval for a presentation layer. Keys are stable content
/// IDs, not rendered strings; [start,end) stays evaluable after seeks.
struct TextCue {
    std::string id, textKey, speakerKey;
    double start = 0, end = 0;
    bool operator==(const TextCue&) const = default;
};
struct Sequence {
    std::string id;
    double duration = 1, displayFps = 30;
    std::vector<Binding> bindings;
    std::vector<TransformTrack> transforms;
    std::vector<AnimationTrack> animations;
    std::vector<EventTrack> eventTracks;
    std::vector<CameraCut> cameraCuts;
    std::vector<AudioCue> audioCues;
    std::vector<TextCue> textCues;
    // Optional schema 2 authoring locator, relative to the project's Assets root.
    // Exact unique entity names are not persistent GUIDs; rename requires rebinding.
    std::string scenePath;
    bool operator==(const Sequence&) const = default;
};
struct TransformSample { std::string binding; Channel channel; Value value; };
struct AnimationSample { std::string binding, section, source; double sourceTime = 0; };
struct CameraSample { std::string cutId, binding; double blendSeconds = 0; };
struct Evaluation {
    std::vector<TransformSample> transforms;
    std::vector<AnimationSample> animations;
    std::optional<CameraSample> camera;
    std::vector<TextCue> textCues; // Active at this time, including direct seek.
};
struct Diagnostic { std::string location, message; };

/// Validate IDs, finite values, time ranges, supported channels and resource paths.
/// Limits: 1 day, 1024 bindings, 4096 tracks, 100000 keys, 10000 sections/events.
bool validate(const Sequence& sequence, Diagnostic* error = nullptr);

/// Immutable owned snapshot. Creation validates before publication; no borrowed data.
class CompiledSequence final {
public:
    /// Return null on invalid input. Animation samples map time only, not poses.
    static std::shared_ptr<const CompiledSequence> compile(Sequence sequence, Diagnostic* error = nullptr);
    const Sequence& data() const noexcept { return _data; }
    /// Direct, event-free seek evaluation. Reject non-finite/out-of-range time.
    /// Sections are [start,end); only a section ending at sequence duration has a final sample.
    bool evaluate(double time, Evaluation& output, Diagnostic* error = nullptr) const;
    const std::vector<Event>& orderedEvents() const noexcept { return _events; }
private:
    explicit CompiledSequence(Sequence sequence);
    Sequence _data;
    std::vector<Event> _events;
};

enum class PlaybackState { Stopped, Playing, Paused, Completed };
struct PlaybackFrame {
    Evaluation evaluation;
    std::vector<Event> events;
    std::vector<AudioCue> audioCues;
    bool completed = false;
};

/// Main-thread value-only Once player. No World access, callbacks or global clock.
/// The owner must advance it exactly once. Outputs own their event data.
class SequencePlayer final {
public:
    /// Bind an immutable snapshot and reset state/time. Null unbinds.
    void bind(std::shared_ptr<const CompiledSequence> sequence);
    bool play();
    void pause() noexcept;
    void stop() noexcept;
    /// Reject invalid times without changing state. Seek never queues events.
    bool seek(double seconds, PlaybackFrame& output, Diagnostic* error = nullptr);
    /// Positive finite delta only (zero is allowed). First positive tick includes
    /// the play origin; subsequent ticks use (previous,current]. No looping.
    bool advance(double deltaSeconds, PlaybackFrame& output, Diagnostic* error = nullptr);
    double position() const noexcept { return _position; }
    PlaybackState state() const noexcept { return _state; }
    const std::shared_ptr<const CompiledSequence>& sequence() const noexcept { return _sequence; }
private:
    std::shared_ptr<const CompiledSequence> _sequence;
    double _position = 0;
    PlaybackState _state = PlaybackState::Stopped;
    bool _includeOrigin = false;
};
} // namespace ayt::sequence
