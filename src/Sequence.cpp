#include <AYSequence/Sequence.h>
#include <AYAssetFormat/AssetFormat.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ayt::sequence {
namespace {
bool fail(Diagnostic* e, std::string where, std::string message) {
    if (e) *e = {std::move(where), std::move(message)};
    return false;
}
bool idValid(const std::string& id) {
    return !id.empty() && id.size() <= 128 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
    });
}
bool timeValid(double t, double end) { return std::isfinite(t) && t >= 0 && t <= end; }
bool pathValid(const std::string& p, std::string_view suffix = {}) {
    if (p.empty() || p.size() > 1024 || p.front() == '/') return false;
    if (suffix.empty() ? !ayt::asset_format::matchesPath(p, ayt::asset_format::Id::Animation) : !p.ends_with(suffix)) return false;
    if (p.find_first_of("\\:*?\"<>|\0", 0, 9) != std::string::npos) return false;
    for (unsigned char c : p) if (c < 32) return false;
    std::size_t from = 0;
    while (from < p.size()) {
        auto to = p.find('/', from);
        auto part = p.substr(from, to == std::string::npos ? to : to - from);
        if (part.empty() || part == "." || part == "..") return false;
        if (to == std::string::npos) break;
        from = to + 1;
    }
    return true;
}
Value sample(const TransformTrack& track, double time) {
    const auto& keys = track.keys;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    auto right = std::upper_bound(keys.begin(), keys.end(), time, [](double t, const Key& k) { return t < k.time; });
    const auto& a = *(right - 1); const auto& b = *right;
    const double t = (time - a.time) / (b.time - a.time);
    Value out{};
    if (track.channel != Channel::Rotation) {
        for (int i = 0; i < 3; ++i) out[i] = a.value[i] + t * (b.value[i] - a.value[i]);
    } else {
        // Normalized shortest-arc SLERP, not four independent component curves.
        double dot = 0; for (int i = 0; i < 4; ++i) dot += a.value[i] * b.value[i];
        const double sign = dot < 0 ? -1 : 1;
        dot = std::clamp(std::abs(dot), 0.0, 1.0);
        double wa = 1 - t, wb = t;
        if (dot < 0.9995) {
            const double angle = std::acos(dot), denom = std::sin(angle);
            wa = std::sin((1 - t) * angle) / denom; wb = std::sin(t * angle) / denom;
        }
        double norm = 0;
        for (int i = 0; i < 4; ++i) { out[i] = wa * a.value[i] + wb * sign * b.value[i]; norm += out[i] * out[i]; }
        for (auto& v : out) v /= std::sqrt(norm);
    }
    return out;
}
}

