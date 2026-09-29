#include <AYSequence/AnimationSequenceDriver.h>
#include <AYSequence/SequenceRuntimeModule.h>
#include <AYEntity.h>
#include <AYEntity/EntityModule.h>
#include <AYEntity/components/SkeletonComponent.h>
#include <AYEntity/components/AnimationComponent.h>
#include <AYEntity/components/BlendSpaceComponent.h>
#include <AYEntity/components/AnimationStateMachineComponent.h>
#include <AYEntity/AnimationSystem.h>
#include <AYEntity/BlendSpaceSystem.h>
#include <AYEntity/StateMachineSystem.h>
#include <AYResource.h>
#include <AYResource/assetsImpl/Animation.h>
#include <AYIO/File.h>
#include <AYScene.h>
#include <AYTest.h>
#include <filesystem>
#include <limits>
#include <chrono>

using namespace ayt::sequence;
using namespace ayt::entity;
using namespace ayt::resource;
namespace {
Sequence animatedModel() {
    Sequence s; s.id="animated-sequence"; s.duration=4; s.bindings={{"actor"}};
    s.transforms={{"move","actor",Channel::Position,{{"k0",0,{0,0,0,0}},{"k1",4,{8,0,0,0}}}}};
    s.animations={{"animation","actor",{{"section-a","a.ayanm",0,2,1,1,4},{"section-b","b.ayanm",2,4,0,2,4}}}};
    s.eventTracks={{"events",{{"event-start",0,"start",""},{"event-middle",2,"middle",""},{"event-end",4,"end",""}}}};
    return s;
}
std::shared_ptr<Animation> clip(float start, float end, const std::string& bone="root") {
    auto a=std::make_shared<Animation>(); a->setDuration(4); a->setTicksPerSecond(2);
    AnimTrack t; t.nodeName=bone; t.property="position"; t.valueType=AnimTrackType::Vector3; t.times={0,8}; t.values={start,0,0,end,0,0}; a->addTrack(t);
    a->addNotify({"ignored",1,42}); return a;
}
void registerTypes() {
    auto& registry=ComponentRegistry::instance(); (void)registerEntityCoreComponents(registry);
    (void)registry.registerType<SkeletonComponent>("SkeletonComponent");
    (void)registry.registerType<AnimationComponent>("AnimationComponent");
    (void)registry.registerType<BlendSpaceComponent>("BlendSpaceComponent");
    (void)registry.registerType<AnimationStateMachineComponent>("AnimationStateMachineComponent");
}
SkeletonComponent* prepareActor(Entity* actor, std::shared_ptr<Animation> idle) {
    actor->addComponent<ayt::entity::Transform>()->setPosition(10,0,0);
    auto* c=actor->addComponent<SkeletonComponent>(); c->skeleton=std::make_shared<Skeleton>(); c->skeleton->setBoneCount(1);
    Bone root; root.name="root"; root.parentIndex=-1; root.localPosition={0,0,0}; root.localRotation={0,0,0,1}; root.localScale={1,1,1}; root.inverseBindMatrix=ayt::math::Float4x4::identity();
    c->skeleton->setBone(0,root); c->jointCount=1; c->loaded=true; c->skinMatrices=new ayt::math::Float4x4[1]; c->skinMatrices[0]=ayt::math::Float4x4::identity(); c->skinMatrices[0].row[0].w=66;
    c->player->setSkeleton(c->skeleton); c->player->play(idle.get()); c->player->setTime(.75f); return c;
}
struct AnimatedFixture {
    ayt::scene::Scene scene{ayt::scene::SceneMode::Edit,"animated-test"};
    Entity* actor=nullptr; SkeletonComponent* skeleton=nullptr;
    std::shared_ptr<Animation> a=clip(0,4), b=clip(100,104), idle=clip(20,24);
    int resolves=0;
    AnimatedFixture() { registerTypes(); actor=scene.world().createEntity(); skeleton=prepareActor(actor,idle); }
    std::map<std::string,std::uint32_t> ids() const { return {{"actor",actor->getId()}}; }
    std::unique_ptr<SceneAnimationDriver> driver() { return makeAnimationSequenceDriver([&](const std::string& path)->std::shared_ptr<const Animation>{ ++resolves; return path=="a.ayanm" ? a : b; }); }
    std::shared_ptr<SceneSequenceSession> session(Sequence data=animatedModel()) {
        auto s=std::make_shared<SceneSequenceSession>(); Diagnostic error;
        if (!s->bind(scene.world(),CompiledSequence::compile(data),ids(),driver(),&error)) throw std::runtime_error(error.message);
        return s;
    }
    float pose() const { return skeleton->skinMatrices[0].row[0].w; }
};
}
TEST_SUITE(AnimationSequenceTests)
TEST_CASE(two_sections_apply_real_pose_with_offset_speed_and_boundary) {
    AnimatedFixture f; auto s=f.session(); SceneFrame frame; CHECK(f.resolves==2); CHECK_TRUE(s->play()); CHECK(f.pose()==1);
    CHECK_TRUE(s->seek(1,frame)); CHECK(f.pose()==2); CHECK_TRUE(s->seek(2,frame)); CHECK(f.pose()==100);
    CHECK_TRUE(s->seek(3,frame)); CHECK(f.pose()==102); CHECK_TRUE(s->seek(4,frame)); CHECK(f.pose()==104);
    CHECK(f.resolves==2); CHECK(frame.playback.events.empty()); CHECK(f.skeleton->player->getTime()==.75f);
    s->stop(); CHECK(f.pose()==66); CHECK(f.actor->getComponent<ayt::entity::Transform>()->position.x==10); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
}
TEST_CASE(completion_pause_and_stop_preserve_original_player_and_pose) {
    AnimatedFixture f; auto* player=f.skeleton->player.get(); auto s=f.session(); SceneFrame frame; s->play(); s->advance(1,frame); s->pause();
    const auto pose=f.pose(); s->advance(1,frame); CHECK(f.pose()==pose); CHECK(s->position()==1); s->play();
    CHECK_TRUE(s->advance(3,frame)); CHECK_TRUE(frame.playback.completed); CHECK(f.pose()==66); CHECK(f.skeleton->player.get()==player); CHECK(player->getTime()==.75f); CHECK_FALSE(player->isPaused()); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
    player->tick(.25f); CHECK(player->getTime()==1);
}
TEST_CASE(gaps_use_captured_baseline_for_both_direct_and_sequential_seek) {
    AnimatedFixture f; auto data=animatedModel(); data.animations[0].sections[0].end=1; data.animations[0].sections[1].start=3; data.animations[0].sections[1].speed=1;
    auto s=f.session(data); SceneFrame frame; CHECK_TRUE(s->seek(2,frame)); CHECK(f.pose()==66); CHECK_TRUE(s->seek(.5,frame)); CHECK(f.pose()==1.5f); CHECK_TRUE(s->seek(2,frame)); CHECK(f.pose()==66);
}
TEST_CASE(clip_mutation_unload_and_caller_release_do_not_change_owned_samples) {
    AnimatedFixture f; auto s=f.session(); f.a->unload(); f.b->setDuration(99); f.a.reset(); f.b.reset();
    SceneFrame frame; CHECK_TRUE(s->seek(1,frame)); CHECK(f.pose()==2); CHECK_TRUE(s->seek(3,frame)); CHECK(f.pose()==102); CHECK(f.resolves==2);
}
TEST_CASE(actual_source_duration_and_incompatible_bones_fail_before_rebind) {
    AnimatedFixture f; auto s=f.session(); s->play(); SceneFrame frame; s->advance(1,frame); f.b=clip(100,104,"other"); Diagnostic e;
    CHECK_FALSE(s->bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),f.driver(),&e)); CHECK(s->state()==PlaybackState::Playing); CHECK(f.pose()==2);
    f.b=clip(100,104); f.b->setDuration(3); CHECK_FALSE(s->bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),f.driver())); CHECK(s->position()==1);
}
TEST_CASE(malformed_clip_values_times_quaternions_and_unsupported_tracks_fail) {
    for (int variant=0;variant<6;++variant) {
        AnimatedFixture f; auto broken=std::make_shared<Animation>(); broken->setDuration(4); broken->setTicksPerSecond(2);
        AnimTrack t=f.a->getTracks()[0];
        if (variant==0) t.values.pop_back();
        if (variant==1) t.times[1]=t.times[0];
        if (variant==2) t.values[0]=std::numeric_limits<float>::quiet_NaN();
        if (variant==3) t.blendMode=AnimBlendMode::Additive;
        if (variant==4) { t.property="rotation"; t.valueType=AnimTrackType::Quaternion; t.values={0,0,0,2,0,0,0,1}; }
        if (variant==5) { t.property="parameter"; t.valueType=AnimTrackType::Float; t.values={0,1}; }
        broken->addTrack(t); f.a=broken; SceneSequenceSession s; Diagnostic e;
        CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),f.driver(),&e)); CHECK_FALSE(e.message.empty()); CHECK(f.pose()==66); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
    }
}
TEST_CASE(missing_clip_resolver_exception_and_unprepared_skeleton_fail_cleanly) {
    AnimatedFixture f; SceneSequenceSession s; f.a.reset(); CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),f.driver()));
    CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),makeAnimationSequenceDriver([](const std::string&)->std::shared_ptr<const Animation>{throw std::runtime_error("load");})));
    f.a=clip(0,4); f.skeleton->loaded=false; CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),f.driver()));
}
TEST_CASE(skeletal_leases_are_exclusive_and_failed_acquire_does_not_claim_transform) {
    AnimatedFixture f; auto a=f.session(); auto b=f.session(); CHECK_TRUE(a->play()); CHECK_FALSE(b->play()); a->stop(); CHECK_TRUE(b->play()); b->stop();
    int other=0; f.skeleton->externalPoseOwner=&other; CHECK_FALSE(a->play());
    auto data=animatedModel(); data.animations.clear(); SceneSequenceSession transform;
    CHECK_TRUE(transform.bind(f.scene.world(),CompiledSequence::compile(data),f.ids())); CHECK_TRUE(transform.play()); transform.stop(); f.skeleton->externalPoseOwner=nullptr;
}
TEST_CASE(external_pose_writer_and_lease_transfer_are_not_overwritten) {
    AnimatedFixture f; auto s=f.session(); s->play(); f.skeleton->skinMatrices[0].row[0].w=999; SceneFrame frame;
    CHECK_FALSE(s->advance(1,frame)); CHECK(f.pose()==999); CHECK_FALSE(bool(f.skeleton->externalPoseOwner)); CHECK(f.actor->getComponent<ayt::entity::Transform>()->position.x==10);
    CHECK_TRUE(s->play()); int owner=0; f.skeleton->externalPoseOwner=&owner; s->stop(); CHECK(f.skeleton->externalPoseOwner==&owner); f.skeleton->externalPoseOwner=nullptr;
}
TEST_CASE(skeleton_mutation_and_component_deletion_revoke_without_dereferencing_stale_data) {
    AnimatedFixture f; auto s=f.session(); s->play(); auto bone=f.skeleton->skeleton->getBones()[0]; bone.name="changed"; f.skeleton->skeleton->setBone(0,bone); SceneFrame frame;
    CHECK_FALSE(s->advance(1,frame)); CHECK(s->state()==PlaybackState::Stopped); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
    AnimatedFixture g; auto t=g.session(); t->play(); g.actor->removeComponent<SkeletonComponent>(); CHECK_FALSE(t->advance(1,frame));
}
TEST_CASE(world_teardown_and_session_destruction_release_pose_ownership) {
    AnimatedFixture f; { auto s=f.session(); s->play(); } CHECK(f.pose()==66); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
    auto s=f.session(); s->play(); f.scene.world().shutdown(); SceneFrame frame; CHECK_FALSE(s->advance(1,frame)); s->stop();
}
TEST_CASE(invalid_skeleton_topology_rest_values_and_duplicate_names_are_rejected) {
    for (int variant=0;variant<3;++variant) {
        AnimatedFixture f; auto bone=f.skeleton->skeleton->getBones()[0];
        if (variant==0) bone.parentIndex=0;
        if (variant==1) bone.localPosition.x=std::numeric_limits<float>::quiet_NaN();
        if (variant==2) { f.skeleton->skeleton->setBoneCount(2); f.skeleton->skeleton->setBone(1,bone); f.skeleton->jointCount=2; delete[] f.skeleton->skinMatrices; f.skeleton->skinMatrices=new ayt::math::Float4x4[2]; }
        f.skeleton->skeleton->setBone(0,bone); SceneSequenceSession s;
        CHECK_FALSE(s.bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),f.driver())); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
    }
}
TEST_CASE(all_skeletal_targets_are_preflighted_before_any_lease_or_transform_write) {
    AnimatedFixture f; auto* second=f.scene.world().createEntity(); auto* skeleton=prepareActor(second,f.idle); int other=0; skeleton->externalPoseOwner=&other;
    auto data=animatedModel(); data.bindings.push_back({"second"}); auto track=data.animations[0]; track.id="second-animation"; track.binding="second";
    for (auto& section : track.sections) section.id+="-second"; data.animations.push_back(track);
    auto s=std::make_shared<SceneSequenceSession>(); auto ids=f.ids(); ids["second"]=second->getId();
    CHECK_TRUE(s->bind(f.scene.world(),CompiledSequence::compile(data),ids,f.driver())); CHECK_FALSE(s->play());
    CHECK_FALSE(bool(f.skeleton->externalPoseOwner)); CHECK(f.pose()==66); CHECK(f.actor->getComponent<ayt::entity::Transform>()->position.x==10); CHECK(skeleton->externalPoseOwner==&other);
    skeleton->externalPoseOwner=nullptr;
}
TEST_CASE(sample_failure_does_not_partially_apply_transform_or_skeletal_pose) {
    AnimatedFixture f; auto broken=std::make_shared<Animation>(); broken->setDuration(4); broken->setTicksPerSecond(2);
    auto track=f.a->getTracks()[0]; track.values={1e6f,0,0,1e6f,0,0}; track.interpolation=AnimInterpolation::CubicHermite;
    track.inTangents={0,0,0,-1e6f,0,0}; track.outTangents={1e6f,0,0,0,0,0}; broken->addTrack(track); f.a=broken;
    auto s=f.session(); SceneFrame output; output.generation=123; Diagnostic error;
    CHECK_FALSE(s->seek(1,output,&error)); CHECK(output.generation==123); CHECK_FALSE(error.message.empty());
    CHECK(f.pose()==66); CHECK(f.actor->getComponent<ayt::entity::Transform>()->position.x==10); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
}
TEST_CASE(clip_notifies_never_fire_on_scrub_or_playback) {
    AnimatedFixture f; int notify=0; f.skeleton->player->setAnimNotifySink([&](const char*,float,float){++notify;}); auto s=f.session(); SceneFrame frame;
    s->seek(1,frame); CHECK(frame.playback.events.empty()); s->stop(); s->play(); s->advance(3,frame);
    CHECK(frame.playback.events.size()==2); CHECK(notify==0); CHECK(f.skeleton->player->getPendingNotifyCount()==0);
}
TEST_CASE(host_tick_applies_bone_pose_once_and_restores_on_shutdown) {
    AnimatedFixture f; auto s=f.session(); SequenceSubSystem host([&]{return &f.scene.world();}); host.initialize(); host.addSession(s); s->play();
    ayt::game::FrameContext c; c.frameCount=1; c.deltaTime=1; host.tick(ayt::game::FramePhase::Gameplay,c); host.tick(ayt::game::FramePhase::Gameplay,c);
    CHECK(f.pose()==2); CHECK(s->position()==1); CHECK(host.takeFrames().size()==1); host.shutdown(); CHECK(f.pose()==66); CHECK_FALSE(bool(f.skeleton->externalPoseOwner));
}
TEST_CASE(built_in_animation_blendspace_and_state_machine_freeze_under_lease) {
    registerTypes(); auto& world=World::instance(); world.initialize(); auto idle=clip(20,24); auto* actor=world.createEntity(); auto* skeleton=prepareActor(actor,idle);
    auto* animationComponent=actor->addComponent<AnimationComponent>(); animationComponent->clipPath="sequence-lease-no-load.ayanm";
    auto* blendComponent=actor->addComponent<BlendSpaceComponent>(); BlendSpaceEntry entry; entry.clipPath="sequence-lease-no-blend-load.ayanm"; blendComponent->entries.push_back(entry);
    auto* stateComponent=actor->addComponent<AnimationStateMachineComponent>(); stateComponent->speed=17; stateComponent->pendingTriggers={"queued"};
    auto a=clip(0,4), b=clip(100,104); auto s=std::make_shared<SceneSequenceSession>();
    CHECK_TRUE(s->bind(world,CompiledSequence::compile(animatedModel()),{{"actor",actor->getId()}},makeAnimationSequenceDriver([&](const std::string& path)->std::shared_ptr<const Animation>{return path=="a.ayanm"?a:b;}))); CHECK_TRUE(s->play());
    AnimationSystem animation; BlendSpaceSystem blend; StateMachineSystem machine;
    auto* stateMachine=machine.getOrCreateMachine(actor); ayt::anim::State state; state.name="Idle"; stateMachine->addState(state); stateMachine->setInitialState("Idle");
    animation.onUpdate(1); blend.onUpdate(1); machine.onUpdate(1); CHECK(skeleton->player->getTime()==.75f); CHECK(skeleton->skinMatrices[0].row[0].w==1); CHECK(skeleton->skinMatricesBlendSpace==nullptr);
    CHECK(stateMachine->getCurrentStateElapsedTime()==0); CHECK(stateMachine->getParam("Speed")==0); CHECK(stateComponent->pendingTriggers.size()==1);
    CHECK(ResourceManager::instance().getLoadState(animationComponent->clipPath)==ResourceLoadState::NotLoaded);
    CHECK(ResourceManager::instance().getLoadState(entry.clipPath)==ResourceLoadState::NotLoaded);
    s->stop(); CHECK(skeleton->skinMatrices[0].row[0].w==66); CHECK_FALSE(bool(skeleton->externalPoseOwner)); world.shutdown();
}
TEST_CASE(resource_manager_loads_standard_loose_clips_and_rejects_missing_assets) {
    AnimatedFixture f; auto& resources=ResourceManager::instance(); CHECK_TRUE(initializeLoaders());
    const auto roots=assetRoots(); const auto directory=std::filesystem::temp_directory_path()/
        ("aysequence-s3-resource-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Cannot create fresh sequence test directory");
    struct Cleanup {
        std::filesystem::path directory; std::vector<std::string> roots; ResourceManager& resources;
        ~Cleanup(){ for (const auto* name : {"a.ayanm","b.ayanm","missing.ayanm"}) resources.unloadResource((directory/name).string()); setAssetRoots(roots); std::error_code ignored; std::filesystem::remove_all(directory,ignored); }
    } cleanup{directory,roots,resources};
    std::vector<ayt::math::UInt8> bytes; CHECK_TRUE(f.a->saveToBinary(bytes)); CHECK_TRUE(ayt::io::File::atomicWrite((directory/"a.ayanm").string(),bytes.data(),bytes.size()));
    CHECK_TRUE(f.b->saveToBinary(bytes)); CHECK_TRUE(ayt::io::File::atomicWrite((directory/"b.ayanm").string(),bytes.data(),bytes.size())); setAssetRoot(directory.string());
    auto s=std::make_shared<SceneSequenceSession>(); Diagnostic error;
    if (!s->bind(f.scene.world(),CompiledSequence::compile(animatedModel()),f.ids(),makeResourceAnimationSequenceDriver(resources),&error)) throw std::runtime_error(error.location+": "+error.message);
    resources.unloadResource((directory/"a.ayanm").string()); resources.unloadResource((directory/"b.ayanm").string()); SceneFrame frame; CHECK_TRUE(s->seek(3,frame)); CHECK(f.pose()==102); s->stop();
    auto data=animatedModel(); data.animations[0].sections[0].source="missing.ayanm";
    CHECK_FALSE(s->bind(f.scene.world(),CompiledSequence::compile(data),f.ids(),makeResourceAnimationSequenceDriver(resources),&error)); CHECK_FALSE(error.message.empty());
}
TEST_SUITE_END
