#include <AYSequence/SequenceAssetIO.h>
#include <AYSequence/SequenceAuthoring.h>
#include <AYTest.h>
#include <limits>
using namespace ayt::sequence;
namespace {
Sequence authored() {
    Sequence s;
    s.id = "authoring";
    s.duration = 6;
    s.bindings = {{"actor", "Character"}};
    s.scenePath = "worlds/test.ayscene";
    s.transforms = {{"track",
                     "actor",
                     Channel::Position,
                     {{"key0", 0, {0, 0, 0, 0}}, {"key1", 4, {4, 0, 0, 0}}}}};
    s.animations = {
        {"clips",
         "actor",
         {{"section0", "walk.ayanm", 0, 2, 1, 1, 6}, {"section1", "walk.ayanm", 3, 5, 0, 1, 6}}}};
    s.eventTracks = {{"events", {{"event0", 1, "marker", "text"}, {"event1", 2, "marker", ""}}}};
    return s;
}
} // namespace
TEST_SUITE(SequenceAuthoringTests)
TEST_CASE(authoring_is_owned_revisioned_and_noop_preserves_cache) {
    SequenceAuthoring model;
    auto input = authored();
    CHECK_TRUE(model.replace(input));
    auto snapshot = model.snapshot();
    auto revision = model.revision();
    input.transforms[0].keys[0].value[0] = 999;
    CHECK(model.snapshot()->data().transforms[0].keys[0].value[0] == 0);
    CHECK_TRUE(model.edit([](Sequence &) { return true; }));
    CHECK(model.snapshot() == snapshot);
    CHECK(model.revision() == revision);
    CHECK_TRUE(model.edit([](Sequence &s) {
        s.duration = 7;
        return true;
    }));
    CHECK(model.revision() == revision + 1);
    CHECK(snapshot->data().duration == 6);
}
TEST_CASE(rejected_or_throwing_edits_leave_original_identity_unchanged) {
    SequenceAuthoring m;
    m.replace(authored());
    auto s = m.snapshot();
    Diagnostic d;
    CHECK_FALSE(m.edit(
        [](Sequence &s) {
            s.duration = -1;
            return true;
        },
        &d));
    CHECK_FALSE(d.message.empty());
    CHECK(m.snapshot() == s);
    CHECK_FALSE(m.edit(
        [](Sequence &s) -> bool {
            s.transforms.clear();
            throw 1;
        },
        &d));
    CHECK(m.snapshot() == s);
    CHECK_FALSE(m.edit([](Sequence &s) {
        s.transforms.clear();
        return false;
    }));
    CHECK(m.snapshot() == s);
}
TEST_CASE(key_edit_sorts_ids_and_collision_is_atomic) {
    SequenceAuthoring m;
    m.replace(authored());
    CHECK_TRUE(m.edit([](Sequence &s) { return updateSequenceKey(s, "key0", 5, {10, 0, 0, 0}); }));
    CHECK(m.snapshot()->data().transforms[0].keys.back().id == "key0");
    auto before = m.snapshot();
    CHECK_FALSE(m.edit([](Sequence &s) { return updateSequenceKey(s, "key0", 4, {9, 0, 0, 0}); }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(group_move_mixes_transform_keys_and_events_atomically) {
    SequenceAuthoring m;
    m.replace(authored());
    CHECK_TRUE(m.edit([](Sequence &s) { return moveSequenceItems(s, {"key0", "event0"}, .5); }));
    CHECK(m.snapshot()->data().transforms[0].keys[0].time == .5);
    CHECK(m.snapshot()->data().eventTracks[0].events[0].time == 1.5);
    auto before = m.snapshot();
    CHECK_FALSE(m.edit([](Sequence &s) { return moveSequenceItems(s, {"key0", "missing"}, 1); }));
    CHECK(m.snapshot() == before);
    CHECK_FALSE(m.edit([](Sequence &s) { return moveSequenceItems(s, {"key0", "key0"}, 1); }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(section_move_preserves_duration_offset_and_rejects_overlap) {
    SequenceAuthoring m;
    m.replace(authored());
    CHECK_TRUE(m.edit([](Sequence &s) { return moveSequenceSection(s, "section0", .5); }));
    auto sec = m.snapshot()->data().animations[0].sections[0];
    CHECK(sec.end == 2.5);
    CHECK(sec.sourceOffset == 1);
    auto before = m.snapshot();
    CHECK_FALSE(m.edit([](Sequence &s) { return moveSequenceSection(s, "section0", 2); }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(left_trim_preserves_source_time_and_range_validation) {
    SequenceAuthoring m;
    m.replace(authored());
    CHECK_TRUE(m.edit([](Sequence &s) { return trimSequenceSection(s, "section0", .5, 1.5); }));
    const auto &sec = m.snapshot()->data().animations[0].sections[0];
    CHECK(sec.sourceOffset == 1.5);
    CHECK(sec.start == .5);
    CHECK(sec.end == 1.5);
    auto before = m.snapshot();
    CHECK_FALSE(m.edit([](Sequence &s) { return trimSequenceSection(s, "section0", 0, 6); }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(delete_last_item_removes_invalid_empty_value_and_animation_tracks) {
    SequenceAuthoring m;
    m.replace(authored());
    CHECK_TRUE(m.edit([](Sequence &s) {
        return removeSequenceItems(s, {"key0", "key1", "section0", "section1"});
    }));
    CHECK(m.snapshot()->data().transforms.empty());
    CHECK(m.snapshot()->data().animations.empty());
    CHECK(m.snapshot()->data().bindings.size() == 1);
    auto before = m.snapshot();
    CHECK_FALSE(m.edit([](Sequence &s) { return removeSequenceItems(s, {"event0", "missing"}); }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(stable_ids_are_unique_across_all_domains) {
    SequenceAuthoring m;
    m.replace(authored());
    auto id = m.nextId("key");
    CHECK_TRUE(m.edit([&](Sequence &s) {
        s.eventTracks[0].events.push_back({id, 5, "event", ""});
        return true;
    }));
    CHECK(m.nextId("key") != id);
}
TEST_CASE(portable_scene_and_entity_locator_limits) {
    SequenceAuthoring m;
    m.replace(authored());
    auto before = m.snapshot();
    for (auto path : {"../bad.ayscene", "C:/bad.ayscene", "/bad.ayscene", "worlds/../bad.ayscene",
                      "worlds/bad.txt"})
        CHECK_FALSE(m.edit([&](Sequence &s) {
            s.scenePath = path;
            return true;
        }));
    CHECK_FALSE(m.edit([](Sequence &s) {
        s.bindings[0].entityName = std::string("a\0b", 3);
        return true;
    }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(schema_two_roundtrip_preserves_scene_binding_and_rejects_unknown_fields) {
    auto input = authored();
    std::string bytes;
    CHECK_TRUE(encodeSequence(input, bytes));
    CHECK(bytes.find("\"version\": 2") != std::string::npos);
    Sequence output;
    CHECK_TRUE(decodeSequence(bytes, output));
    CHECK(input == output);
    bytes.insert(bytes.find('{') + 1, "\"unknown\":1,");
    auto before = output;
    CHECK_FALSE(decodeSequence(bytes, output));
    CHECK(output == before);
}
TEST_CASE(legacy_schema_one_remains_readable_and_no_locator_roundtrip) {
    auto s = authored();
    s.scenePath.clear();
    s.bindings[0].entityName.clear();
    std::string bytes;
    CHECK_TRUE(encodeSequence(s, bytes));
    CHECK(bytes.find("\"version\": 1") != std::string::npos);
    Sequence output;
    CHECK_TRUE(decodeSequence(bytes, output));
    CHECK(output == s);
}
TEST_CASE(authoring_rejects_nonfinite_and_invalid_rotation_without_normalizing_asset) {
    SequenceAuthoring m;
    m.replace(authored());
    auto before = m.snapshot();
    CHECK_FALSE(m.edit([](Sequence &s) {
        return updateSequenceKey(s, "key0", 0, {std::numeric_limits<double>::quiet_NaN(), 0, 0, 0});
    }));
    CHECK(m.snapshot() == before);
    CHECK_FALSE(m.edit([](Sequence &s) {
        s.transforms[0].channel = Channel::Rotation;
        return true;
    }));
    CHECK(m.snapshot() == before);
}
TEST_CASE(nested_mutations_cannot_replace_outer_edit_baseline) {
    SequenceAuthoring m;
    m.replace(authored());
    CHECK_TRUE(m.edit([&](Sequence &s) {
        CHECK_FALSE(m.edit([](Sequence &s) {
            s.duration = 9;
            return true;
        }));
        CHECK_FALSE(m.replace(authored()));
        s.duration = 7;
        return true;
    }));
    CHECK(m.snapshot()->data().duration == 7);
}
TEST_SUITE_END