bool validate(const Sequence& s, Diagnostic* e) {
    if (e) *e = {};
    if (!idValid(s.id)) return fail(e, "id", "Invalid sequence ID");
    if (!s.scenePath.empty()) {
        const auto matched = ayt::asset_format::matchPath(s.scenePath);
        if (!matched || matched->descriptor->id != ayt::asset_format::Id::Scene
            || !pathValid(s.scenePath, matched->suffix)) {
            return fail(e, "scenePath", "Expected portable relative Scene path");
        }
    }
    if (!std::isfinite(s.duration) || s.duration <= 0 || s.duration > 86400) return fail(e, "duration", "Expected 0 < duration <= 86400 seconds");
    if (!std::isfinite(s.displayFps) || s.displayFps < 1 || s.displayFps > 240) return fail(e, "displayFps", "Expected display rate in [1,240]");
    if (s.bindings.size() > 1024 || s.transforms.size() + s.animations.size() + s.eventTracks.size() > 4096
        || s.cameraCuts.size() > 1024 || s.audioCues.size() > 10000
        || s.textCues.size() > 10000)
        return fail(e, "tracks", "Sequence exceeds binding/track/cue limits");
    std::unordered_set<std::string> ids{s.id}, bindings, channels;
    auto addId = [&](const std::string& id) { return idValid(id) && ids.insert(id).second; };
    for (const auto& b : s.bindings) {
        if (!addId(b.id)) return fail(e, b.id, "Invalid/duplicate ID");
        if (b.entityName.size() > 256 || std::any_of(b.entityName.begin(), b.entityName.end(), [](unsigned char c) { return c < 32; })) return fail(e, b.id, "Invalid entity name locator");
        bindings.insert(b.id);
    }
    std::size_t keyCount = 0, sectionCount = 0, eventCount = 0;
    for (const auto& tr : s.transforms) {
        if (tr.channel != Channel::Position && tr.channel != Channel::Rotation && tr.channel != Channel::Scale) return fail(e, tr.id, "Unsupported transform channel");
        if (!addId(tr.id) || !bindings.contains(tr.binding)) return fail(e, tr.id, "Invalid track ID or missing binding");
        if (!channels.insert(tr.binding + "/" + std::to_string(static_cast<int>(tr.channel))).second) return fail(e, tr.id, "Duplicate authoritative channel");
        keyCount += tr.keys.size();
        if (tr.keys.empty() || keyCount > 100000) return fail(e, tr.id, "Empty/excessive key data");
        double previous = -1;
        for (const auto& k : tr.keys) {
            if (!addId(k.id) || !timeValid(k.time, s.duration) || k.time <= previous) return fail(e, k.id, "Invalid ID/time or unsorted/duplicate key time");
            previous = k.time;
            for (double v : k.value) if (!std::isfinite(v) || std::abs(v) > 1e6) return fail(e, k.id, "Non-finite/out-of-range value");
            if (tr.channel == Channel::Rotation) {
                double norm = 0; for (double v : k.value) norm += v * v;
                if (std::abs(norm - 1) > 1e-6) return fail(e, k.id, "Quaternion must be unit length");
            } else {
                if (k.value[3] != 0) return fail(e, k.id, "Vector channel must have zero fourth component");
                if (tr.channel == Channel::Scale && (k.value[0] < 1e-6 || k.value[1] < 1e-6 || k.value[2] < 1e-6)) return fail(e, k.id, "Scale must be at least 1e-6");
            }
        }
    }
    for (const auto& tr : s.animations) {
        if (!addId(tr.id) || !bindings.contains(tr.binding) || !channels.insert(tr.binding + "/animation").second) return fail(e, tr.id, "Invalid animation track or duplicate channel");
        sectionCount += tr.sections.size();
        if (tr.sections.empty() || sectionCount > 10000) return fail(e, tr.id, "Empty/excessive sections");
        double previousEnd = 0;
        for (const auto& sec : tr.sections) {
            if (!addId(sec.id) || !pathValid(sec.source)) return fail(e, sec.id, "Invalid section ID or non-portable .anm path");
            if (!timeValid(sec.start, s.duration) || !timeValid(sec.end, s.duration) || sec.end <= sec.start || sec.start < previousEnd) return fail(e, sec.id, "Invalid/overlapping section range");
            if (!std::isfinite(sec.speed) || sec.speed <= 0 || sec.speed > 240 || !std::isfinite(sec.sourceDuration) || sec.sourceDuration <= 0 || sec.sourceDuration > 86400 || !timeValid(sec.sourceOffset, sec.sourceDuration)) return fail(e, sec.id, "Invalid source time/speed");
            if ((sec.end - sec.start) * sec.speed > sec.sourceDuration - sec.sourceOffset + 1e-9) return fail(e, sec.id, "Section exceeds declared source duration");
            previousEnd = sec.end;
        }
    }
    for (const auto& tr : s.eventTracks) {
        if (!addId(tr.id)) return fail(e, tr.id, "Invalid event track ID");
        eventCount += tr.events.size();
        if (eventCount > 10000) return fail(e, tr.id, "Too many events");
        double previous = -1;
        for (const auto& ev : tr.events) {
            if (!addId(ev.id) || !timeValid(ev.time, s.duration) || ev.time < previous || !idValid(ev.name) || ev.payload.size() > 4096 || ev.payload.find('\0') != std::string::npos) return fail(e, ev.id, "Invalid event ID/time/name/payload");
            previous = ev.time;
        }
    }
    double previousCut = -1;
    for (const auto& cut : s.cameraCuts) {
        if (!addId(cut.id) || !bindings.contains(cut.binding)
            || !timeValid(cut.time, s.duration) || cut.time <= previousCut
            || !std::isfinite(cut.blendSeconds) || cut.blendSeconds < 0
            || cut.blendSeconds > s.duration)
            return fail(e, cut.id, "Invalid camera cut ID, binding, time or blend");
        previousCut = cut.time;
    }
    double previousAudio = -1;
    for (const auto& cue : s.audioCues) {
        const auto matched = ayt::asset_format::matchPath(cue.source);
        if (!addId(cue.id) || !timeValid(cue.time, s.duration)
            || cue.time < previousAudio || !matched
            || matched->descriptor->id != ayt::asset_format::Id::Audio
            || !pathValid(cue.source, matched->suffix)
            || !std::isfinite(cue.gain) || cue.gain < 0 || cue.gain > 4)
            return fail(e, cue.id, "Invalid audio cue ID, time, asset or gain");
        previousAudio = cue.time;
    }
    if (eventCount + s.audioCues.size() > 10000)
        return fail(e, "cues", "Too many timed events and audio cues");
    double previousText = -1;
    for (const auto& cue : s.textCues) {
        if (!addId(cue.id) || !idValid(cue.textKey)
            || (!cue.speakerKey.empty() && !idValid(cue.speakerKey))
            || !timeValid(cue.start, s.duration)
            || !timeValid(cue.end, s.duration) || cue.end <= cue.start
            || cue.start < previousText)
            return fail(e, cue.id, "Invalid text cue ID, key or interval");
        previousText = cue.start;
    }
    return true;
}
CompiledSequence::CompiledSequence(Sequence s) : _data(std::move(s)) {
    for (const auto& tr : _data.eventTracks) _events.insert(_events.end(), tr.events.begin(), tr.events.end());
    std::stable_sort(_events.begin(), _events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
}
std::shared_ptr<const CompiledSequence> CompiledSequence::compile(Sequence s, Diagnostic* e) {
    if (!validate(s, e)) return {};
    return std::shared_ptr<const CompiledSequence>(new CompiledSequence(std::move(s)));
}
bool CompiledSequence::evaluate(double time, Evaluation& output, Diagnostic* e) const {
    if (!timeValid(time, _data.duration)) return fail(e, "time", "Time outside sequence");
    Evaluation next;
    next.transforms.reserve(_data.transforms.size()); next.animations.reserve(_data.animations.size());
    for (const auto& tr : _data.transforms) next.transforms.push_back({tr.binding, tr.channel, sample(tr, time)});
    for (const auto& tr : _data.animations) {
        auto right = std::upper_bound(tr.sections.begin(), tr.sections.end(), time, [](double t, const AnimationSection& s) { return t < s.start; });
        if (right == tr.sections.begin()) continue;
        const auto& sec = *(right - 1);
        if (time < sec.end || (time == _data.duration && sec.end == time)) next.animations.push_back({tr.binding, sec.id, sec.source, std::min(sec.sourceDuration, sec.sourceOffset + (time - sec.start) * sec.speed)});
    }
    const auto rightCut = std::upper_bound(_data.cameraCuts.begin(), _data.cameraCuts.end(),
        time, [](double t, const CameraCut& cut) { return t < cut.time; });
    if (rightCut != _data.cameraCuts.begin()) {
        const auto& cut = *(rightCut - 1);
        next.camera = CameraSample{cut.id, cut.binding, cut.blendSeconds};
    }
    for (const auto& cue : _data.textCues) {
        if (cue.start > time) break;
        if (time < cue.end) next.textCues.push_back(cue);
    }
    output = std::move(next); if (e) *e = {}; return true;
}
void SequencePlayer::bind(std::shared_ptr<const CompiledSequence> s) { _sequence = std::move(s); stop(); }
bool SequencePlayer::play() {
    if (!_sequence) return false;
    if (_state == PlaybackState::Playing) return true;
    if (_state == PlaybackState::Completed || _position == _sequence->data().duration) _position = 0;
    if (_state != PlaybackState::Paused) _includeOrigin = true;
    _state = PlaybackState::Playing; return true;
}
void SequencePlayer::pause() noexcept { if (_state == PlaybackState::Playing) _state = PlaybackState::Paused; }
void SequencePlayer::stop() noexcept { _state = PlaybackState::Stopped; _position = 0; _includeOrigin = false; }
bool SequencePlayer::seek(double t, PlaybackFrame& out, Diagnostic* e) {
    PlaybackFrame next;
    if (!_sequence) return fail(e, "sequence", "No bound sequence");
    if (!_sequence->evaluate(t, next.evaluation, e)) return false;
    _position = t; _includeOrigin = false;
    if (_state == PlaybackState::Completed) _state = PlaybackState::Paused;
    out = std::move(next); return true;
}
bool SequencePlayer::advance(double dt, PlaybackFrame& out, Diagnostic* e) {
    if (!_sequence || !std::isfinite(dt) || dt < 0) return fail(e, "delta", "Expected bound sequence and nonnegative finite delta");
    double nextTime = _position;
    if (_state == PlaybackState::Playing) {
        const double remaining = _sequence->data().duration - _position;
        nextTime = dt >= remaining ? _sequence->data().duration : _position + dt;
    }
    PlaybackFrame next;
    if (!_sequence->evaluate(nextTime, next.evaluation, e)) return false;
    if (_state == PlaybackState::Playing && dt > 0) {
        const auto& events = _sequence->orderedEvents();
        auto begin = _includeOrigin ? std::lower_bound(events.begin(), events.end(), _position, [](const Event& ev, double t) { return ev.time < t; }) : std::upper_bound(events.begin(), events.end(), _position, [](double t, const Event& ev) { return t < ev.time; });
        for (auto it = begin; it != events.end() && it->time <= nextTime; ++it) next.events.push_back(*it);
        const auto& audio = _sequence->data().audioCues;
        auto audioBegin = _includeOrigin ? std::lower_bound(audio.begin(), audio.end(), _position, [](const AudioCue& cue, double t) { return cue.time < t; }) : std::upper_bound(audio.begin(), audio.end(), _position, [](double t, const AudioCue& cue) { return t < cue.time; });
        for (auto it = audioBegin; it != audio.end() && it->time <= nextTime; ++it) next.audioCues.push_back(*it);
        _includeOrigin = false;
        if (nextTime == _sequence->data().duration) { _state = PlaybackState::Completed; next.completed = true; }
    }
    _position = nextTime; out = std::move(next); return true;
}
} // namespace ayt::sequence
