#include "LightShowFogInternal.h"
#include <AYSequence/LightShow.h>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <filesystem>
#include <unordered_map>
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
    out.apertureRadius = mix(a.apertureRadius,b.apertureRadius,t);
    out.focusDistance=mix(a.focusDistance,b.focusDistance,t);
    out.prismAngle=mix(a.prismAngle,b.prismAngle,t);out.prismRotation=mix(a.prismRotation,b.prismRotation,t);
    out.areaShadowBias=mix(a.areaShadowBias,b.areaShadowBias,t);
    out.shadowSoftness = mix(a.shadowSoftness,b.shadowSoftness,t);
    out.goboRotation = mix(a.goboRotation,b.goboRotation,t); out.goboScale = mix(a.goboScale,b.goboScale,t);
    out.goboFocus = mix(a.goboFocus,b.goboFocus,t); out.iris = mix(a.iris,b.iris,t);
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
        && finite(s.volumetric,0,100) && finite(s.apertureRadius,0,1)
        && finite(s.shadowSoftness,0,4) && finite(s.areaShadowBias,0,.1f)
        && finite(s.goboRotation,-100000,100000) && finite(s.goboScale,.01f,100)
        && finite(s.goboFocus,0,1) && finite(s.iris,.01f,1)
        && finite(s.focusDistance,0,10000)&&s.prismCount>=1&&s.prismCount<=8
        && finite(s.prismAngle,0,.35f)&&finite(s.prismRotation,-100000,100000)
        && (s.outerAngle+s.prismAngle<=1.56f)
        && s.gobo <= ShowGobo::Texture && s.intensityUnit <= ShowIntensityUnit::Lumen;
}
}

