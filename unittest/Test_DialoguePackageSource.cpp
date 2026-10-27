#include <AYSequence/DialoguePackageSource.h>
#include <AYSequence/DialogueMediaSession.h>
#include <AYSequence/SequenceCooked.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYLocalization.h>
#include <AYResource/assetsImpl/Audio.h>
#include <AYStorage/IPackageWriter.h>
#include <AYUI/TextLabel.h>
#include <AYTest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace ayt::sequence;

TEST_SUITE(DialoguePackageSourceTests)
TEST_CASE(published_dialogue_resolves_text_voice_and_sequence_without_eager_media)
{
    namespace dialogue = ayt::progression::dialogue;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto packagePath = std::filesystem::temp_directory_path() /
        ("ay-dialogue-package-" + std::to_string(unique) + ".pak");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{packagePath};
    const std::string document = R"({"schema":1,"id":"story.intro","contentVersion":1,"startNode":"first","nodes":[{"id":"first","speakerKey":"npc.greeter","textKey":"dialogue.welcome","voiceCueKey":"voice.welcome","sequenceCueKey":"sequence.intro","choices":[{"id":"leave","textKey":"dialogue.leave","nextNode":"","requiredFacts":[],"emittedFacts":[]}]}]})";
    const std::string locale = R"({"_metadata":{"language":"en-US"},"npc":{"greeter":"Greeter"},"dialogue":{"welcome":"Welcome","leave":"Goodbye"}})";
    const std::string zhLocale = R"({"_metadata":{"language":"zh-CN"},"npc":{"greeter":"向导"},"dialogue":{"welcome":"欢迎","leave":"再见"}})";
    dialogue::Bindings bindings;
    bindings.documents = {"dialogue/intro.dialogue.json"};
    bindings.locales = {"locales/en-US.json", "locales/zh-CN.json"};
    bindings.voice.emplace("voice.welcome", "audio/welcome.aud");
    bindings.sequence.emplace("sequence.intro", "scenes/intro.sqc");
    const auto encodedBindings = dialogue::encodeBindingsJson(bindings);
    CHECK(encodedBindings.has_value());
    if (!encodedBindings) return;
    ayt::resource::Audio audio;
    audio.createSilence(32);
    std::vector<std::uint8_t> voiceBytes;
    CHECK(audio.saveToBinary(voiceBytes));
    Sequence sequence;
    sequence.id = "intro";
    sequence.duration = 1;
    sequence.eventTracks = {{"events", {{"start", 0, "start", ""}}}};
    std::vector<std::uint8_t> sequenceBytes;
    Diagnostic sequenceError;
    CHECK(encodeCookedSequence(sequence, packagePath.parent_path(),
        sequenceBytes, &sequenceError));
    auto writePackage = [&](const std::vector<std::uint8_t>& voice,
                            std::string_view authored = {}) {
        if (authored.empty()) authored = document;
        auto writer = ayt::storage::IPackageWriter::create(packagePath.string());
        if (!writer) return false;
        return writer->addData(encodedBindings->data(), encodedBindings->size(),
                   "dialogue/bindings.json") &&
            writer->addData(authored.data(), authored.size(),
                   "dialogue/intro.dialogue.json") &&
            writer->addData(locale.data(), locale.size(), "locales/en-US.json") &&
            writer->addData(zhLocale.data(), zhLocale.size(), "locales/zh-CN.json") &&
            writer->addData(voice.data(), voice.size(), "audio/welcome.aud") &&
            writer->addData(sequenceBytes.data(), sequenceBytes.size(),
                   "scenes/intro.sqc") && writer->flush();
    };
    CHECK(writePackage(voiceBytes));
    DialoguePackageSource source;
    std::string error;
    CHECK(source.open(packagePath.string(), "dialogue/bindings.json", &error));
    CHECK(source.preflightDocumentMedia("dialogue/intro.dialogue.json", &error));
    CHECK(error.empty());
    CHECK(source.bindings() && source.bindings()->voicePath("voice.welcome") ==
        "audio/welcome.aud");
    CHECK_FALSE(source.open(packagePath.string(), "../bindings.json", &error));
    CHECK(source.ready());
    CHECK_FALSE(source.loadDocument("dialogue/missing.dialogue.json", &error).has_value());
    const auto authored = source.loadDocument("dialogue/intro.dialogue.json", &error);
    CHECK(authored.has_value());
    if (!authored) return;
    ayt::localization::Localization texts;
    CHECK(source.loadLocale("locales/en-US.json", texts, &error));
    texts.setLanguage("en-US");
    CHECK(texts.get("dialogue.welcome") == "Welcome");
    CHECK(source.loadSequence("sequence.intro", &error) != nullptr);
    CHECK(source.loadVoice("voice.welcome", &error) != nullptr);
    ayt::audio::AudioSubSystem audioSystem;
    CHECK(audioSystem.initialize());
    ayt::ui::TextLabel speaker, body;
    DialogueMediaSession media(audioSystem);
    CHECK(media.bindText(speaker, body));
    dialogue::Session session;
    CHECK(session.install(*authored, "story.intro/player").has_value());
    dialogue::PresentationController presenter;
    const auto sequenceResolver = source.sequenceResolver();
    const auto voiceLoader = source.voiceLoader();
    dialogue::Presentation lastPresentation;
    const dialogue::PresentationSink sink = [&](const dialogue::Presentation& frame) {
        lastPresentation = frame;
        return media.present(frame, sequenceResolver, voiceLoader, texts, &error);
    };
    CHECK(presenter.begin(session, sink).has_value());
    CHECK(body.isVisible() && body.getText() == L"Welcome");
    CHECK(media.voice().sourceKey() == "voice.welcome");
    PlaybackFrame frame;
    CHECK(media.advance(0.1, frame));
    CHECK(frame.events.size() == 1);
    dialogue::PresentationController resumed;
    CHECK(resumed.restored(session, sink).has_value());
    CHECK(media.voice().sourceKey().empty());
    CHECK(body.isVisible() && body.getText() == L"Welcome");
    CHECK(source.loadLocale("locales/zh-CN.json", texts, &error));
    texts.setLanguage("zh-CN");
    CHECK(media.present(lastPresentation, sequenceResolver,
        voiceLoader, texts, &error));
    CHECK(body.isVisible() && body.getText() == L"欢迎");
    CHECK(media.voice().sourceKey().empty());
    media.stop();
    audioSystem.shutdown();

    audio.createSilence(64);
    std::vector<std::uint8_t> replacement;
    CHECK(audio.saveToBinary(replacement));
    CHECK(writePackage(replacement));
    CHECK(source.loadVoice("voice.welcome", &error) == nullptr);
    CHECK(error.find("changed") != std::string::npos);
    CHECK(source.open(packagePath.string(), "dialogue/bindings.json", &error));
    CHECK(source.loadVoice("voice.welcome", &error) != nullptr);
    CHECK(writePackage(std::vector<std::uint8_t>{0, 1, 2}));
    CHECK(source.open(packagePath.string(), "dialogue/bindings.json", &error));
    CHECK(source.loadVoice("voice.welcome", &error) == nullptr);
    CHECK_FALSE(source.preflightDocumentMedia("dialogue/intro.dialogue.json", &error));
    CHECK(error.find("first voice voice.welcome") != std::string::npos);
    CHECK(source.ready());
    auto unboundDocument=document;
    const auto cue=unboundDocument.find("voice.welcome");
    CHECK(cue != std::string::npos);
    unboundDocument.replace(cue,std::string("voice.welcome").size(),"voice.missing");
    CHECK(writePackage(voiceBytes,unboundDocument));
    CHECK(source.open(packagePath.string(),"dialogue/bindings.json",&error));
    CHECK_FALSE(source.preflightDocumentMedia("dialogue/intro.dialogue.json",&error));
    CHECK(error.find("voice.missing") != std::string::npos);
}
TEST_SUITE_END
