#include <AYSequence/SequenceDependencies.h>
#include <AYTest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

using namespace ayt::sequence;

namespace {
Sequence content() {
    Sequence value;
    value.id = "manifest";
    value.duration = 2;
    value.scenePath = "scenes/room.scn";
    value.bindings = {{"hero", "Hero"}};
    value.animations = {{"motion", "hero", {{
        "run", "clips/run.ayanm", 0, 2, 0, 1, 2}}}};
    value.audioCues = {{"hit", "audio/hit.aud", 1, 1}};
    return value;
}
void write(const std::filesystem::path& path, const char* bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream << bytes;
}
}

TEST_SUITE(SequenceDependencyTests)
TEST_CASE(manifest_tracks_content_and_keeps_output_on_failure) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("aysequence-manifest-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "scenes");
    fs::create_directories(root / "clips");
    fs::create_directories(root / "audio");
    write(root / "scenes/room.scn", "scene-a");
    write(root / "clips/run.ayanm", "clip-a");
    write(root / "audio/hit.aud", "audio-a");
    auto sequence = content();
    SequenceDependencyManifest manifest;
    Diagnostic diagnostic;
    CHECK_TRUE(buildSequenceDependencyManifest(sequence, root, manifest,
        &diagnostic));
    CHECK(manifest.entries.size() == 3);
    CHECK(manifest.entries[0].kind == SequenceDependencyKind::Scene);
    CHECK(manifest.entries[1].kind == SequenceDependencyKind::Animation);
    CHECK(manifest.entries[2].kind == SequenceDependencyKind::Audio);
    const auto first = manifest.signature;
    CHECK_TRUE(buildSequenceDependencyManifest(sequence, root, manifest));
    CHECK(manifest.signature == first);
    write(root / "audio/hit.aud", "audio-b");
    CHECK_TRUE(buildSequenceDependencyManifest(sequence, root, manifest));
    CHECK(manifest.signature != first);
    const auto second = manifest.signature;
    fs::remove(root / "clips/run.ayanm");
    CHECK_FALSE(buildSequenceDependencyManifest(sequence, root, manifest,
        &diagnostic));
    CHECK(manifest.signature == second);
    sequence.audioCues[0].source = "../outside.aud";
    CHECK_FALSE(buildSequenceDependencyManifest(sequence, root, manifest));
    CHECK(manifest.signature == second);
    fs::remove(root / "audio/hit.aud");
    fs::remove(root / "scenes/room.scn");
    fs::remove(root / "audio");
    fs::remove(root / "clips");
    fs::remove(root / "scenes");
    fs::remove(root);
}
TEST_SUITE_END
