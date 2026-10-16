#include <AYSequence/LightShow.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace ayt::sequence {
namespace {
constexpr double tau = 2 * std::numbers::pi;
bool finite(float x, float lo, float hi) { return std::isfinite(x) && x >= lo && x <= hi; }
bool vectorValid(const ShowVec3& v, float lo, float hi) {
    return std::all_of(v.begin(), v.end(), [=](float x) { return finite(x, lo, hi); });
}
bool interpolationValid(ShowInterpolation i) { return i <= ShowInterpolation::Smooth; }
float weight(double time, double begin, double end, ShowInterpolation mode) {
    if (begin == end || mode == ShowInterpolation::Step) return 0;
    auto t = static_cast<float>(std::clamp((time - begin) / (end - begin), 0.0, 1.0));
    return mode == ShowInterpolation::Smooth ? t * t * (3 - 2 * t) : t;
}
float mix(float a, float b, float t) { return a + (b - a) * t; }
ShowVec3 mix(const ShowVec3& a, const ShowVec3& b, float t) {
    return {mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)};
}
FixtureState mix(const FixtureState& a, const FixtureState& b, float t) {
    auto out = a;
    out.position = mix(a.position, b.position, t); out.color = mix(a.color, b.color, t);
    out.pan = mix(a.pan,b.pan,t); out.tilt = mix(a.tilt,b.tilt,t);
    out.intensity = mix(a.intensity,b.intensity,t); out.range = mix(a.range,b.range,t);
    out.innerAngle = mix(a.innerAngle,b.innerAngle,t); out.outerAngle = mix(a.outerAngle,b.outerAngle,t);
    out.volumetric = mix(a.volumetric,b.volumetric,t);
    out.goboRotation = mix(a.goboRotation,b.goboRotation,t); out.goboScale = mix(a.goboScale,b.goboScale,t);
    return out;
}
template<class Keys> std::size_t lowerKey(const Keys& keys, double time) {
    const auto it = std::upper_bound(keys.begin(), keys.end(), time,
        [](double t, const auto& key) { return t < key.time; });
    return it == keys.begin() ? 0 : static_cast<std::size_t>(it - keys.begin() - 1);
}
bool fixtureValid(const FixtureState& s) {
    return vectorValid(s.position,-100000,100000) && vectorValid(s.color,0,10000)
        && finite(s.pan,-100000,100000) && finite(s.tilt,-100000,100000)
        && finite(s.intensity,0,100000) && finite(s.range,.01f,10000)
        && finite(s.innerAngle,.001f,1.55f) && finite(s.outerAngle,.002f,1.56f)
        && s.innerAngle < s.outerAngle
        && std::cos(s.innerAngle) >= std::cos(s.outerAngle) + .000001f
        && finite(s.volumetric,0,100)
        && finite(s.goboRotation,-100000,100000) && finite(s.goboScale,.01f,100)
        && s.gobo <= ShowGobo::Star;
}
}

std::shared_ptr<const CompiledLightShow> CompiledLightShow::compile(LightShow data, std::string* error) {
    const auto fail = [&](const std::string& message) -> std::shared_ptr<const CompiledLightShow> {
        if (error) *error = message; return {};
    };
    if (!std::isfinite(data.duration) || data.duration <= 0 || data.duration > 86400
        || data.name.size() > 256 || data.fixtures.size() > 256 || data.formations.size() > 4096)
        return fail("Invalid show duration/name or collection limit");
    if (data.fixtures.empty() && data.formations.empty()) return fail("Show has no fixtures or formations");
    std::size_t samples = 0;
    std::unordered_set<std::uint32_t> fixtureIds;
    for (const auto& track : data.fixtures) {
        if (!fixtureIds.insert(track.id).second || track.group.size() > 128
            || track.keys.empty() || track.keys.size() > 65536)
            return fail("Invalid fixture ID/group/key count");
        samples += track.keys.size();
        if (samples > 1000000) return fail("Show sample budget exceeded");
        if (!finite(track.sweepPan,-100,100) || !finite(track.sweepTilt,-100,100)
            || !finite(track.sweepFrequency,0,100) || !finite(track.phase,-100000,100000)
            || !finite(track.strobeFrequency,0,1000) || !finite(track.strobeDuty,0,1))
            return fail("Invalid sweep/strobe parameters");
        double previous = -1;
        for (const auto& key : track.keys) {
            if (!std::isfinite(key.time) || key.time < 0 || key.time > data.duration || key.time <= previous
                || !interpolationValid(key.interpolation) || !fixtureValid(key.value))
                return fail("Invalid fixture key time/state");
            previous = key.time;
        }
    }
    double previous = -1;
    for (auto& key : data.formations) {
        if (!std::isfinite(key.time) || key.time < 0 || key.time > data.duration || key.time <= previous
            || !interpolationValid(key.interpolation) || key.points.empty() || key.points.size() > 65536)
            return fail("Invalid formation time/point count");
        previous = key.time; samples += key.points.size();
        if (samples > 1000000) return fail("Show sample budget exceeded");
        std::sort(key.points.begin(),key.points.end(),[](const auto& a,const auto& b){return a.id<b.id;});
        for (std::size_t i = 0; i < key.points.size(); ++i) {
            const auto& p = key.points[i];
            if ((i && key.points[i-1].id == p.id) || !vectorValid(p.position,-100000,100000)
                || !vectorValid(p.color,0,10000) || !finite(p.intensity,0,100000) || !finite(p.radius,.0001f,100))
                return fail("Invalid formation point ID/position/color/intensity/radius");
        }
        const auto& first = data.formations.front().points;
        if (first.size() != key.points.size()) return fail("Formation point IDs must match");
        for (std::size_t i = 0; i < first.size(); ++i)
            if (first[i].id != key.points[i].id) return fail("Formation point IDs must match");
    }
    if (error) error->clear();
    return std::shared_ptr<const CompiledLightShow>(new CompiledLightShow(std::move(data)));
}

