#include <AYSequence/DialogueVoicePlayer.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYAudio/AudioEngine.h>
#include <AYResource/assetsImpl/Audio.h>
#include <AYTest.h>

#include <vector>

using namespace ayt::sequence;
namespace dialogue=ayt::progression::dialogue;

namespace {
std::shared_ptr<ayt::resource::IAudio> clip() {
    auto value=std::make_shared<ayt::resource::Audio>();
    value->setFormat(48000,1,16,48000);
    value->setData(std::vector<std::uint8_t>(96000,0));
    return value;
}
}

TEST_SUITE(DialogueVoicePlayerTests)
TEST_CASE(voice_cursor_starts_once_retries_missing_asset_and_restores_silently) {
    ayt::audio::AudioSubSystem audio;
    CHECK_TRUE(audio.initialize());
    DialogueVoicePlayer player(audio);
    dialogue::Presentation frame;
    frame.deliveryKey="view:guard:0";
    frame.startMedia=true;
    frame.voiceCueKey="voice.guard.hello";
    int loads=0;
    bool answerReady=false;
    const DialogueVoicePlayer::Loader loader=[&](const std::string& key) {
        ++loads;
        if (key=="voice.guard.answer" && !answerReady) return
            std::shared_ptr<ayt::resource::IAudio>{};
        return clip();
    };
    std::string error;
    CHECK_TRUE(player.apply(frame,loader,&error));
    CHECK(error.empty() && player.sourceKey()=="voice.guard.hello");
    CHECK(audio.engine()->clipRegistry().size()==1);
    CHECK_TRUE(player.apply(frame,loader));
    CHECK(loads==1 && audio.engine()->clipRegistry().size()==1);
    frame.deliveryKey="view:guard:1";
    frame.voiceCueKey="voice.guard.answer";
    CHECK_FALSE(player.apply(frame,loader,&error));
    CHECK_FALSE(error.empty());
    CHECK(player.sourceKey()=="voice.guard.hello");
    CHECK(audio.engine()->clipRegistry().size()==1);
    answerReady=true;
    CHECK_TRUE(player.apply(frame,loader));
    CHECK(player.sourceKey()=="voice.guard.answer");
    CHECK(audio.engine()->clipRegistry().size()==1);
    frame.deliveryKey="view:guard:2";
    frame.startMedia=false;
    CHECK_TRUE(player.apply(frame,{},&error));
    CHECK(player.sourceKey().empty() && audio.engine()->clipRegistry().size()==0);
    player.clear();
    audio.shutdown();
}
TEST_CASE(audio_restart_invalidates_voice_handles_and_same_delivery_can_reprepare) {
    ayt::audio::AudioSubSystem audio;
    CHECK_TRUE(audio.initialize());
    DialogueVoicePlayer player(audio);
    dialogue::Presentation frame;
    frame.deliveryKey="view:guard:0";
    frame.startMedia=true;
    frame.voiceCueKey="voice.guard.hello";
    const DialogueVoicePlayer::Loader loader=[](const std::string&) { return clip(); };
    CHECK_TRUE(player.apply(frame,loader));
    audio.shutdown();
    CHECK_FALSE(player.playing());
    CHECK_TRUE(audio.initialize());
    CHECK_TRUE(player.apply(frame,loader));
    CHECK(audio.engine()->clipRegistry().size()==1);
    player.clear();
    CHECK(audio.engine()->clipRegistry().size()==0);
    frame.deliveryKey.clear();
    CHECK_FALSE(player.apply(frame,loader));
    audio.shutdown();
}
TEST_SUITE_END
