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
enum class ShowGobo : std::uint8_t { Open, Stripes, Grid, Ring, Star };

struct FixtureState {
    ShowVec3 position{0, 5, 0};
    ShowVec3 color{1, 1, 1};
    float pan = 0, tilt = 0; // radians; zero aims downward, pan rotates around Y.
    float intensity = 30, range = 30;
    /// Cone half angles in radians; cos(inner) >= cos(outer) + 1e-6.
    float innerAngle = 0.12f, outerAngle = 0.22f;
    float volumetric = 1;
    float goboRotation = 0, goboScale = 1;
    ShowGobo gobo = ShowGobo::Open;
    bool shadow = false;
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
