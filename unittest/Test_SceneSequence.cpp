#include <AYSequence/SequenceRuntimeModule.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <AYGameLoop.h>
#include <AYGameLoop/SubSystemRegistry.h>
#include <AYModule/ModuleManager.h>
#include <AYModule/ModuleContext.h>
#include <AYScene.h>
#include <AYTest.h>
#include <limits>

using namespace ayt::sequence;
using namespace ayt::entity;
namespace {
Sequence model() {
    Sequence s; s.id="two-objects"; s.duration=4; s.bindings={{"left"},{"right"}};
    s.transforms={{"move","left",Channel::Position,{{"p0",0,{0,0,0,0}},{"p1",4,{8,0,0,0}}}},
        {"scale","right",Channel::Scale,{{"s0",0,{1,1,1,0}},{"s1",4,{2,3,4,0}}}},
        {"rotate","right",Channel::Rotation,{{"q0",0,{0,0,0,1}},{"q1",4,{0,1,0,0}}}}};
    s.eventTracks={{"events",{{"start",0,"start",""},{"end",4,"end",""}}}};
    return s;
}
struct Fixture {
    ayt::scene::Scene scene{ayt::scene::SceneMode::Edit,"sequence-test"};
    Entity* left = nullptr; Entity* right = nullptr;
    Fixture() {
        (void)registerEntityCoreComponents(ComponentRegistry::instance());
        (void)ComponentRegistry::instance().registerType<RigidBodyComponent>("RigidBodyComponent");
        left=scene.world().createEntity(); right=scene.world().createEntity();
        left->addComponent<Transform>()->setPosition(10,20,30);
        right->addComponent<Transform>()->setScale(5);
    }
    std::map<std::string,std::uint32_t> ids() { return {{"left",left->getId()},{"right",right->getId()}}; }
    std::shared_ptr<SceneSequenceSession> session() {
        auto s=std::make_shared<SceneSequenceSession>();
        if (!s->bind(scene.world(),CompiledSequence::compile(model()),ids())) throw std::runtime_error("fixture binding failed");
        return s;
    }
};
class ModuleService final : public ayt::game::ISubSystemModuleService {
public:
    std::unique_ptr<ayt::game::ISubSystem> system;
    ayt::game::ISubSystem* findSubSystem(std::string_view name) noexcept override { return system && name==system->getName() ? system.get() : nullptr; }
    bool installSubSystem(std::unique_ptr<ayt::game::ISubSystem> s) override { if(system) return false; system=std::move(s); return true; }
    void uninstallSubSystem(std::string_view name,ayt::game::ISubSystem* expected) noexcept override { if(system.get()==expected && name==system->getName()) system.reset(); }
};
float observedPosition = -1;
class ObserveSystem final : public ISystem {
public:
    const char* getName() const override { return "SequenceObserve"; }
    void onUpdate(float) override { auto* e=World::instance().findEntity(1u); observedPosition=e->getComponent<Transform>()->position.x; }
};
}
TEST_SUITE(SceneSequenceTests)
TEST_CASE(two_objects_scrub_and_stop_restore_original_state) {
    Fixture f; auto s=f.session(); SceneFrame out;
    CHECK_TRUE(s->seek(2,out)); CHECK(f.left->getComponent<Transform>()->position.x==4); CHECK(f.right->getComponent<Transform>()->scale.y==2); CHECK(std::abs(f.right->getComponent<Transform>()->rotation.y-std::sqrt(0.5f))<1e-5f); CHECK(out.playback.events.empty());
    CHECK_TRUE(s->seek(1,out)); s->stop(); CHECK(f.left->getComponent<Transform>()->position.x==10); CHECK(f.right->getComponent<Transform>()->scale.x==5); CHECK_FALSE(s->isCurrent(out.generation));
}
TEST_CASE(once_completion_restores_baseline_and_returns_final_output) {
    Fixture f; auto s=f.session(); CHECK_TRUE(s->play()); SceneFrame out;
    CHECK_TRUE(s->advance(4,out)); CHECK_TRUE(out.playback.completed); CHECK(out.playback.events.size()==2); CHECK(out.playback.evaluation.transforms[0].value[0]==8);
    CHECK(f.left->getComponent<Transform>()->position.x==10); CHECK(s->state()==PlaybackState::Completed); CHECK_TRUE(s->isCurrent(out.generation));
}
TEST_CASE(pause_resume_does_not_recapture_baseline) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame out; s->advance(1,out); s->pause(); const auto revision=f.left->getComponent<Transform>()->revision; s->advance(1,out); CHECK(s->position()==1); CHECK(f.left->getComponent<Transform>()->revision==revision);
    s->play(); s->advance(1,out); CHECK(s->position()==2); s->stop(); CHECK(f.left->getComponent<Transform>()->position.x==10);
}
TEST_CASE(restarting_after_completion_revokes_previous_event_batch) {
    Fixture f; auto s=f.session(); CHECK_TRUE(s->play()); SceneFrame completed, next;
    CHECK_TRUE(s->advance(4,completed)); CHECK_TRUE(s->isCurrent(completed.generation));
    CHECK_TRUE(s->play()); CHECK_FALSE(s->isCurrent(completed.generation));
    CHECK_TRUE(s->advance(1,next)); CHECK_TRUE(s->isCurrent(next.generation));
    s->pause(); CHECK_TRUE(s->play()); CHECK_TRUE(s->isCurrent(next.generation));
}
TEST_CASE(competing_sequences_acquire_atomically_and_release) {
    Fixture f; auto a=f.session(); auto b=f.session(); CHECK_TRUE(a->play()); Diagnostic error; CHECK_FALSE(b->play(&error)); CHECK_FALSE(error.message.empty());
    a->stop(); CHECK_TRUE(b->play()); b->stop(); CHECK(f.right->getComponent<Transform>()->scale.x==5);
}
TEST_CASE(external_mutator_and_direct_writes_are_not_overwritten_on_stop) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame out; s->advance(1,out);
    f.left->getComponent<Transform>()->setPosition(99,0,0); CHECK_FALSE(s->advance(1,out)); CHECK(f.left->getComponent<Transform>()->position.x==99); CHECK(f.right->getComponent<Transform>()->scale.x==5); CHECK(s->state()==PlaybackState::Stopped);
    CHECK_TRUE(s->play()); f.left->getComponent<Transform>()->position.x=123; s->stop(); CHECK(f.left->getComponent<Transform>()->position.x==123);
}
TEST_CASE(deleted_entity_and_removed_component_fail_without_partial_writes) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame out; s->advance(1,out); f.scene.world().destroyEntity(f.left);
    CHECK_FALSE(s->advance(1,out)); CHECK(f.right->getComponent<Transform>()->scale.x==5);
    Fixture g; auto t=g.session(); t->play(); g.right->removeComponent<Transform>(); CHECK_FALSE(t->advance(1,out)); CHECK(g.left->getComponent<Transform>()->position.x==10);
}
TEST_CASE(world_destruction_and_reinitialize_revoke_old_generation) {
    auto s=std::make_shared<SceneSequenceSession>(); SceneFrame out;
    { Fixture f; CHECK_TRUE(s->bind(f.scene.world(),CompiledSequence::compile(model()),f.ids())); s->play(); CHECK_TRUE(s->advance(1,out)); }
    CHECK_FALSE(s->isCurrent(out.generation)); CHECK_FALSE(s->advance(1,out)); CHECK_FALSE(s->play()); s->stop();
    Fixture g; auto t=g.session(); t->play(); g.scene.world().shutdown(); g.scene.world().initialize(); CHECK_FALSE(t->play());
}
TEST_CASE(missing_alias_animation_and_controller_bindings_are_rejected) {
    Fixture f; SceneSequenceSession s; Diagnostic e; auto ids=f.ids(); ids["left"]=9999; CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(model()),ids,&e));
    ids=f.ids(); ids["right"]=ids["left"]; CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(model()),ids));
    f.left->addComponent<SimTransformComponent>(); CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(model()),f.ids())); f.left->removeComponent<SimTransformComponent>();
    f.left->addComponent<DetSimTransformComponent>(); CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(model()),f.ids())); f.left->removeComponent<DetSimTransformComponent>();
    f.right->addComponent<RigidBodyComponent>(); CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(model()),f.ids())); f.right->removeComponent<RigidBodyComponent>();
    auto data=model(); data.animations={{"anim","left",{{"clip","clip.ayanm",0,4,0,1,4}}}}; CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(data),f.ids()));
}
TEST_CASE(failed_rebind_and_invalid_seek_preserve_active_session) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame out; s->advance(1,out); auto ids=f.ids(); ids.erase("right");
    CHECK_FALSE(s->bind(f.scene.world(),CompiledSequence::compile(model()),ids)); CHECK(s->state()==PlaybackState::Playing); CHECK(s->position()==1);
    CHECK_FALSE(s->seek(std::numeric_limits<double>::quiet_NaN(),out)); CHECK(s->position()==1); CHECK(f.left->getComponent<Transform>()->position.x==2);
}
TEST_CASE(destructor_restores_while_world_is_still_live) {
    Fixture f; { auto s=f.session(); s->play(); SceneFrame out; s->advance(1,out); }
    CHECK(f.left->getComponent<Transform>()->position.x==10); CHECK(f.right->getComponent<Transform>()->scale.x==5);
}
TEST_CASE(host_tick_owns_once_per_frame_and_manual_tick_is_rejected) {
    Fixture f; auto s=f.session(); SequenceSubSystem host([&]{return &f.scene.world();}); CHECK_TRUE(host.initialize()); CHECK_TRUE(host.addSession(s)); CHECK_FALSE(host.addSession(s)); s->play();
    ayt::game::FrameContext ctx; ctx.frameCount=1; ctx.deltaTime=1; host.tick(ayt::game::FramePhase::World,ctx); CHECK(s->position()==0);
    host.tick(ayt::game::FramePhase::Gameplay,ctx); host.tick(ayt::game::FramePhase::Gameplay,ctx); CHECK(s->position()==1); SceneFrame out; CHECK_FALSE(s->advance(1,out));
    auto frames=host.takeFrames(); CHECK(frames.size()==1); CHECK(frames[0].frame.playback.events.size()==1); CHECK(host.takeFrames().empty());
    host.removeSession(s); CHECK(f.left->getComponent<Transform>()->position.x==10); CHECK_TRUE(s->play()); CHECK_TRUE(s->advance(1,out));
}
TEST_CASE(host_world_switch_revokes_bindings_without_touching_new_world) {
    Fixture f; Fixture g; auto s=f.session(); World* active=&f.scene.world(); SequenceSubSystem host([&]{return active;}); host.initialize(); host.addSession(s); s->play();
    active=&g.scene.world(); ayt::game::FrameContext ctx; ctx.frameCount=1; ctx.deltaTime=1; host.tick(ayt::game::FramePhase::Gameplay,ctx);
    CHECK(s->state()==PlaybackState::Stopped); CHECK(f.left->getComponent<Transform>()->position.x==10); CHECK(g.left->getComponent<Transform>()->position.x==10); CHECK_FALSE(s->play());
}
TEST_CASE(runtime_module_install_shutdown_and_dependency_contract) {
    Fixture f; ModuleService service; ayt::module::ModuleContext context;
    CHECK_TRUE(context.bindService(std::string(ayt::game::kSubSystemModuleService),&service));
    SequenceRuntimeModule module([&]{return &f.scene.world();}); CHECK(module.descriptor().id=="AYSequence.Runtime"); CHECK(module.descriptor().dependencies.size()==1);
    CHECK_TRUE(module.install(context).succeeded()); auto* host=dynamic_cast<SequenceSubSystem*>(module.installedSubSystem()); CHECK_TRUE(host!=nullptr); CHECK_TRUE(host->initialize());
    CHECK(host->getDescriptor().phases==ayt::game::phaseBit(ayt::game::FramePhase::Gameplay)); CHECK(host->getDescriptor().phasePriority>0);
    auto s=f.session(); host->addSession(s); s->play(); SequenceRuntimeModule duplicate; CHECK_FALSE(duplicate.install(context).succeeded()); module.shutdown(context); CHECK(service.system==nullptr); CHECK(f.left->getComponent<Transform>()->position.x==10);
}
TEST_CASE(real_hosted_loop_orders_sequence_before_world_and_ticks_once) {
    Fixture f; auto s=f.session(); auto& loop=ayt::game::GameLoop::instance(); auto& reg=ayt::game::SubSystemRegistry::instance();
    loop.endHostedSession(); reg.clearAll(); loop.setPresentationOwnership(ayt::game::PresentationOwnership::ExternalHost); loop.setTimeScale(1);
    World::setActiveWorld(&f.scene.world()); f.scene.world().registerSystem<ObserveSystem>();
    auto* host=new SequenceSubSystem([&]{return &f.scene.world();}); CHECK_TRUE(host->addSession(s));
    loop.registerSubSystem(host); loop.registerSubSystem(createEntitySubSystem().release());
    const bool ready=loop.prepareHostedSession(); CHECK_TRUE(ready);
    if(ready) { CHECK_TRUE(s->play()); observedPosition=-1; ayt::game::HostedFrameContext ctx; ctx.realWallDeltaTime=0.1f; ctx.hostFrameIndex=1; loop.tickHostedFrame(ctx);
        CHECK(s->position()>0); CHECK(std::abs(observedPosition-static_cast<float>(s->position()*2))<1e-5f); CHECK(host->takeFrames().size()==1); }
    loop.endHostedSession(); reg.clearAll(); World::setActiveWorld(nullptr); CHECK(f.left->getComponent<Transform>()->position.x==10);
}
TEST_CASE(module_graph_rejects_missing_entity_runtime_dependency) {
    ayt::module::ModuleManager modules; CHECK_TRUE(modules.emplace<SequenceRuntimeModule>().succeeded()); CHECK_FALSE(modules.resolve().succeeded());
}
TEST_CASE(successful_seek_invalidates_older_event_batches) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame first,second; s->advance(1,first); CHECK_TRUE(s->isCurrent(first.generation)); s->seek(2,second);
    CHECK_FALSE(s->isCurrent(first.generation)); CHECK_TRUE(s->isCurrent(second.generation)); CHECK(second.playback.events.empty());
}
TEST_CASE(scene_clear_and_new_physics_authority_invalidate_owned_targets) {
    Fixture f; auto s=f.session(); s->play(); f.scene.clear(); SceneFrame out; CHECK_FALSE(s->advance(1,out)); CHECK(s->state()==PlaybackState::Stopped);
    Fixture g; auto t=g.session(); t->play(); g.left->addComponent<SimTransformComponent>(); CHECK_FALSE(t->advance(1,out)); CHECK(g.left->getComponent<Transform>()->position.x==0); CHECK(g.right->getComponent<Transform>()->scale.x==5);
}
TEST_CASE(duplicate_claim_failure_never_partially_acquires_other_targets) {
    Fixture f; auto single=model(); single.bindings.resize(1); single.transforms.resize(1); single.id="single";
    auto a=std::make_shared<SceneSequenceSession>(); CHECK_TRUE(a->bind(f.scene.world(),CompiledSequence::compile(single),{{"left",f.left->getId()}})); a->play(); auto b=f.session(); CHECK_FALSE(b->play());
    single.id="other"; single.bindings={{"right"}}; single.transforms[0].binding="right"; single.transforms[0].id="other-track";
    SceneSequenceSession c; CHECK_TRUE(c.bind(f.scene.world(),CompiledSequence::compile(single),{{"right",f.right->getId()}})); CHECK_TRUE(c.play()); c.stop(); CHECK(f.right->getComponent<Transform>()->scale.x==5);
}
TEST_CASE(new_binary32_authority_revokes_sequence_without_overwriting_it) {
    Fixture f; auto s=f.session(); CHECK_TRUE(s->play());
    auto* det=f.left->addComponent<DetSimTransformComponent>();
    CHECK_NOT_NULL(det); CHECK_TRUE(det->setPosition(ayt::math::DetVec3::fromInts(2,3,4)));
    SceneFrame out; CHECK_FALSE(s->advance(1,out));
    CHECK(s->state()==PlaybackState::Stopped);
    CHECK(f.left->getComponent<Transform>()->position.x==0);
    CHECK(det->position == ayt::math::DetVec3::fromInts(2,3,4));
    CHECK(f.right->getComponent<Transform>()->scale.x==5);
}
TEST_CASE(event_dispatch_is_owned_once_and_rejects_foreign_batches) {
    Fixture f; auto a=f.session(); auto b=f.session(); a->play(); SceneFrame frame; a->advance(4,frame);
    std::size_t delivered=0; std::vector<std::string> names;
    CHECK_FALSE(dispatchSequenceEvents(b,frame,[&](const Event& e){names.push_back(e.name);},delivered)); CHECK(delivered==0);
    CHECK_TRUE(dispatchSequenceEvents(a,frame,[&](const Event& e){names.push_back(e.name);},delivered)); CHECK(delivered==2); CHECK(names.size()==2);
    CHECK_FALSE(dispatchSequenceEvents(a,frame,[](const Event&){},delivered)); CHECK(delivered==0);
}
TEST_CASE(event_callback_stop_seek_and_rebind_cancel_remaining_batch) {
    for (int action=0;action<3;++action) {
        Fixture f; auto s=f.session(); s->play(); SceneFrame frame; s->advance(4,frame); std::size_t delivered=0;
        CHECK_FALSE(dispatchSequenceEvents(s,frame,[&](const Event&){
            if (action==0) s->stop();
            else if (action==1) { SceneFrame seek; s->seek(1,seek); }
            else s->bind(f.scene.world(),CompiledSequence::compile(model()),f.ids());
        },delivered)); CHECK(delivered==1);
    }
}
TEST_CASE(event_callback_deletion_and_world_shutdown_cancel_remaining_batch) {
    for (bool shutdown : {false,true}) {
        Fixture f; auto s=f.session(); s->play(); SceneFrame frame; s->advance(4,frame); std::size_t delivered=0;
        CHECK_FALSE(dispatchSequenceEvents(s,frame,[&](const Event&){
            if (shutdown) f.scene.world().shutdown(); else f.scene.world().destroyEntity(f.left);
        },delivered)); CHECK(delivered==1);
    }
}
TEST_CASE(event_callback_exception_stops_and_restores) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame frame; s->advance(1,frame); std::size_t delivered=0; Diagnostic error;
    CHECK_FALSE(dispatchSequenceEvents(s,frame,[](const Event&){throw std::runtime_error("callback");},delivered,&error));
    CHECK(delivered==1); CHECK_FALSE(error.message.empty()); CHECK(s->state()==PlaybackState::Stopped); CHECK(f.left->getComponent<Transform>()->position.x==10);
}
TEST_CASE(event_dispatch_holds_session_and_sink_and_rejects_nested_replay) {
    Fixture f; auto s=f.session(); s->play(); SceneFrame frame; s->advance(4,frame); std::weak_ptr<SceneSequenceSession> weak=s;
    SequenceEventSink sink; std::size_t delivered=0, nested=0;
    sink=[&](const Event&){ auto retained=weak.lock(); CHECK_TRUE(bool(retained)); CHECK_FALSE(dispatchSequenceEvents(retained,frame,[](const Event&){},nested)); s.reset(); sink={}; };
    CHECK_TRUE(dispatchSequenceEvents(s,frame,sink,delivered)); CHECK(delivered==2); CHECK(nested==0); CHECK_FALSE(bool(s)); CHECK_TRUE(weak.expired());
}
TEST_CASE(host_removed_during_event_dispatch_releases_without_invalidating_iteration) {
    Fixture f; auto s=f.session(); SequenceSubSystem host([&]{return &f.scene.world();}); host.initialize(); host.addSession(s); s->play();
    ayt::game::FrameContext context; context.frameCount=1; context.deltaTime=4; host.tick(ayt::game::FramePhase::Gameplay,context);
    auto frames=host.takeFrames(); std::size_t delivered=0;
    CHECK_FALSE(dispatchSequenceEvents(frames[0].session,frames[0].frame,[&](const Event&){host.removeSession(s);},delivered)); CHECK(delivered==1);
}
TEST_CASE(active_world_switch_in_callback_cancels_without_waiting_for_next_host_tick) {
    Fixture f; Fixture g; auto s=f.session(); World* active=&f.scene.world(); SequenceSubSystem host([&]{return active;}); host.initialize(); host.addSession(s); s->play();
    ayt::game::FrameContext context; context.frameCount=1; context.deltaTime=4; host.tick(ayt::game::FramePhase::Gameplay,context);
    auto frames=host.takeFrames(); std::size_t delivered=0;
    CHECK_FALSE(dispatchSequenceEvents(s,frames[0].frame,[&](const Event&){active=&g.scene.world();},delivered)); CHECK(delivered==1);
    CHECK_FALSE(s->isCurrent(frames[0].frame.generation)); CHECK(g.left->getComponent<Transform>()->position.x==10); CHECK_FALSE(s->play());
}
TEST_CASE(debug_event_snapshot_does_not_deliver_or_replay_gameplay_events) {
    struct Reset {~Reset(){SceneSequenceSession::setDebugCaptureEnabled(false);}} reset;
    SceneSequenceSession::setDebugCaptureEnabled(true);SceneSequenceSession::clearDebugHistory();
    Fixture f;auto session=f.session();CHECK_TRUE(session->play());SceneFrame frame;CHECK_TRUE(session->advance(1,frame));
    const auto pose=f.left->getComponent<Transform>()->position;auto snapshot=SceneSequenceSession::debugSnapshots(f.scene.world());
    CHECK(snapshot.size()==1);CHECK(frame.playback.events.size()==1);
    if(!snapshot.empty()){CHECK(snapshot[0].position==1);CHECK(snapshot[0].events.size()==1);if(!snapshot[0].events.empty())CHECK_FALSE(snapshot[0].events[0].delivered);}
    (void)SceneSequenceSession::debugSnapshots(f.scene.world());CHECK(f.left->getComponent<Transform>()->position.x==pose.x);
    size_t delivered=0;int callbacks=0;CHECK_TRUE(dispatchSequenceEvents(session,frame,[&](const Event&){++callbacks;},delivered));CHECK(delivered==1);CHECK(callbacks==1);
    auto after=SceneSequenceSession::debugSnapshots(f.scene.world());if(!after.empty() && !after[0].events.empty())CHECK_TRUE(after[0].events[0].delivered);
    if(!snapshot.empty() && !snapshot[0].events.empty())CHECK_FALSE(snapshot[0].events[0].delivered);
    CHECK_FALSE(dispatchSequenceEvents(session,frame,[&](const Event&){++callbacks;},delivered));CHECK(callbacks==1);
}
TEST_SUITE_END