std::shared_ptr<const CompiledLightShow> CompiledLightShow::compile(LightShow data, std::string* error) {
    const auto fail = [&](const std::string& message) -> std::shared_ptr<const CompiledLightShow> {
        if (error) *error = message; return {};
    };
    if (!std::isfinite(data.duration) || data.duration <= 0 || data.duration > 86400
        || data.name.size() > 256 || data.fixtures.size() > 256 || data.formations.size() > 4096)
        return fail("Invalid show duration/name or collection limit");
    if (data.fixtures.empty() && data.formations.empty() && data.fog.keys.empty()) return fail("Show has no authored tracks");
    if(!detail::validateFogTrack(data.fog,data.duration,error))return {};
    std::size_t samples=data.fog.keys.size()+data.fog.grid.values.size();
    std::unordered_set<std::uint32_t> fixtureIds;
    for (const auto& track : data.fixtures) {
        if (!fixtureIds.insert(track.id).second || track.group.size() > 128
            || track.keys.empty() || track.keys.size() > 65536)
            return fail("Invalid fixture ID/group/key count");
        if (track.goboTexturePath.size() > 1024 || track.goboTexturePath.find('\0') != std::string::npos)
            return fail("Invalid Gobo texture path");
        if (!track.goboTexturePath.empty()) {
            const std::filesystem::path path(track.goboTexturePath);
            if (path.is_absolute() || path.has_root_name() || path.has_root_directory()
                || std::find(path.begin(),path.end(),std::filesystem::path("..")) != path.end())
                return fail("Gobo texture path must stay relative to the show");
        }
        if(track.iesProfilePath.size()>1024||track.iesProfilePath.find(char(0))!=std::string::npos)return fail("Invalid IES path");
        if(!track.iesProfilePath.empty()){const auto p=std::filesystem::u8path(track.iesProfilePath);if(p.is_absolute()||p.has_root_name()||p.has_root_directory()||std::find(p.begin(),p.end(),std::filesystem::path(".."))!=p.end())return fail("IES path must stay relative to the show");}
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
            if (key.value.gobo == ShowGobo::Texture && track.goboTexturePath.empty())
                return fail("Texture Gobo requires a fixture texture path");
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
    out.fog.authored=!_data.fog.keys.empty();out.fog.value=out.fog.authored?detail::evaluateFogTrack(_data.fog,time):ShowFogState{};out.fog.grid=out.fog.authored?_fogGrid:nullptr;
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
        out.fixtures[i] = {track.id,state,!track.iesProfilePath.empty()};
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

bool CompiledLightShow::analyzeFormationClearance(float minimumDistance,
        std::size_t maxPairChecks, std::size_t maxConflicts,
        FormationClearanceReport& output) const {
    if (!std::isfinite(minimumDistance) || minimumDistance <= 0 || !maxPairChecks || !maxConflicts)
        return false;
    FormationClearanceReport result;
    struct Bounds { double minimum, maximum; std::size_t index; };
    const auto& keys = _data.formations;
    for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex) {
        const auto& from = keys[keyIndex];
        const auto& to = keyIndex + 1 < keys.size() && from.interpolation != ShowInterpolation::Step
            ? keys[keyIndex + 1] : from;
        const bool moving = &from != &to;
        std::vector<Bounds> xBounds;
        xBounds.reserve(from.points.size());
        for (std::size_t i = 0; i < from.points.size(); ++i) {
            const double a = from.points[i].position[0], b = to.points[i].position[0];
            xBounds.push_back({std::min(a,b) - minimumDistance,
                               std::max(a,b) + minimumDistance, i});
        }
        std::sort(xBounds.begin(), xBounds.end(), [](const Bounds& a, const Bounds& b) {
            return a.minimum < b.minimum || (a.minimum == b.minimum && a.index < b.index);
        });
        for (std::size_t i = 0; i < xBounds.size() && result.complete; ++i) {
            for (std::size_t j = i + 1; j < xBounds.size() &&
                 xBounds[j].minimum <= xBounds[i].maximum; ++j) {
                if (result.checkedPairs == maxPairChecks) { result.complete = false; break; }
                ++result.checkedPairs;
                const auto ai = xBounds[i].index, bi = xBounds[j].index;
                const auto& a = from.points[ai].position;
                const auto& b = from.points[bi].position;
                const auto& ae = to.points[ai].position;
                const auto& be = to.points[bi].position;
                bool disjoint = false;
                for (int axis = 1; axis < 3; ++axis) {
                    const double amin = std::min(a[axis],ae[axis]) - minimumDistance;
                    const double amax = std::max(a[axis],ae[axis]) + minimumDistance;
                    const double bmin = std::min(b[axis],be[axis]);
                    const double bmax = std::max(b[axis],be[axis]);
                    if (amax < bmin || bmax < amin) { disjoint = true; break; }
                }
                if (disjoint) continue;
                double position[3], velocity[3], dot = 0, speed2 = 0;
                for (int axis = 0; axis < 3; ++axis) {
                    position[axis] = static_cast<double>(a[axis]) - b[axis];
                    velocity[axis] = static_cast<double>(ae[axis]) - a[axis] - be[axis] + b[axis];
                    dot += position[axis] * velocity[axis];
                    speed2 += velocity[axis] * velocity[axis];
                }
                const double fraction = moving && speed2 > 0 ? std::clamp(-dot / speed2, 0.0, 1.0) : 0.0;
                double distance2 = 0;
                for (int axis = 0; axis < 3; ++axis)
                    distance2 += (position[axis] + fraction * velocity[axis]) *
                                 (position[axis] + fraction * velocity[axis]);
                if (distance2 >= static_cast<double>(minimumDistance) * minimumDistance) continue;
                double timeFraction = fraction;
                if (moving && from.interpolation == ShowInterpolation::Smooth) {
                    double low = 0, high = 1;
                    for (int step = 0; step < 48; ++step) {
                        const double middle = (low + high) * .5;
                        if (middle * middle * (3 - 2 * middle) < fraction) low = middle;
                        else high = middle;
                    }
                    timeFraction = (low + high) * .5;
                }
                result.conflicts.push_back({from.points[ai].id,from.points[bi].id,
                    from.time + (moving ? timeFraction * (keys[keyIndex + 1].time - from.time) : 0),
                    static_cast<float>(std::sqrt(distance2))});
                if (result.conflicts.size() == maxConflicts) { result.complete = false; break; }
            }
        }
        if (!result.complete) break;
    }
    output = std::move(result);
    return true;
}

bool planFormationTransition(const LightShow& source, std::size_t fromKeyIndex,
                             FormationRouteOptions options, LightShow& output,
                             std::string* error) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(!std::isfinite(options.minimumDistance)||options.minimumDistance<=0
        ||!std::isfinite(options.maxVerticalDetour)||options.maxVerticalDetour<=0
        ||options.maxVerticalDetour>100000
        ||!options.maxPairChecks||!options.maxConflicts)
        return fail("Invalid formation route options");
    auto compiled=CompiledLightShow::compile(source,error);
    if(!compiled)return false;
    const auto& data=compiled->data();
    if(fromKeyIndex>=data.formations.size()||fromKeyIndex+1>=data.formations.size())
        return fail("Formation transition index is out of range");
    const auto& from=data.formations[fromKeyIndex];
    const auto& to=data.formations[fromKeyIndex+1];
    if(from.interpolation==ShowInterpolation::Step)
        return fail("Step transition has no continuous route to plan");
    const double middleTime=from.time+(to.time-from.time)*.5;
    if(!(middleTime>from.time&&middleTime<to.time))
        return fail("Formation transition is too short for a middle key");

    LightShow preview;preview.duration=data.duration;preview.formations={from,to};
    const auto inspect=[&](const LightShow& show,FormationClearanceReport& report){
        auto snapshot=CompiledLightShow::compile(show,error);
        return snapshot&&snapshot->analyzeFormationClearance(options.minimumDistance,
            options.maxPairChecks,options.maxConflicts,report)&&report.complete;
    };
    FormationClearanceReport report;
    if(!inspect(preview,report))return fail("Formation route diagnostic budget was exhausted");
    if(report.conflicts.empty()){output=data;if(error)error->clear();return true;}
    const auto atEndpoint=[&](const FormationClearanceReport& value){
        return std::any_of(value.conflicts.begin(),value.conflicts.end(),[&](const auto& conflict){
            return std::abs(conflict.time-from.time)<1e-7
                ||std::abs(conflict.time-to.time)<1e-7;
        });
    };
    if(atEndpoint(report))return fail("Formation endpoint points are already too close");

    const auto count=from.points.size();
    std::unordered_map<std::uint32_t,std::size_t> index;
    index.reserve(count);
    for(std::size_t i=0;i<count;++i)index.emplace(from.points[i].id,i);
    std::vector<std::vector<std::size_t>> neighbors(count);
    const auto addConflicts=[&](const FormationClearanceReport& value){
        for(const auto& conflict:value.conflicts){
            const auto a=index.at(conflict.firstId),b=index.at(conflict.secondId);
            if(std::find(neighbors[a].begin(),neighbors[a].end(),b)==neighbors[a].end()){
                neighbors[a].push_back(b);neighbors[b].push_back(a);
            }
        }
    };
    addConflicts(report);
    for(int pass=0;pass<8;++pass){
        std::vector<int> colors(count,-1),used(count,-1);
        for(std::size_t i=0;i<count;++i){
            if(neighbors[i].empty())continue;
            for(const auto neighbor:neighbors[i])if(colors[neighbor]>=0)used[colors[neighbor]]=static_cast<int>(i);
            int color=0;while(used[color]==static_cast<int>(i))++color;
            colors[i]=color;
        }
        FormationKey middle;middle.time=middleTime;middle.interpolation=ShowInterpolation::Linear;
        middle.points=from.points;
        const float laneSpacing=options.minimumDistance*1.25f*float(1u<<pass);
        bool heightExceeded=false;
        for(std::size_t i=0;i<count;++i){
            for(int axis=0;axis<3;++axis)
                middle.points[i].position[axis]=.5f*(from.points[i].position[axis]+to.points[i].position[axis]);
            if(colors[i]<0)continue;
            const float lift=(colors[i]+1)*laneSpacing;
            if(lift>options.maxVerticalDetour||middle.points[i].position[1]+lift>100000){heightExceeded=true;break;}
            middle.points[i].position[1]+=lift;
        }
        if(heightExceeded)break;
        preview.formations={from,middle,to};
        preview.formations[0].interpolation=ShowInterpolation::Linear;
        if(!inspect(preview,report))return fail("Formation route diagnostic budget was exhausted");
        if(report.conflicts.empty()){
            LightShow planned=data;
            planned.formations[fromKeyIndex].interpolation=ShowInterpolation::Linear;
            planned.formations.insert(planned.formations.begin()+fromKeyIndex+1,std::move(middle));
            if(!CompiledLightShow::compile(planned,error))return false;
            output=std::move(planned);if(error)error->clear();return true;
        }
        if(atEndpoint(report))return fail("Formation endpoint points are already too close");
        addConflicts(report);
    }
    return fail("No continuously separated route fits the vertical detour budget");
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
bool LightShowPlayer::synchronize(double elapsedSeconds) {
    if (!_show || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0) return false;
    if (!_playing) return true;
    const auto duration = _show->data().duration;
    if (_show->data().loop) _time = std::fmod(elapsedSeconds,duration);
    else { _time = std::min(duration,elapsedSeconds); if (_time == duration) _playing = false; }
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
        state.intensity=400; state.volumetric=4; state.shadow=i<4;
        state.gobo=ShowGobo::Open;
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
