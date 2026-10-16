#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ayt::sequence {

using ShowVec3 = std::array<float, 3>;
enum class ShowInterpolation : std::uint8_t { Step, Linear, Smooth };
enum class ShowGobo : std::uint8_t { Open, Stripes, Grid, Ring, Star, Texture };

struct FixtureState {
    ShowVec3 position{0, 5, 0};
    ShowVec3 color{1, 1, 1};
    float pan = 0, tilt = 0; // radians; zero aims downward, pan rotates around Y.
    float intensity = 30, range = 30;
    /// Core and outer field half angles in radians. The region between them
    /// fades smoothly; each radius = apertureRadius + distance * tan(angle).
    /// cos(inner) >= cos(outer) + 1e-6. Intensity is not a constant-flux model.
    float innerAngle = 0.035f, outerAngle = 0.05236f;
    float volumetric = 1;
    float goboRotation = 0, goboScale = 1;
    /// Pattern edge blur [0,1] and normalized iris opening (0,1].
    float goboFocus = 0, iris = 1;
    ShowGobo gobo = ShowGobo::Open;
    bool shadow = false;
    /// Exit-aperture radius in world units, [0,1]; zero is a point source.
    float apertureRadius = 0.035f;
    /// Optional bounded 2x2 shadow filter radius in atlas texels, [0,4].
    float shadowSoftness = 0;
};
struct FixtureKey {
    double time = 0;
    FixtureState value;
    ShowInterpolation interpolation = ShowInterpolation::Linear;
};
struct FixtureTrack {
    std::uint32_t id = 0;
    std::string group;
    std::vector<FixtureKey> keys;
    float sweepPan = 0, sweepTilt = 0, sweepFrequency = 0, phase = 0;
    float strobeFrequency = 0, strobeDuty = 0.5f;
    /// Optional UTF-8 texture path relative to the .lshow file. Required by
    /// Texture keys; the host loads and binds it before presenting frames.
    std::string goboTexturePath;
};
struct ShowPoint {
    std::uint32_t id = 0;
    ShowVec3 position{};
    ShowVec3 color{1, 1, 1};
    float intensity = 4;
    float radius = 0.05f;
};
struct FormationKey {
    double time = 0;
    std::vector<ShowPoint> points;
    ShowInterpolation interpolation = ShowInterpolation::Smooth;
};
struct LightShow {
    std::string name;
    double duration = 10;
    bool loop = true;
    std::vector<FixtureTrack> fixtures;
    std::vector<FormationKey> formations;
};
struct EvaluatedFixture { std::uint32_t id = 0; FixtureState value; };
struct LightShowFrame {
    double time = 0;
    std::vector<EvaluatedFixture> fixtures;
    std::vector<ShowPoint> points;
};

struct FormationClearanceConflict {
    std::uint32_t firstId = 0, secondId = 0;
    /// Show time at closest approach, in seconds.
    double time = 0;
    float distance = 0;
};
struct FormationClearanceReport {
    std::vector<FormationClearanceConflict> conflicts;
    std::size_t checkedPairs = 0;
    /// False when a caller-supplied work or result limit was reached.
    bool complete = true;
};

/// Immutable validated performance data, independent of Renderer/World/audio.
/// Compile canonicalizes formation points by stable ID; all formations must
/// contain the same IDs. Bounds: 256 fixtures, 65536 points, 1M total samples,
/// 24 hours. Rejected input never replaces a previously compiled show.
class CompiledLightShow final {
public:
    static std::shared_ptr<const CompiledLightShow> compile(
        LightShow data, std::string* error = nullptr);
    /// Absolute-time evaluation, independent of frame cadence. Caller-owned
    /// output reuses vector capacity. Seek emits no events and performs no I/O.
    /// Set procedural=false to sample authored keys for editing, excluding
    /// sweep and strobe modulation. Playback always includes procedural motion.
    bool evaluate(double time, LightShowFrame& output, bool procedural = true) const;
    /// Check simultaneous drone separation over every authored formation interval.
    /// Linear and Smooth use the same geometric line and a shared monotone time
    /// parameter, so closest approach is solved continuously, not frame-sampled.
    /// Step keys jump instantly; the held and next key positions are checked.
    /// This is an authoring diagnostic, not flight-path planning or a safety
    /// certification. Invalid limits leave output unchanged.
    bool analyzeFormationClearance(float minimumDistance, std::size_t maxPairChecks,
                                   std::size_t maxConflicts,
                                   FormationClearanceReport& output) const;
    const LightShow& data() const noexcept { return _data; }
private:
    explicit CompiledLightShow(LightShow data) : _data(std::move(data)) {}
    LightShow _data;
};

/// One explicit clock owner. Pause holds the current frame; seek is valid while
/// paused, loop wraps, once holds the endpoint. No wall clock or implicit tick.
class LightShowPlayer final {
public:
    void bind(std::shared_ptr<const CompiledLightShow> show);
    void play() noexcept { if (_show) _playing = true; }
    void pause() noexcept { _playing = false; }
    void stop() noexcept { _playing = false; _time = 0; }
    bool seek(double time);
    bool advance(double delta);
    /// Drive playback from an external absolute clock, e.g. an audio playhead.
    /// Idempotent for repeated timestamps; pause holds, loops wrap and Once
    /// holds the endpoint. Caller owns clock latency/transport synchronization.
    bool synchronize(double elapsedSeconds);
    bool evaluate(LightShowFrame& output) const;
    double time() const noexcept { return _time; }
    bool playing() const noexcept { return _playing; }
private:
    std::shared_ptr<const CompiledLightShow> _show;
    double _time = 0;
    bool _playing = false;
};

/// General demonstration content; the catalog describes the evaluator, never
/// these fixture arrangements or formation rules.
LightShow makeLightShowExample(std::uint32_t fixtureCount = 32,
                              std::uint32_t pointCount = 1000);

} // namespace ayt::sequence
