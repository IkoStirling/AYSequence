#include <AYSequence/DialogueMediaSession.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYResource/assetsImpl/Audio.h>
#include <AYLocalization.h>
#include <AYUI/TextLabel.h>
#include <AYTest.h>

#include <vector>

using namespace ayt::sequence;
namespace dialogue=ayt::progression::dialogue;

TEST_SUITE(DialogueMediaSessionTests)
TEST_CASE(committed_dialogue_survives_partial_media_failure_and_restores_text) {
    ayt::audio::AudioSubSystem audio;
    CHECK_TRUE(audio.initialize());
    auto& locale=ayt::localization::Localization::instance();
    locale.clear(); locale.addSupportedLanguage("en-US");
    locale.setLanguage("en-US");
    locale.setText("line.hello","Hello");
    locale.setText("line.bye","Goodbye");
    ayt::ui::TextLabel speaker,body;
    DialogueMediaSession media(audio);
    CHECK_TRUE(media.bindText(speaker,body));
    Sequence authoredSequence;
    authoredSequence.id="intro";
    authoredSequence.duration=2;
    authoredSequence.eventTracks={{"events",{{"start",0,"start",""}}}};
    const auto compiled=CompiledSequence::compile(authoredSequence);
    CHECK(compiled!=nullptr);
    const DialogueSequenceResolver sequenceLoader=
        [&](std::string_view key) -> std::shared_ptr<const CompiledSequence> {
            return key=="scene.intro"?compiled:nullptr;
        };
    bool voiceReady=false;
    const DialogueVoicePlayer::Loader voiceLoader=[&](const std::string& key)
        -> std::shared_ptr<ayt::resource::IAudio> {
        if (key!="voice.hello" || !voiceReady) return nullptr;
        auto clip=std::make_shared<ayt::resource::Audio>();
        clip->setFormat(48000,1,16,48000);
        clip->setData(std::vector<std::uint8_t>(96000,0));
        return clip;
    };
    dialogue::Document authored;
    authored.id="guard";
    authored.startNode="hello";
    authored.nodes={
        {"hello","speaker.guard","line.hello",
            {{"leave","choice.leave","bye",{}, {}}},
            "voice.hello","scene.intro"},
        {"bye","speaker.guard","line.bye",{}}
    };
    dialogue::Session dialogueSession;
    CHECK_TRUE(bool(dialogueSession.install(authored,"guard/player")));
    dialogue::PresentationController presenter;
    std::string error;
    const dialogue::PresentationSink sink=[&](const dialogue::Presentation& frame) {
        return media.present(frame,sequenceLoader,voiceLoader,locale,&error);
    };
    CHECK_FALSE(bool(presenter.begin(dialogueSession,sink)));
    CHECK(media.sequence().state()==PlaybackState::Playing);
    CHECK_FALSE(body.isVisible());
    PlaybackFrame played;
    CHECK_TRUE(media.advance(0.25,played));
    CHECK(played.events.size()==1);
    voiceReady=true;
    CHECK_TRUE(bool(presenter.begin(dialogueSession,sink)));
    CHECK(media.sequence().position()==0.25);
    CHECK(media.voice().sourceKey()=="voice.hello");
    CHECK(body.isVisible() && body.getText()==L"Hello");
    auto transition=dialogueSession.choose("leave",
        [](std::string_view){return true;},
        [](const dialogue::Transition&){return true;});
    CHECK_TRUE(bool(transition));
    CHECK_TRUE(bool(presenter.accepted(dialogueSession,*transition,sink)));
    CHECK(media.sequence().state()==PlaybackState::Stopped);
    CHECK(media.voice().sourceKey().empty());
    CHECK(body.isVisible() && body.getText()==L"Goodbye");
    dialogue::PresentationController resumed;
    CHECK_TRUE(bool(resumed.restored(dialogueSession,sink)));
    CHECK(media.sequence().state()==PlaybackState::Stopped);
    CHECK(body.getText()==L"Goodbye");
    media.stop();
    locale.clear();
    audio.shutdown();
}
TEST_SUITE_END