bool CompiledLightShow::evaluate(double time, LightShowFrame& out, bool procedural) const {
    if (!std::isfinite(time) || time < 0 || time > _data.duration) return false;
    out.time = time; out.fixtures.resize(_data.fixtures.size());
    for (std::size_t i = 0; i < _data.fixtures.size(); ++i) {
        const auto& track = _data.fixtures[i];
        const auto a = lowerKey(track.keys,time), b = std::min(a+1,track.keys.size()-1);
        auto state = mix(track.keys[a].value, track.keys[b].value,
            weight(time,track.keys[a].time,track.keys[b].time,track.keys[a].interpolation));
        const double phase = tau * track.sweepFrequency * time + track.phase;
        if(procedural) {
        state.pan += track.sweepPan * static_cast<float>(std::sin(phase));
        state.tilt += track.sweepTilt * static_cast<float>(std::cos(phase));
        if (track.strobeFrequency > 0) {
            const double cycle = time * track.strobeFrequency + track.phase / tau;
            if (cycle - std::floor(cycle) >= track.strobeDuty) state.intensity = 0;
        }
        }
        out.fixtures[i] = {track.id,state};
    }
    if (_data.formations.empty()) { out.points.clear(); return true; }
    const auto a = lowerKey(_data.formations,time), b = std::min(a+1,_data.formations.size()-1);
    const auto& from = _data.formations[a]; const auto& to = _data.formations[b];
    const auto t = weight(time,from.time,to.time,from.interpolation);
    out.points.resize(from.points.size());
    for (std::size_t i = 0; i < out.points.size(); ++i) {
        const auto& p = from.points[i]; const auto& q = to.points[i];
        out.points[i] = {p.id,mix(p.position,q.position,t),mix(p.color,q.color,t),
            mix(p.intensity,q.intensity,t),mix(p.radius,q.radius,t)};
    }
    return true;
}
void LightShowPlayer::bind(std::shared_ptr<const CompiledLightShow> show) {
    _show = std::move(show); _time = 0; _playing = false;
}
bool LightShowPlayer::seek(double time) {
    if (!_show || !std::isfinite(time) || time < 0 || time > _show->data().duration) return false;
    _time = time; return true;
}
bool LightShowPlayer::advance(double delta) {
    if (!_show || !std::isfinite(delta) || delta < 0) return false;
    if (!_playing) return true;
    const auto duration = _show->data().duration;
    if (_show->data().loop) _time = std::fmod(_time + std::fmod(delta,duration),duration);
    else { _time = std::min(duration,_time+delta); if (_time == duration) _playing = false; }
    return true;
}
bool LightShowPlayer::evaluate(LightShowFrame& out) const { return _show && _show->evaluate(_time,out); }

LightShow makeLightShowExample(std::uint32_t fixtureCount, std::uint32_t pointCount) {
    LightShow show; show.name = "Stage and formation"; show.duration = 16;
    fixtureCount = std::min(fixtureCount,256u); pointCount = std::min(pointCount,65536u);
    for (std::uint32_t i = 0; i < fixtureCount; ++i) {
        const float p = static_cast<float>(tau*i/std::max(1u,fixtureCount));
        FixtureTrack track; track.id=i; track.group=i%2?"left":"right";
        FixtureState state; state.position={8*std::sin(p),7,8*std::cos(p)};
        state.pan = p + static_cast<float>(std::numbers::pi); state.tilt = .5f;
        state.color={.5f+.5f*std::sin(p),.5f+.5f*std::sin(p+2.1f),.5f+.5f*std::sin(p+4.2f)};
        state.intensity=600; state.volumetric=8; state.shadow=i<4; state.gobo=static_cast<ShowGobo>(i%5);
        track.sweepPan=.5f; track.sweepTilt=.15f; track.sweepFrequency=.1f; track.phase=p;
        track.keys={{0,state}}; show.fixtures.push_back(std::move(track));
    }
    if (pointCount) {
        for (int frame = 0; frame < 3; ++frame) {
            FormationKey key; key.time=frame*8.0; key.points.reserve(pointCount);
            for (std::uint32_t i=0;i<pointCount;++i) {
                const float a=static_cast<float>(tau*i/pointCount);
                const float h=1-2*(static_cast<float>(i)+.5f)/pointCount;
                ShowPoint p; p.id=i;
                p.intensity=8;p.radius=.06f;
                const float ring=std::sqrt(std::max(0.f,1-h*h));
                const float spiral=static_cast<float>(i*2.399963229728653);
                p.position=frame==1?ShowVec3{12*ring*std::cos(spiral),18+12*h,12*ring*std::sin(spiral)}
                    :ShowVec3{12*std::cos(a),18+6*std::sin(a),0};
                p.color={.5f+.5f*std::sin(a+frame*2.f),.5f+.5f*std::sin(a+2.1f),.5f+.5f*std::sin(a+4.2f)};
                key.points.push_back(p);
            }
            show.formations.push_back(std::move(key));
        }
    }
    return show;
}
} // namespace ayt::sequence
