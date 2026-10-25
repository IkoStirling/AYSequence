#include <AYSequence/SceneSequenceSession.h>
#include <AYSequence/SequenceRuntimeModule.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/DetSimTransformComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <algorithm>
#include <stdexcept>

namespace ayt::sequence {
namespace {
using ayt::entity::World; using ayt::entity::EntityHandle; using ayt::entity::Transform;
struct Registry {
    bool debugCapture=false;
    std::uint64_t nextSession = 0;
    std::vector<SceneSequenceSession*> sessions;
    std::map<std::pair<World*, std::uint32_t>, SceneSequenceSession*> claims;
};
Registry& registry() { static Registry r; return r; }
bool same(const Transform& a, const Transform& b) {
    const auto vec = [](const auto& x, const auto& y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    const auto quat = [&](const auto& x, const auto& y) { return vec(x,y) && x.w == y.w; };
    return vec(a.position,b.position) && quat(a.rotation,b.rotation) && vec(a.scale,b.scale)
        && vec(a.previousPosition,b.previousPosition) && quat(a.previousRotation,b.previousRotation)
        && a.hasPreviousSimulationPose == b.hasPreviousSimulationPose && a.revision == b.revision;
}
Transform* transform(World& w, EntityHandle h) {
    // Check the World's stored handle/membership, not the process-global pool:
    // different Worlds may have equal ids/versions. Teardown guards World life.
    if (!h.isValid() || w.getEntityHandle(h.id) != h) return nullptr;
    if (!w.findEntity(h.id)) return nullptr;
    auto* storage = w.getStorage<Transform>();
    return storage ? static_cast<Transform*>(storage->get(h.id)) : nullptr;
}
bool conflict(World& w, EntityHandle h) {
    auto* sim = w.getStorage<ayt::entity::SimTransformComponent>();
    auto* det = w.getStorage<ayt::entity::DetSimTransformComponent>();
    auto* body = w.getStorage<ayt::entity::RigidBodyComponent>();
    return !w.findEntity(h.id) || (sim && sim->has(h.id)) || (det && det->has(h.id)) || (body && body->has(h.id));
}
}
struct SceneSequenceSession::Impl {
    struct Held { EntityHandle handle; Transform* identity; Transform baseline, expected; };
    World* world = nullptr;
    SequencePlayer player;
    std::map<std::string, EntityHandle> bindings;
    std::vector<Held> held;
    std::uint64_t generation = 1;
    std::uint64_t owner = 0, batch = 0, dispatched = 0;
    bool dispatching = false;
    std::unique_ptr<SceneAnimationDriver> animation;
    SequenceSubSystem* host = nullptr;
    Diagnostic error;
    std::vector<DebugEvent> debugEvents;
};
SceneSequenceSession::SceneSequenceSession() : _impl(std::make_unique<Impl>()) {
    auto& r = registry();
    _impl->owner = ++r.nextSession;
    if (r.sessions.empty() && !ayt::entity::registerWorldBeforeShutdownCallback(&r, &beforeWorldShutdown)) throw std::runtime_error("Cannot register sequence World lifecycle guard");
    r.sessions.push_back(this);
}
SceneSequenceSession::~SceneSequenceSession() {
    stop(); auto& r = registry(); std::erase(r.sessions,this);
    if (r.sessions.empty()) ayt::entity::unregisterWorldBeforeShutdownCallback(&r);
}
void SceneSequenceSession::beforeWorldShutdown(World& w) noexcept {
    for (auto* s : registry().sessions) if (s->_impl->world == &w) {
        s->stop(); s->_impl->world = nullptr; s->_impl->bindings.clear();
    }
}
bool SceneSequenceSession::bind(World& w, std::shared_ptr<const CompiledSequence> s,
                               const std::map<std::string, std::uint32_t>& ids, Diagnostic* e) {
    return bind(w,std::move(s),ids,std::unique_ptr<SceneAnimationDriver>{},e);
}
bool SceneSequenceSession::bind(World& w, std::shared_ptr<const CompiledSequence> s,
                               const std::map<std::string, std::uint32_t>& ids,
                               std::unique_ptr<SceneAnimationDriver> animation, Diagnostic* e) {
    auto fail = [&](std::string msg) { if (e) *e = {"binding",std::move(msg)}; return false; };
    if (!s || !w.isInitialized()) return fail("Expected initialized World and compiled sequence");
    if (!s->data().animations.empty() && !animation) return fail("Animation sections require AYSequence::AnimationIntegration");
    if (ids.size() != s->data().bindings.size()) return fail("Binding table must match all declared bindings");
    std::map<std::string, EntityHandle> next;
    for (const auto& b : s->data().bindings) {
        auto it = ids.find(b.id);
        if (it == ids.end() || !w.findEntity(it->second)) return fail("Missing Entity binding: " + b.id);
        auto h = w.getEntityHandle(it->second);
        if (!transform(w,h) || conflict(w,h)) return fail("Missing Transform or physics/SimTransform conflict: " + b.id);
        next.emplace(b.id,h);
    }
    std::vector<std::uint32_t> unique; for (const auto& [id,h] : next) unique.push_back(h.id);
    std::sort(unique.begin(),unique.end());
    if (std::adjacent_find(unique.begin(),unique.end()) != unique.end()) return fail("Bindings must target distinct Entities");
    if (animation && !animation->prepare(w,*s,ids,e)) return false;
    for (const auto& [id,h] : next) if (!transform(w,h) || conflict(w,h)) return fail("Binding changed during resource preparation: " + id);
    stop(); _impl->world = &w; _impl->bindings = std::move(next); _impl->player.bind(std::move(s));
    _impl->animation = std::move(animation);
    _impl->error = {}; if (e) *e = {}; return true;
}
bool SceneSequenceSession::acquire(Diagnostic* e) {
    auto& p = *_impl;
    if (!p.world || !p.player.sequence()) { if (e) *e = {"world","Unbound or revoked World"}; return false; }
    if (!p.held.empty()) return check(e);
    std::vector<Impl::Held> pending;
    if (!check(e)) return false;
    for (const auto& [binding,h] : p.bindings) {
        const auto& tracks = p.player.sequence()->data().transforms;
        if (std::none_of(tracks.begin(),tracks.end(),[&](const TransformTrack& t) { return t.binding == binding; })) continue;
        auto* t = transform(*p.world,h); auto key = std::make_pair(p.world,h.id);
        const auto claim = registry().claims.find(key);
        if (!t || conflict(*p.world,h) || (claim != registry().claims.end() && claim->second != this)) {
            if (e) *e = {binding,"Deleted Entity, conflicting controller or another sequence owns Transform"}; return false;
        }
        pending.push_back({h,t,*t,*t});
    }
    if (p.animation && !p.animation->acquire(e)) return false;
    for (const auto& held : pending) registry().claims.emplace(std::make_pair(p.world,held.handle.id),this);
    p.held = std::move(pending); return true;
}
bool SceneSequenceSession::check(Diagnostic* e) {
    auto& p = *_impl;
    if (!p.world) { if (e) *e = {"world","World was revoked"}; return false; }
    for (const auto& [id,h] : p.bindings) {
        if (!transform(*p.world,h) || conflict(*p.world,h)) {
            p.error = {id,"Required binding disappeared or gained conflicting control"};
            if (e) *e = p.error;
            stop(); return false;
        }
    }
    for (const auto& held : p.held) {
        auto* t = transform(*p.world,held.handle);
        auto claim = registry().claims.find({p.world,held.handle.id});
        if (!t || t != held.identity || !same(*t,held.expected) || conflict(*p.world,held.handle) || claim == registry().claims.end() || claim->second != this) {
            p.error = {"ownership","Entity/Transform disappeared or an external writer changed its state"};
            if (e) *e = p.error;
            stop(); return false;
        }
    }
    Diagnostic animationError;
    if (p.animation && !p.animation->check(&animationError)) { p.error=std::move(animationError); if (e) *e=p.error; stop(); return false; }
    return true;
}
bool SceneSequenceSession::apply(const Evaluation& evaluation, Diagnostic* e) {
    if (!check(e)) return false;
    auto& p = *_impl;
    if (p.animation && !p.animation->sample(evaluation,e)) { stop(); return false; }
    // Preflight every target before any write. No user callback occurs here.
    for (const auto& sample : evaluation.transforms) {
        const auto h = p.bindings.at(sample.binding);
        auto* t = transform(*p.world,h); const auto& v = sample.value;
        if (sample.channel == Channel::Position) t->setPosition(static_cast<float>(v[0]),static_cast<float>(v[1]),static_cast<float>(v[2]));
        else if (sample.channel == Channel::Rotation) t->setRotation(static_cast<float>(v[0]),static_cast<float>(v[1]),static_cast<float>(v[2]),static_cast<float>(v[3]));
        else t->setScale(static_cast<float>(v[0]),static_cast<float>(v[1]),static_cast<float>(v[2]));
        t->previousPosition = t->position; t->previousRotation = t->rotation; t->hasPreviousSimulationPose = false;
    }
    for (auto& held : p.held) held.expected = *transform(*p.world,held.handle);
    if (p.animation) p.animation->apply();
    return true;
}
void SceneSequenceSession::release() noexcept {
    auto& p = *_impl;
    if (p.animation) p.animation->release();
    for (const auto& held : p.held) {
        auto claim = registry().claims.find({p.world,held.handle.id});
        if (claim == registry().claims.end() || claim->second != this) continue;
        if (p.world) {
            auto* t = transform(*p.world,held.handle);
            if (t && t == held.identity && same(*t,held.expected) && !conflict(*p.world,held.handle)) {
                const auto revision = t->revision + 1;
                *t = held.baseline; t->revision = revision;
            }
        }
        registry().claims.erase(claim);
    }
    p.held.clear();
}
bool SceneSequenceSession::play(Diagnostic* e) {
    if (!acquire(e)) return false;
    const bool newRun = _impl->player.state() == PlaybackState::Stopped || _impl->player.state() == PlaybackState::Completed;
    if (!_impl->player.play()) return false;
    if (newRun) ++_impl->generation;
    Evaluation eval;
    if (!_impl->player.sequence()->evaluate(_impl->player.position(),eval,e) || !apply(eval,e)) { stop(); return false; }
    return true;
}
void SceneSequenceSession::pause() noexcept { _impl->player.pause(); }
void SceneSequenceSession::stop() noexcept { release(); _impl->player.stop(); ++_impl->generation; }
bool SceneSequenceSession::seek(double seconds, SceneFrame& out, Diagnostic* e) {
    Evaluation preflight;
    if (!_impl->player.sequence() || !_impl->player.sequence()->evaluate(seconds,preflight,e)) return false;
    if (!acquire(e)) return false;
    SceneFrame next;
    if (!_impl->player.seek(seconds,next.playback,e) || !apply(next.playback.evaluation,e)) return false;
    ++_impl->generation; // Invalidate any queued batch from before this seek.
    stamp(next); out = std::move(next); return true;
}
bool SceneSequenceSession::step(double dt, SceneFrame& out, Diagnostic* e) {
    if (!check(e)) return false;
    SceneFrame next;
    if (!_impl->player.advance(dt,next.playback,e)) return false;
    if (_impl->player.state() == PlaybackState::Stopped && _impl->held.empty()) { if (e) *e = {"playback","Play or seek before advancing scene session"}; return false; }
    if (_impl->player.state() != PlaybackState::Playing && !next.playback.completed) { stamp(next); out = std::move(next); return true; }
    if (!apply(next.playback.evaluation,e)) return false;
    if (next.playback.completed) release();
    stamp(next); out = std::move(next); return true;
}
void SceneSequenceSession::stamp(SceneFrame& out) {
    out.generation = _impl->generation; out.owner = _impl->owner; out.batch = ++_impl->batch;
    if(registry().debugCapture)try{for(const auto& event:out.playback.events){if(_impl->debugEvents.size()==128)_impl->debugEvents.erase(_impl->debugEvents.begin());_impl->debugEvents.push_back({event,out.batch,out.generation,false});}}catch(...){}
}
bool dispatchSequenceEvents(std::shared_ptr<SceneSequenceSession> session, SceneFrame frame,
                            SequenceEventSink sink, std::size_t& delivered, Diagnostic* e) {
    std::size_t audioDelivered = 0;
    if (!sink) { delivered = 0; if (e) *e = {"events", "Expected event sink"}; return false; }
    return dispatchSequenceCues(std::move(session), std::move(frame),
        std::move(sink), {}, delivered, audioDelivered, e);
}
bool dispatchSequenceCues(std::shared_ptr<SceneSequenceSession> session,
                          SceneFrame frame, SequenceEventSink events,
                          SequenceAudioSink audio, std::size_t& deliveredEvents,
                          std::size_t& deliveredAudio, Diagnostic* e) {
    deliveredEvents = 0;
    deliveredAudio = 0;
    auto fail = [&](const char* message) { if (e) *e = {"cues",message}; return false; };
    if (!session || (!events && !frame.playback.events.empty())
        || (!audio && !frame.playback.audioCues.empty()))
        return fail("Expected live session and sinks for contained cues");
    auto& p = *session->_impl;
    if (p.dispatching || frame.owner != p.owner || !frame.batch || frame.batch > p.batch || frame.batch <= p.dispatched
        || !session->isCurrent(frame.generation)) return fail("Stale, foreign, replayed or nested event batch");
    p.dispatched = frame.batch; p.dispatching = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{p.dispatching};
    const auto beforeCue = [&]() {
        if (p.host) {
            // Hold the provider callable across potential host changes.
            World* current=nullptr;
            try { auto provider=p.host->_provider; current=provider(); }
            catch (...) { session->stop(); return fail("World provider threw; cues canceled"); }
            if (p.world!=current) {
                session->stop(); p.world=nullptr; p.bindings.clear();
                return fail("Active World changed; remaining cues canceled and explicit rebind required");
            }
        }
        if (!session->isCurrent(frame.generation) || !session->check(e))
            return fail("Playback or binding changed; remaining cues canceled");
        return true;
    };
    std::size_t eventIndex = 0, audioIndex = 0;
    while (eventIndex < frame.playback.events.size()
           || audioIndex < frame.playback.audioCues.size()) {
        if (!beforeCue()) return false;
        const bool useEvent = eventIndex < frame.playback.events.size()
            && (audioIndex == frame.playback.audioCues.size()
                || frame.playback.events[eventIndex].time
                    <= frame.playback.audioCues[audioIndex].time);
        try {
            if (useEvent) {
                const auto& event = frame.playback.events[eventIndex++];
                ++deliveredEvents;
                events(event);
                for(auto& observed:p.debugEvents)if(observed.batch==frame.batch && observed.generation==frame.generation && observed.event.id==event.id)observed.delivered=true;
            } else {
                const auto& cue = frame.playback.audioCues[audioIndex++];
                ++deliveredAudio;
                audio(cue);
            }
        }
        catch (...) { session->stop(); return fail("Cue callback threw; playback stopped"); }
    }
    if (e) *e = {};
    return true;
}
bool SceneSequenceSession::advance(double dt, SceneFrame& out, Diagnostic* e) {
    if (_impl->host) { if (e) *e = {"tick","Hosted session cannot be manually advanced"}; return false; }
    return step(dt,out,e);
}
void SceneSequenceSession::setDebugCaptureEnabled(bool enabled) noexcept {registry().debugCapture=enabled;if(!enabled)clearDebugHistory();}
void SceneSequenceSession::clearDebugHistory() noexcept {for(auto* session:registry().sessions)session->_impl->debugEvents.clear();}
std::vector<SceneSequenceSession::DebugSnapshot> SceneSequenceSession::debugSnapshots(const World& world) {
    std::vector<DebugSnapshot> out;
    for(const auto* session:registry().sessions){const auto& p=*session->_impl;if(p.world!=&world || !p.player.sequence() || out.size()==64)continue;
        const auto& data=p.player.sequence()->data();DebugSnapshot row;row.session=p.owner;row.sequence=data.id;row.position=p.player.position();row.duration=data.duration;row.state=p.player.state();row.error=p.error.message;row.events=p.debugEvents;
        for(const auto& [name,handle]:p.bindings){if(row.bindings.size()==256)break;DebugBinding binding;binding.name=name;binding.entityId=handle.id;
            binding.ownsTransform=std::any_of(p.held.begin(),p.held.end(),[&](const auto& held){return held.handle==handle;});
            for(const auto& track:data.animations)if(binding.ownsTransform && track.binding==name)for(const auto& section:track.sections)if(row.position>=section.start && (row.position<section.end || (section.end==data.duration && row.position==data.duration)))binding.activeSections.push_back(section.id+" | "+section.source);
            row.bindings.push_back(std::move(binding));}
        out.push_back(std::move(row));}
    return out;
}
double SceneSequenceSession::position() const noexcept { return _impl->player.position(); }
PlaybackState SceneSequenceSession::state() const noexcept { return _impl->player.state(); }
bool SceneSequenceSession::isCurrent(std::uint64_t g) const noexcept { return _impl->world && _impl->generation == g; }
const Diagnostic& SceneSequenceSession::lastError() const noexcept { return _impl->error; }

bool SequenceSubSystem::addSession(std::shared_ptr<SceneSequenceSession> session, Diagnostic* e) {
    if (!session || session->_impl->host || !session->_impl->world || _sessions.size() >= 64) {
        if (e) *e = {"host","Expected bound unhosted session; maximum 64 sessions"}; return false;
    }
    _sessions.push_back(session); session->_impl->host = this; return true;
}
void SequenceSubSystem::removeSession(const std::shared_ptr<SceneSequenceSession>& session) {
    if (!session || session->_impl->host != this) return;
    session->stop(); session->_impl->host = nullptr; std::erase(_sessions,session);
    std::erase_if(_frames,[&](const HostedSceneFrame& f) { return f.session == session; });
}
void SequenceSubSystem::shutdown() {
    for (auto& session : _sessions) { session->stop(); session->_impl->host = nullptr; }
    _sessions.clear(); _frames.clear(); _initialized = false;
}
void SequenceSubSystem::tick(ayt::game::FramePhase phase, const ayt::game::FrameContext& context) {
    if (!_initialized || phase != ayt::game::FramePhase::Gameplay || _lastFrame == context.frameCount) return;
    _lastFrame = context.frameCount; _frames.clear();
    auto* current = _provider();
    for (auto& session : _sessions) {
        if (session->_impl->world != current) {
            session->stop(); session->_impl->world = nullptr; session->_impl->error = {"world","Active World changed; explicit rebind required"}; continue;
        }
        if (session->state() == PlaybackState::Stopped || session->state() == PlaybackState::Completed) continue;
        SceneFrame out; Diagnostic error;
        if (session->step(context.deltaTime,out,&error)) _frames.push_back({session,std::move(out)});
        else session->_impl->error = std::move(error);
    }
}
} // namespace ayt::sequence
