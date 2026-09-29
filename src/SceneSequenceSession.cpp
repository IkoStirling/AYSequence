#include <AYSequence/SceneSequenceSession.h>
#include <AYSequence/SequenceRuntimeModule.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/WorldLifecycle.h>
#include <AYEntity/components/TransformComponent.h>
#include <AYEntity/components/SimTransformComponent.h>
#include <AYEntity/components/RigidBodyComponent.h>
#include <algorithm>
#include <stdexcept>

namespace ayt::sequence {
namespace {
using ayt::entity::World; using ayt::entity::EntityHandle; using ayt::entity::Transform;
struct Registry {
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
    auto* body = w.getStorage<ayt::entity::RigidBodyComponent>();
    return !w.findEntity(h.id) || (sim && sim->has(h.id)) || (body && body->has(h.id));
}
}
struct SceneSequenceSession::Impl {
    struct Held { EntityHandle handle; Transform* identity; Transform baseline, expected; };
    World* world = nullptr;
    SequencePlayer player;
    std::map<std::string, EntityHandle> bindings;
    std::vector<Held> held;
    std::uint64_t generation = 1;
    SequenceSubSystem* host = nullptr;
    Diagnostic error;
};
SceneSequenceSession::SceneSequenceSession() : _impl(std::make_unique<Impl>()) {
    auto& r = registry();
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
    auto fail = [&](std::string msg) { if (e) *e = {"binding",std::move(msg)}; return false; };
    if (!s || !w.isInitialized()) return fail("Expected initialized World and compiled sequence");
    if (!s->data().animations.empty()) return fail("S2 does not apply animation sections; use Core mapping until S3");
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
    stop(); _impl->world = &w; _impl->bindings = std::move(next); _impl->player.bind(std::move(s));
    _impl->error = {}; if (e) *e = {}; return true;
}
bool SceneSequenceSession::acquire(Diagnostic* e) {
    auto& p = *_impl;
    if (!p.world || !p.player.sequence()) { if (e) *e = {"world","Unbound or revoked World"}; return false; }
    if (!p.held.empty()) return check(e);
    std::vector<Impl::Held> pending;
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
    return true;
}
bool SceneSequenceSession::apply(const Evaluation& evaluation, Diagnostic* e) {
    if (!check(e)) return false;
    auto& p = *_impl;
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
    return true;
}
void SceneSequenceSession::release() noexcept {
    auto& p = *_impl;
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
    next.generation = _impl->generation; out = std::move(next); return true;
}
bool SceneSequenceSession::step(double dt, SceneFrame& out, Diagnostic* e) {
    if (!check(e)) return false;
    SceneFrame next;
    if (!_impl->player.advance(dt,next.playback,e)) return false;
    if (_impl->player.state() == PlaybackState::Stopped && _impl->held.empty()) { if (e) *e = {"playback","Play or seek before advancing scene session"}; return false; }
    if (_impl->player.state() != PlaybackState::Playing && !next.playback.completed) { next.generation = _impl->generation; out = std::move(next); return true; }
    if (!apply(next.playback.evaluation,e)) return false;
    if (next.playback.completed) release();
    next.generation = _impl->generation; out = std::move(next); return true;
}
bool SceneSequenceSession::advance(double dt, SceneFrame& out, Diagnostic* e) {
    if (_impl->host) { if (e) *e = {"tick","Hosted session cannot be manually advanced"}; return false; }
    return step(dt,out,e);
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
