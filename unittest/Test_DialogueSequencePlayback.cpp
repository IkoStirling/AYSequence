#include <AYSequence/DialogueSequencePlayback.h>
#include <AYTest.h>

using namespace ayt::sequence;
namespace dialogue=ayt::progression::dialogue;

namespace {
std::shared_ptr<const CompiledSequence> scene() {
    Sequence authored;
    authored.id="guard-intro";
    authored.duration=2;
    authored.eventTracks={
        {"events",{{"begin",0,"begin",""},{"end",2,"end",""}}}};
    return CompiledSequence::compile(std::move(authored));
}
}

TEST_SUITE(DialogueSequencePlaybackTests)
TEST_CASE(committed_cursor_starts_once_and_failed_resolution_keeps_prior_playback) {
    DialogueSequencePlayback bridge;
    dialogue::Presentation frame;
    frame.deliveryKey="view:guard:0";
    frame.startMedia=true;
    frame.sequenceCueKey="sequence.guard.intro";
    int resolves=0;
    bool missingReady=false;
    const DialogueSequenceResolver resolver=[&](std::string_view key) {
        ++resolves;
        return key=="sequence.guard.intro" ||
            (key=="sequence.missing" && missingReady) ? scene() : nullptr;
    };
    Diagnostic error;
    CHECK_TRUE(bridge.apply(frame,resolver,&error));
    CHECK(bridge.state()==PlaybackState::Playing);
    CHECK(bridge.sourceKey()=="sequence.guard.intro");
    PlaybackFrame output;
    CHECK_TRUE(bridge.advance(0.25,output));
    CHECK(output.events.size()==1 && output.events[0].id=="begin");
    CHECK_TRUE(bridge.apply(frame,resolver,&error));
    CHECK(resolves==1 && bridge.position()==0.25);
    frame.deliveryKey="view:guard:1";
    frame.sequenceCueKey="sequence.missing";
    CHECK_FALSE(bridge.apply(frame,resolver,&error));
    CHECK(bridge.state()==PlaybackState::Playing && bridge.position()==0.25);
    CHECK(bridge.sourceKey()=="sequence.guard.intro");
    CHECK_FALSE(error.message.empty());
    missingReady=true;
    CHECK_TRUE(bridge.apply(frame,resolver,&error));
    CHECK(bridge.position()==0 && bridge.sourceKey()=="sequence.missing");
    frame.sequenceCueKey="sequence.guard.intro";
    frame.deliveryKey="view:guard:2";
    CHECK_TRUE(bridge.apply(frame,resolver,&error));
    CHECK(bridge.position()==0 && resolves==4);
    CHECK_TRUE(bridge.advance(2,output));
    CHECK(output.completed && bridge.state()==PlaybackState::Completed);
}
TEST_CASE(restored_or_empty_cursor_stops_without_resolving_and_bad_key_preserves_player) {
    DialogueSequencePlayback bridge;
    dialogue::Presentation frame;
    frame.deliveryKey="view:guard:0";
    frame.startMedia=true;
    frame.sequenceCueKey="sequence.guard.intro";
    const DialogueSequenceResolver resolver=[](std::string_view) { return scene(); };
    CHECK_TRUE(bridge.apply(frame,resolver));
    frame.deliveryKey.clear();
    CHECK_FALSE(bridge.apply(frame,resolver));
    CHECK(bridge.state()==PlaybackState::Playing);
    frame.deliveryKey="view:guard:1";
    frame.startMedia=false;
    CHECK_TRUE(bridge.apply(frame,{}));
    CHECK(bridge.state()==PlaybackState::Stopped && bridge.sourceKey().empty());
    frame.deliveryKey="view:guard:2";
    frame.startMedia=true;
    frame.sequenceCueKey.clear();
    CHECK_TRUE(bridge.apply(frame,{}));
    CHECK(bridge.state()==PlaybackState::Stopped);
}
TEST_CASE(dialogue_commit_drives_sequence_and_restore_does_not_replay) {
    dialogue::Document authored;
    authored.id="guard";
    authored.startNode="hello";
    authored.nodes={
        {"hello","guard","line.hello",{{"continue","choice.continue","bye",{}, {}}},
            "","sequence.guard.intro"},
        {"bye","guard","line.bye",{}}
    };
    dialogue::Session session;
    CHECK_TRUE(bool(session.install(authored,"guard/player")));
    dialogue::PresentationController presenter;
    DialogueSequencePlayback bridge;
    int resolves=0;
    const DialogueSequenceResolver resolver=[&](std::string_view) {
        ++resolves; return scene();
    };
    const dialogue::PresentationSink sink=[&](const dialogue::Presentation& frame) {
        return bridge.apply(frame,resolver);
    };
    CHECK_TRUE(bool(presenter.begin(session,sink)));
    CHECK(bridge.state()==PlaybackState::Playing && resolves==1);
    auto transition=session.choose("continue",[](std::string_view){return true;},
        [](const dialogue::Transition&){return true;});
    CHECK_TRUE(bool(transition));
    CHECK_TRUE(bool(presenter.accepted(session,*transition,sink)));
    CHECK(bridge.state()==PlaybackState::Stopped && resolves==1);
    dialogue::Session restored;
    CHECK_TRUE(bool(restored.install(authored,"guard/player")));
    CHECK_TRUE(bool(restored.restore(session.snapshot())));
    dialogue::PresentationController resumed;
    CHECK_TRUE(bool(resumed.restored(restored,sink)));
    CHECK(bridge.state()==PlaybackState::Stopped && resolves==1);
}
TEST_SUITE_END
