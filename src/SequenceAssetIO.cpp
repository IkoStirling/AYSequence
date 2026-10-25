#include <AYSequence/SequenceAssetIO.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYIO/File.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace ayt::sequence {
namespace {
using Json = nlohmann::json;
constexpr std::size_t maxBytes = 16 * 1024 * 1024;
bool fail(Diagnostic* e, const std::string& message) { if (e) *e = {"asset", message}; return false; }
void fields(const Json& j, std::initializer_list<const char*> names) {
    if (!j.is_object() || j.size() != names.size()) throw std::runtime_error("Unexpected/missing object fields");
    for (auto n : names) if (!j.contains(n)) throw std::runtime_error(std::string("Missing field: ") + n);
}
void arrayLimit(const Json& j, std::size_t limit) { if (!j.is_array() || j.size() > limit) throw std::runtime_error("Invalid/excessive array"); }
double number(const Json& j) { if (!j.is_number()) throw std::runtime_error("Expected number"); return j.get<double>(); }
bool filePath(const std::string& path) { return !path.empty() && path.find('\0') == std::string::npos && ayt::asset_format::matchesPath(path, ayt::asset_format::Id::SceneSequence); }
}
bool decodeSequence(std::string_view bytes, Sequence& output, Diagnostic* e) {
    if (bytes.size() > maxBytes) return fail(e, "Sequence file exceeds 16 MiB");
    try {
        std::vector<std::unordered_set<std::string>> objectKeys;
        auto j = Json::parse(bytes.begin(), bytes.end(), [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 32) throw std::runtime_error("JSON nesting exceeds 32");
            if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
            if (event == Json::parse_event_t::key && !objectKeys.back().insert(value.get<std::string>()).second) throw std::runtime_error("Duplicate JSON key");
            if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
            return true;
        });
        if (j.at("type") != "authoring.scene-sequence" || !j.at("version").is_number_integer()
            || (j.at("version") != 1 && j.at("version") != 2 && j.at("version") != 3))
            return fail(e, "Unsupported sequence type/version");
        const bool cues = j.at("version") == 3;
        const bool authoring = j.at("version") == 2 || cues;
        if (cues) fields(j, {"type", "version", "id", "duration", "displayFps", "bindings", "transforms", "animations", "eventTracks", "scenePath", "cameraCuts", "audioCues"});
        else if (authoring) fields(j, {"type", "version", "id", "duration", "displayFps", "bindings", "transforms", "animations", "eventTracks", "scenePath"});
        else fields(j, {"type", "version", "id", "duration", "displayFps", "bindings", "transforms", "animations", "eventTracks"});
        Sequence s; s.id = j.at("id").get<std::string>(); s.duration = number(j.at("duration")); s.displayFps = number(j.at("displayFps"));
        if (authoring) s.scenePath = j.at("scenePath").get<std::string>();
        arrayLimit(j.at("bindings"), 1024); arrayLimit(j.at("transforms"), 4096); arrayLimit(j.at("animations"), 4096); arrayLimit(j.at("eventTracks"), 4096);
        if (cues) { arrayLimit(j.at("cameraCuts"), 1024); arrayLimit(j.at("audioCues"), 10000); }
        for (const auto& b : j.at("bindings")) {
            if (authoring) fields(b, {"id", "entityName"}); else fields(b, {"id"});
            s.bindings.push_back({b.at("id").get<std::string>(), authoring ? b.at("entityName").get<std::string>() : std::string{}});
        }
        std::size_t keys = 0, sections = 0, events = 0;
        for (const auto& tr : j.at("transforms")) {
            fields(tr, {"id", "binding", "channel", "keys"});
            TransformTrack t; t.id = tr.at("id"); t.binding = tr.at("binding");
            const std::string c = tr.at("channel");
            if (c == "position") t.channel = Channel::Position; else if (c == "rotation") t.channel = Channel::Rotation; else if (c == "scale") t.channel = Channel::Scale; else throw std::runtime_error("Unsupported channel");
            arrayLimit(tr.at("keys"), 100000); keys += tr.at("keys").size(); if (keys > 100000) throw std::runtime_error("Too many keys");
            for (const auto& k : tr.at("keys")) {
                fields(k, {"id", "time", "value"}); arrayLimit(k.at("value"), 4);
                if (k.at("value").size() != (t.channel == Channel::Rotation ? 4u : 3u)) throw std::runtime_error("Value component count mismatch");
                Key v; v.id = k.at("id"); v.time = number(k.at("time")); for (std::size_t i = 0; i < k.at("value").size(); ++i) v.value[i] = number(k.at("value")[i]); t.keys.push_back(std::move(v));
            }
            s.transforms.push_back(std::move(t));
        }
        for (const auto& tr : j.at("animations")) {
            fields(tr, {"id", "binding", "sections"}); arrayLimit(tr.at("sections"), 10000); sections += tr.at("sections").size(); if (sections > 10000) throw std::runtime_error("Too many sections");
            AnimationTrack t; t.id = tr.at("id"); t.binding = tr.at("binding");
            for (const auto& v : tr.at("sections")) {
                fields(v, {"id", "source", "start", "end", "sourceOffset", "speed", "sourceDuration"});
                t.sections.push_back({v.at("id"), v.at("source"), number(v.at("start")), number(v.at("end")), number(v.at("sourceOffset")), number(v.at("speed")), number(v.at("sourceDuration"))});
            }
            s.animations.push_back(std::move(t));
        }
        for (const auto& tr : j.at("eventTracks")) {
            fields(tr, {"id", "events"}); arrayLimit(tr.at("events"), 10000); events += tr.at("events").size(); if (events > 10000) throw std::runtime_error("Too many events");
            EventTrack t; t.id = tr.at("id");
            for (const auto& v : tr.at("events")) { fields(v, {"id", "time", "name", "payload"}); t.events.push_back({v.at("id"), number(v.at("time")), v.at("name"), v.at("payload")}); }
            s.eventTracks.push_back(std::move(t));
        }
        if (cues) {
            for (const auto& cut : j.at("cameraCuts")) {
                fields(cut, {"id", "binding", "time", "blendSeconds"});
                s.cameraCuts.push_back({cut.at("id"), cut.at("binding"),
                    number(cut.at("time")), number(cut.at("blendSeconds"))});
            }
            for (const auto& cue : j.at("audioCues")) {
                fields(cue, {"id", "source", "time", "gain"});
                s.audioCues.push_back({cue.at("id"), cue.at("source"),
                    number(cue.at("time")), number(cue.at("gain"))});
            }
        }
        if (!validate(s, e)) return false;
        output = std::move(s); return true;
    } catch (const std::exception& ex) { return fail(e, ex.what()); }
}
bool encodeSequence(const Sequence& s, std::string& output, Diagnostic* e) {
    if (!validate(s, e)) return false;
    try {
        Json j = {{"type", "authoring.scene-sequence"}, {"version", 1}, {"id", s.id}, {"duration", s.duration}, {"displayFps", s.displayFps}, {"bindings", Json::array()}, {"transforms", Json::array()}, {"animations", Json::array()}, {"eventTracks", Json::array()}};
        const bool cues = !s.cameraCuts.empty() || !s.audioCues.empty();
        const bool authoring = cues || !s.scenePath.empty() || std::any_of(s.bindings.begin(), s.bindings.end(), [](const Binding& b) { return !b.entityName.empty(); });
        if (authoring) { j["version"] = cues ? 3 : 2; j["scenePath"] = s.scenePath; }
        for (const auto& b : s.bindings) {
            Json binding = {{"id", b.id}};
            if (authoring) binding["entityName"] = b.entityName;
            j["bindings"].push_back(std::move(binding));
        }
        for (const auto& tr : s.transforms) {
            Json keys = Json::array();
            for (const auto& k : tr.keys) { Json value = Json::array(); for (int i = 0; i < (tr.channel == Channel::Rotation ? 4 : 3); ++i) value.push_back(k.value[i]); keys.push_back({{"id", k.id}, {"time", k.time}, {"value", value}}); }
            j["transforms"].push_back({{"id", tr.id}, {"binding", tr.binding}, {"channel", tr.channel == Channel::Position ? "position" : tr.channel == Channel::Rotation ? "rotation" : "scale"}, {"keys", keys}});
        }
        for (const auto& tr : s.animations) {
            Json sections = Json::array();
            for (const auto& v : tr.sections) sections.push_back({{"id", v.id}, {"source", v.source}, {"start", v.start}, {"end", v.end}, {"sourceOffset", v.sourceOffset}, {"speed", v.speed}, {"sourceDuration", v.sourceDuration}});
            j["animations"].push_back({{"id", tr.id}, {"binding", tr.binding}, {"sections", sections}});
        }
        for (const auto& tr : s.eventTracks) {
            Json events = Json::array(); for (const auto& v : tr.events) events.push_back({{"id", v.id}, {"time", v.time}, {"name", v.name}, {"payload", v.payload}});
            j["eventTracks"].push_back({{"id", tr.id}, {"events", events}});
        }
        if (cues) {
            j["cameraCuts"] = Json::array();
            for (const auto& cut : s.cameraCuts)
                j["cameraCuts"].push_back({{"id", cut.id}, {"binding", cut.binding},
                    {"time", cut.time}, {"blendSeconds", cut.blendSeconds}});
            j["audioCues"] = Json::array();
            for (const auto& cue : s.audioCues)
                j["audioCues"].push_back({{"id", cue.id}, {"source", cue.source},
                    {"time", cue.time}, {"gain", cue.gain}});
        }
        std::string bytes = j.dump(2);
        if (bytes.size() > maxBytes) return fail(e, "Encoded sequence exceeds 16 MiB");
        output = std::move(bytes); return true;
    } catch (const std::exception& ex) { return fail(e, ex.what()); }
}
bool loadSequence(const std::string& path, Sequence& output, Diagnostic* e) {
    if (!filePath(path)) return fail(e, "Expected explicit .seq file path");
    ayt::io::File file(path, ayt::io::File::Mode::BinaryRead);
    const auto size = file.size();
    if (!file.isOpen() || size > maxBytes) return fail(e, "Cannot open file or file too large");
    std::string bytes(size, '\0');
    if (file.read(bytes.data(), bytes.size()) != bytes.size()) return fail(e, "Incomplete file read");
    return decodeSequence(bytes, output, e);
}
bool saveSequence(const std::string& path, const Sequence& s, Diagnostic* e) {
    if (!filePath(path) || !ayt::asset_format::isCanonicalPath(path, ayt::asset_format::Id::SceneSequence)) return fail(e, "Expected explicit .seq file path");
    std::string bytes; if (!encodeSequence(s, bytes, e)) return false;
    if (!ayt::io::File::atomicWrite(path, bytes.data(), bytes.size())) return fail(e, "Atomic sequence write failed");
    return true;
}
} // namespace ayt::sequence
