#include <AYSequence/SequenceResource.h>
#include <AYSequence/SequenceAssetIO.h>
#include <AYSequence/SequenceCooked.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/ResourceRegistry.h>
#include <AYTest.h>
#include <AYTestFixtures.h>

#include <fstream>

using namespace ayt::sequence;

namespace {
Sequence content() {
    Sequence source;
    source.id = "resource-sequence";
    source.duration = 3;
    source.bindings = {{"camera"}};
    source.cameraCuts = {{"intro", "camera", 0, 0}};
    source.audioCues = {{"sound", "audio/intro.aud", 1, 0.8}};
    return source;
}
}

TEST_SUITE(SequenceResourceTests)

TEST_CASE(resource_manager_caches_and_reload_retains_old_snapshot) {
    ayt::test::ScratchDirectory scratch("sequence-resource");
    const auto path = (scratch.path() / "scene.seq").string();
    CHECK_TRUE(saveSequence(path, content()));
    registerSequenceResourceLoader();
    auto& manager = ayt::resource::ResourceManager::instance();
    auto loaded = manager.load<SequenceResource>(path);
    CHECK_TRUE(loaded != nullptr);
    CHECK(loaded->isLoaded());
    CHECK(loaded->sizeInBytes() > sizeof(SequenceResource));
    CHECK(loaded->snapshot()->data().audioCues.size() == 1);
    CHECK(manager.load<SequenceResource>(path) == loaded);
    auto old = loaded->snapshot();
    auto modified = content(); modified.duration = 4;
    CHECK_TRUE(saveSequence(path, modified));
    CHECK_TRUE(loaded->reload(path));
    CHECK(loaded->snapshot()->data().duration == 4);
    CHECK(old->data().duration == 3);
    { std::ofstream broken(path, std::ios::trunc); broken << "{bad"; }
    auto current = loaded->snapshot();
    CHECK_FALSE(loaded->reload(path));
    CHECK(loaded->snapshot() == current);
    CHECK_TRUE(saveSequence(path, content()));
    CHECK_TRUE(reloadSequenceResource(path));
    CHECK(manager.load<SequenceResource>(path)->snapshot()->data().duration == 3);
    manager.unloadResource(path);
}

TEST_CASE(binary_loader_validates_and_owns_sequence_snapshot) {
    registerSequenceResourceLoader();
    auto loader = ayt::resource::ResourceRegistry::createLoader("SceneSequence");
    CHECK_TRUE(loader != nullptr);
    CHECK(loader->canLoad("cut.seq"));
    CHECK(loader->canLoad("legacy.ayseq"));
    CHECK(loader->canLoad("cooked.sqc"));
    CHECK_FALSE(loader->canLoad("cut.txt"));
    std::string encoded;
    CHECK_TRUE(encodeSequence(content(), encoded));
    auto loaded = std::dynamic_pointer_cast<SequenceResource>(
        loader->loadFromBinary(encoded.data(), encoded.size()));
    CHECK_TRUE(loaded != nullptr);
    CHECK(loaded->snapshot()->data().cameraCuts[0].binding == "camera");
    const auto old = loaded->snapshot();
    CHECK_FALSE(loaded->decode("{bad", 4));
    CHECK(loaded->snapshot() == old);
    CHECK(loaded->unload());
    CHECK_FALSE(loaded->isLoaded());
    CHECK(old->data().id == "resource-sequence");
}

TEST_CASE(cooked_resource_loads_from_file_and_binary) {
    ayt::test::ScratchDirectory scratch("sequence-cooked-resource");
    std::filesystem::create_directories(scratch.path() / "audio");
    { std::ofstream sound(scratch.path() / "audio/intro.aud",
        std::ios::binary); sound << "audio-content"; }
    std::vector<std::uint8_t> bytes;
    CHECK_TRUE(encodeCookedSequence(content(), scratch.path(), bytes));
    const auto path = scratch.path() / "cut.sqc";
    { std::ofstream stream(path, std::ios::binary);
      stream.write(reinterpret_cast<const char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size())); }
    registerSequenceResourceLoader();
    auto loader = ayt::resource::ResourceRegistry::createLoader("SceneSequence");
    auto fromFile = std::dynamic_pointer_cast<SequenceResource>(
        loader->load(path.string()));
    CHECK_TRUE(fromFile != nullptr);
    CHECK(fromFile->snapshot()->data() == content());
    CHECK(fromFile->dependencySignature() != 0);
    auto fromBytes = std::dynamic_pointer_cast<SequenceResource>(
        loader->loadFromBinary(bytes.data(), bytes.size()));
    CHECK_TRUE(fromBytes != nullptr);
    CHECK(fromBytes->dependencySignature()
        == fromFile->dependencySignature());
    bytes.back() ^= 1;
    const auto previous = fromFile->snapshot();
    CHECK_FALSE(fromFile->decode(bytes.data(), bytes.size()));
    CHECK(fromFile->snapshot() == previous);
}

TEST_SUITE_END
