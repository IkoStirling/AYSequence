#include <AYProgression/DialogueJson.h>
#include <AYProgression/DialogueBindings.h>
#include <AYProgression/DialoguePresentation.h>
#include <AYSequence/DialogueMediaSession.h>
#include <AYSequence/DialoguePackageSource.h>
#include <AYSequence/SequenceCooked.h>
#include <AYAudio/AudioSubSystem.h>
#include <AYLocalization.h>
#include <AYResource/assetsImpl/Audio.h>
#include <AYStorage/IPackageWriter.h>
#include <AYUI/TextLabel.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace d = ayt::progression::dialogue;
namespace s = ayt::sequence;

static std::optional<std::string> readExample(const std::string& path) {
    std::ifstream file(std::string(AY_PROGRESSION_EXAMPLE_DIR) + "/" + path,
                       std::ios::binary);
    if (!file) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>{file}, {});
}

int main() {
    const auto authored = readExample("town.dialogue.json");
    const auto english = readExample("locales/en-US.json");
    const auto chinese = readExample("locales/zh-CN.json");
    if (!authored || !english || !chinese) return 1;
    d::Bindings bindings;
    bindings.documents={"dialogue/town.dialogue.json"};
    bindings.locales={"locales/en-US.json","locales/zh-CN.json"};
    bindings.voice.emplace("voice.greeter.welcome","audio/greeter.aud");
    bindings.sequence.emplace("sequence.greeter.intro","scenes/greeter.sqc");
    const auto encodedBindings=d::encodeBindingsJson(bindings);
    if (!encodedBindings) return 2;

    s::Sequence opening;
    opening.id = "greeter.intro";
    opening.duration = 1;
    opening.bindings = {{"greeter.camera", "GreeterCamera"}};
    opening.cameraCuts = {{"opening", "greeter.camera", 0, 0}};
    const auto packagePath=std::filesystem::temp_directory_path() /
        ("ay-town-story-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())+".pak");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path,ignored); }
    } cleanup{packagePath};
    std::vector<std::uint8_t> sequenceBytes,voiceBytes;
    s::Diagnostic sequenceError;
    if (!s::encodeCookedSequence(opening,packagePath.parent_path(),
            sequenceBytes,&sequenceError)) return 3;
    ayt::resource::Audio exampleVoice;
    exampleVoice.createSilence(2048); // Replace with published performance audio.
    if (!exampleVoice.saveToBinary(voiceBytes)) return 4;
    auto writer=ayt::storage::IPackageWriter::create(packagePath.string());
    if (!writer ||
        !writer->addData(encodedBindings->data(),encodedBindings->size(),
                         "dialogue/bindings.json") ||
        !writer->addData(authored->data(),authored->size(),
                         "dialogue/town.dialogue.json") ||
        !writer->addData(english->data(),english->size(),"locales/en-US.json") ||
        !writer->addData(chinese->data(),chinese->size(),"locales/zh-CN.json") ||
        !writer->addData(sequenceBytes.data(),sequenceBytes.size(),
                         "scenes/greeter.sqc") ||
        !writer->addData(voiceBytes.data(),voiceBytes.size(),"audio/greeter.aud") ||
        !writer->flush()) return 5;
    writer.reset();
    s::DialoguePackageSource package;
    std::string error;
    if (!package.open(packagePath.string(),"dialogue/bindings.json",&error) ||
        !package.preflightDocumentMedia("dialogue/town.dialogue.json",&error))
        return 6;
    const auto document=package.loadDocument("dialogue/town.dialogue.json",&error);
    if (!document) return 7;
    ayt::localization::Localization locale;
    if (!package.loadLocale("locales/en-US.json",locale,&error) ||
        !package.loadLocale("locales/zh-CN.json",locale,&error)) return 8;
    locale.setLanguage("en-US");
    const auto sequenceResolver=package.sequenceResolver();
    const auto voiceLoader=package.voiceLoader();

    ayt::audio::AudioSubSystem audio;
    if (!audio.initialize()) return 9;
    ayt::ui::TextLabel speaker, body;
    s::DialogueMediaSession media(audio);
    if (!media.bindText(speaker, body)) return 10;
    d::Presentation last;
    const d::PresentationSink sink = [&](const d::Presentation& frame) {
        last = frame;
        return media.present(frame, sequenceResolver, voiceLoader, locale, &error);
    };

    d::Session session;
    if (!session.install(*document, "town.greeter/player-one")) return 7;
    std::set<std::string> facts{"quest.greeting.started"}, receipts;
    const auto hasFact = [&](std::string_view key) {
        return facts.contains(std::string(key));
    };
    const auto commit = [&](const d::Transition& transition) {
        if (receipts.insert(transition.receipt).second)
            for (const auto& fact : transition.emittedFacts) facts.insert(fact);
        return true;
    };
    d::PresentationController presenter;
    if (!presenter.begin(session, sink) || !body.isVisible() ||
        media.voice().sourceKey() != "voice.greeter.welcome") return 8;
    s::PlaybackFrame picture;
    if (!media.advance(0.1, picture) || !picture.evaluation.camera ||
        picture.evaluation.camera->binding != "greeter.camera") return 9;
    std::cout << "welcome: localized subtitle, voice and camera cue\n";

    const auto ask = session.choose("ask", hasFact, commit);
    if (!ask || !presenter.accepted(session, *ask, sink)) return 10;
    const auto englishAnswer = body.getText();
    const auto saved = d::encodeDialogueSnapshotJson(session.snapshot());
    const auto snapshot = d::decodeDialogueSnapshotJson(saved);
    if (!snapshot) return 11;
    d::Session reopened;
    if (!reopened.install(*document, "town.greeter/player-one") ||
        !reopened.restore(*snapshot)) return 12;
    d::PresentationController resumed;
    if (!resumed.restored(reopened, sink) || !media.voice().sourceKey().empty())
        return 13;
    locale.setLanguage("zh-CN");
    if (!media.present(last, sequenceResolver, voiceLoader, locale, &error) ||
        !body.isVisible() || body.getText() == englishAnswer) return 14;
    std::cout << "answer: restored subtitle changed language without replay\n";

    const auto thanks = reopened.choose("thanks", hasFact, commit);
    if (!thanks || !resumed.accepted(reopened, *thanks, sink) ||
        !reopened.finished() || !facts.contains("dialogue.greeter.thanked"))
        return 15;
    std::cout << "farewell: choice facts committed once\n";
    media.stop();
    audio.shutdown();
    return 0;
}
