#include <AYSequence/SequenceAudioPlayer.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYAudio/AudioEngine.h>
#include <AYResource/assetsImpl/Audio.h>
#include <AYTest.h>

#include <memory>
#include <vector>

using namespace ayt::sequence;

namespace {
Sequence source() {
    Sequence value;
    value.id = "audio-bridge";
    value.duration = 4;
    value.audioCues = {{"first", "audio/first.aud", 0, 0.5},
                       {"second", "audio/first.aud", 2, 2},
                       {"third", "audio/third.aud", 3, 1}};
    return value;
}
std::shared_ptr<ayt::resource::IAudio> clip(const std::string&) {
    auto value = std::make_shared<ayt::resource::Audio>();
    value->setFormat(48000, 1, 16, 4800);
    value->setData(std::vector<std::uint8_t>(9600, 0));
    return value;
}
}

TEST_SUITE(SequenceAudioPlayerTests)

TEST_CASE(preload_reuses_sources_and_plays_validated_cues) {
    ayt::audio::AudioSubSystem audio;
    CHECK_TRUE(audio.initialize());
    SequenceAudioPlayer player(audio);
    const auto compiled = CompiledSequence::compile(source());
    CHECK_TRUE(compiled != nullptr);
    std::size_t loads = 0;
    std::string error;
    CHECK_TRUE(player.prepare(compiled, [&](const std::string& path) {
        ++loads;
        return clip(path);
    }, &error));
    CHECK(error.empty());
    CHECK(loads == 2);
    CHECK(player.preparedSources() == 2);
    CHECK_FALSE(player.play({"first", "audio/first.aud", 0, 0.6}));
    CHECK_FALSE(player.play({"other", "audio/first.aud", 0, 0.5}));
    CHECK_TRUE(player.play(compiled->data().audioCues[0]));
    CHECK_TRUE(player.play(compiled->data().audioCues[1]));
    CHECK(player.activeVoices() == 2);
    player.stop();
    CHECK(player.activeVoices() == 0);
    CHECK(player.preparedSources() == 2);
    player.clear();
    CHECK(audio.engine()->clipRegistry().size() == 0);
    audio.shutdown();
}

TEST_CASE(failed_preparation_releases_partial_clips_and_audio_shutdown_invalidates) {
    ayt::audio::AudioSubSystem audio;
    CHECK_TRUE(audio.initialize());
    SequenceAudioPlayer player(audio);
    const auto compiled = CompiledSequence::compile(source());
    std::string error;
    CHECK_FALSE(player.prepare(compiled, [](const std::string& path) {
        return path == "audio/first.aud" ? clip(path) : nullptr;
    }, &error));
    CHECK_FALSE(error.empty());
    CHECK(player.preparedSources() == 0);
    CHECK(audio.engine()->clipRegistry().size() == 0);
    CHECK_TRUE(player.prepare(compiled, clip));
    audio.shutdown();
    CHECK_FALSE(player.play(compiled->data().audioCues[0]));
    CHECK_TRUE(audio.initialize());
    CHECK_FALSE(player.play(compiled->data().audioCues[0]));
    player.clear();
    CHECK(player.preparedSources() == 0);
    CHECK_TRUE(player.prepare(compiled, clip));
    CHECK_TRUE(player.play(compiled->data().audioCues[0]));
    player.clear();
    audio.shutdown();
}

TEST_SUITE_END
