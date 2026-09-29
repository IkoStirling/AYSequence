#include <AYSequence/SequenceAssetIO.h>
#include <AYTest.h>
#include <AYTestFixtures.h>
#include <cmath>
#include <filesystem>
#include <limits>

using namespace ayt::sequence;
namespace {
Sequence fixture() {
    Sequence s; s.id = "scene-sequence"; s.duration = 4; s.bindings = {{"left"}, {"right"}};
    s.transforms = {{"left-pos", "left", Channel::Position, {{"lp0", 0, {0,0,0,0}}, {"lp1", 4, {8,0,0,0}}}}, {"right-scale", "right", Channel::Scale, {{"rs0", 0, {1,1,1,0}}, {"rs1", 4, {2,3,4,0}}}}};
    s.animations = {{"action", "left", {{"walk", "clips/walk.ayanm", 0,2,1,2,5}, {"idle", "clips/idle.ayanm",2,4,0,1,2}}}};
    s.eventTracks = {{"events", {{"ev0",0,"start",""}, {"ev1",1,"beat","a"}, {"ev2",4,"finish",""}}}};
    return s;
}
}
TEST_SUITE(SequenceCoreTests)
TEST_CASE(linear_transform_and_animation_source_mapping) {
    auto s = CompiledSequence::compile(fixture()); CHECK_TRUE(s != nullptr);
    Evaluation out; CHECK_TRUE(s->evaluate(1, out)); CHECK(out.transforms[0].value[0] == 2); CHECK(out.transforms[1].value[1] == 1.5); CHECK(out.animations[0].sourceTime == 3);
}
TEST_CASE(section_boundary_selects_only_next_section_and_final_endpoint) {
    auto s = CompiledSequence::compile(fixture()); Evaluation out;
    CHECK_TRUE(s->evaluate(2, out)); CHECK(out.animations.size() == 1); CHECK(out.animations[0].section == "idle");
    CHECK_TRUE(s->evaluate(4, out)); CHECK(out.animations[0].sourceTime == 2);
}
TEST_CASE(gaps_do_not_hold_old_animation_sections) {
    auto f = fixture(); f.animations[0].sections[0].end = 1; auto s = CompiledSequence::compile(f); Evaluation out;
    CHECK_TRUE(s->evaluate(1.5, out)); CHECK(out.animations.empty());
}
TEST_CASE(quaternion_shortest_arc_is_normalized) {
    auto f = fixture(); f.transforms.push_back({"rotation", "left", Channel::Rotation, {{"r0",0,{0,0,0,1}}, {"r1",4,{0,0,0,-1}}}});
    auto s = CompiledSequence::compile(f); Evaluation out; CHECK_TRUE(s->evaluate(2,out)); CHECK(std::abs(out.transforms.back().value[3] - 1) < 1e-9);
    f.transforms.back().keys.back().value = {0,1,0,0}; s = CompiledSequence::compile(f); CHECK_TRUE(s->evaluate(2,out)); CHECK(std::abs(out.transforms.back().value[1] - std::sqrt(0.5)) < 1e-9);
}
TEST_CASE(invalid_evaluation_preserves_output) {
    auto s = CompiledSequence::compile(fixture()); Evaluation out; CHECK_TRUE(s->evaluate(1,out));
    CHECK_FALSE(s->evaluate(std::numeric_limits<double>::quiet_NaN(),out)); CHECK(out.transforms[0].value[0] == 2); CHECK_FALSE(s->evaluate(5,out));
}
TEST_CASE(immutable_snapshot_survives_source_mutation) {
    auto f = fixture(); auto s = CompiledSequence::compile(f); f.transforms.clear(); Evaluation out; CHECK_TRUE(s->evaluate(1,out)); CHECK(out.transforms.size() == 2);
}
TEST_CASE(ids_bindings_channels_and_times_are_validated) {
    auto f = fixture(); f.transforms[0].keys[0].id = f.id; CHECK_FALSE(validate(f));
    f = fixture(); f.transforms[0].binding = "absent"; CHECK_FALSE(validate(f));
    f = fixture(); f.transforms.push_back(f.transforms.front()); CHECK_FALSE(validate(f));
    f = fixture(); f.transforms[0].keys[1].time = 0; CHECK_FALSE(validate(f));
    f = fixture(); f.transforms[0].channel = static_cast<Channel>(99); CHECK_FALSE(validate(f));
}
TEST_CASE(value_scale_quaternion_and_limits_are_validated) {
    auto f = fixture(); f.transforms[0].keys[0].value[0] = std::numeric_limits<double>::infinity(); CHECK_FALSE(validate(f));
    f = fixture(); f.transforms[1].keys[0].value[0] = 0; CHECK_FALSE(validate(f));
    f = fixture(); f.transforms[0].channel = Channel::Rotation; CHECK_FALSE(validate(f));
    f = fixture(); f.duration = 86401; CHECK_FALSE(validate(f));
    f = fixture(); f.eventTracks[0].events[0].payload.assign(4097,'a'); CHECK_FALSE(validate(f));
}
TEST_CASE(section_paths_ranges_and_overlap_are_validated) {
    for (const auto& path : {"../walk.ayanm", "C:/walk.ayanm", "/walk.ayanm", "clips\\walk.ayanm", "clips//walk.ayanm", "x.ayanim"}) { auto f = fixture(); f.animations[0].sections[0].source = path; CHECK_FALSE(validate(f)); }
    auto f = fixture(); f.animations[0].sections[1].start = 1; CHECK_FALSE(validate(f));
    f = fixture(); f.animations[0].sections[0].speed = -1; CHECK_FALSE(validate(f));
    f = fixture(); f.animations[0].sections[0].sourceDuration = 4; CHECK_FALSE(validate(f));
}
TEST_CASE(once_events_large_tick_and_completion_are_unique) {
    SequencePlayer p; p.bind(CompiledSequence::compile(fixture())); CHECK_TRUE(p.play()); PlaybackFrame out;
    CHECK_TRUE(p.advance(0,out)); CHECK(out.events.empty()); CHECK_TRUE(p.advance(100,out)); CHECK(out.events.size() == 3); CHECK_TRUE(out.completed); CHECK(p.state() == PlaybackState::Completed);
    CHECK_TRUE(p.advance(1,out)); CHECK(out.events.empty()); CHECK_FALSE(out.completed);
}
TEST_CASE(pause_resume_seek_and_restart_event_contract) {
    SequencePlayer p; p.bind(CompiledSequence::compile(fixture())); p.play(); PlaybackFrame out;
    p.advance(1,out); CHECK(out.events.size() == 2); p.pause(); p.advance(1,out); CHECK(p.position() == 1); CHECK(out.events.empty());
    p.play(); p.advance(0.5,out); CHECK(out.events.empty()); CHECK_TRUE(p.seek(0,out)); CHECK(out.events.empty()); p.advance(0.1,out); CHECK(out.events.empty());
    p.stop(); p.play(); p.advance(0.1,out); CHECK(out.events.size() == 1);
}
TEST_CASE(invalid_delta_seek_and_unbound_player_do_not_mutate) {
    SequencePlayer p; PlaybackFrame out; CHECK_FALSE(p.play()); CHECK_FALSE(p.advance(1,out));
    p.bind(CompiledSequence::compile(fixture())); p.play(); p.advance(1,out); CHECK_FALSE(p.advance(-1,out)); CHECK_FALSE(p.seek(8,out)); CHECK(p.position() == 1); CHECK(out.events.size() == 2);
}
TEST_CASE(equal_time_events_keep_track_declaration_order) {
    auto f = fixture(); f.eventTracks.push_back({"events2", {{"ev3",1,"also",""}}}); SequencePlayer p; p.bind(CompiledSequence::compile(f)); p.play(); PlaybackFrame out; p.advance(1,out);
    CHECK(out.events.size() == 3); CHECK(out.events[1].id == "ev1"); CHECK(out.events[2].id == "ev3");
}
TEST_CASE(pause_before_first_positive_tick_retains_unconsumed_origin) {
    SequencePlayer p; p.bind(CompiledSequence::compile(fixture())); p.play(); p.pause(); p.play(); PlaybackFrame out;
    CHECK_TRUE(p.advance(0.1,out)); CHECK(out.events.size() == 1); CHECK(out.events.front().time == 0);
}
TEST_CASE(terminal_seek_restart_and_partial_key_ranges_are_explicit) {
    auto f = fixture(); f.transforms[0].keys[0].time = 1; f.transforms[0].keys[1].time = 3;
    auto s = CompiledSequence::compile(f); Evaluation eval; CHECK_TRUE(s->evaluate(0,eval)); CHECK(eval.transforms.front().value[0] == 0); CHECK_TRUE(s->evaluate(4,eval)); CHECK(eval.transforms.front().value[0] == 8);
    SequencePlayer p; p.bind(s); PlaybackFrame out; CHECK_TRUE(p.seek(4,out)); CHECK(out.events.empty()); p.play(); CHECK(p.position() == 0); p.advance(4,out); CHECK_TRUE(out.completed);
    CHECK_TRUE(p.seek(2,out)); CHECK(p.state() == PlaybackState::Paused); p.play(); p.advance(1,out); CHECK(out.events.empty());
}
TEST_SUITE_END

TEST_SUITE(SequenceAssetTests)
TEST_CASE(json_round_trip_preserves_all_track_types) {
    auto f = fixture(); std::string bytes; CHECK_TRUE(encodeSequence(f,bytes)); Sequence copy; CHECK_TRUE(decodeSequence(bytes,copy)); std::string again; CHECK_TRUE(encodeSequence(copy,again)); CHECK(bytes == again);
}
TEST_CASE(bad_json_version_duplicate_and_unknown_fields_preserve_output) {
    Sequence out = fixture(); Diagnostic e;
    for (const auto& bad : {"{", "{\"id\":1,\"id\":2}", "[]"}) { CHECK_FALSE(decodeSequence(bad,out,&e)); CHECK(out.id == "scene-sequence"); CHECK_FALSE(e.message.empty()); }
    std::string bytes; encodeSequence(fixture(),bytes); auto pos = bytes.find("\"version\": 1"); bytes.replace(pos,12,"\"version\": 2"); CHECK_FALSE(decodeSequence(bytes,out)); CHECK(out.id == "scene-sequence");
    encodeSequence(fixture(),bytes); bytes.insert(1,"\"unknown\": true,"); CHECK_FALSE(decodeSequence(bytes,out));
}
TEST_CASE(depth_and_file_size_limits_are_enforced) {
    Sequence out = fixture(); std::string deep(40,'['); deep += "0"; deep.append(40,']'); CHECK_FALSE(decodeSequence(deep,out));
    std::string huge(16 * 1024 * 1024 + 1,' '); CHECK_FALSE(decodeSequence(huge,out)); CHECK(out.id == "scene-sequence");
}
TEST_CASE(invalid_encode_preserves_previous_bytes) {
    auto f = fixture(); f.duration = -1; std::string bytes = "old"; CHECK_FALSE(encodeSequence(f,bytes)); CHECK(bytes == "old");
}
TEST_CASE(atomic_save_reload_and_invalid_save_keeps_file) {
    ayt::test::ScratchDirectory scratch("sequence");
    const auto path = (scratch.path() / "roundtrip.ayseq").string();
    auto f = fixture(); CHECK_TRUE(saveSequence(path,f)); Sequence loaded; CHECK_TRUE(loadSequence(path,loaded)); CHECK(loaded.id == f.id);
    f.duration = -1; CHECK_FALSE(saveSequence(path,f)); CHECK_TRUE(loadSequence(path,loaded)); CHECK(loaded.duration == 4);
    CHECK_FALSE(saveSequence(path + ".json",fixture())); CHECK_FALSE(loadSequence(path + ".missing",loaded));
}
TEST_SUITE_END

TEST_SUITE(SequenceStressTests)
TEST_CASE(large_keys_seek_without_replaying_events_or_recompiling) {
    Sequence f; f.id = "stress"; f.duration = 100; f.bindings = {{"object"}};
    TransformTrack tr{"track","object",Channel::Position,{}};
    for (int i = 0; i < 100000; ++i) tr.keys.push_back({"key" + std::to_string(i), i / 1000.0, {i / 1000.0,0,0,0}});
    f.transforms.push_back(std::move(tr)); auto s = CompiledSequence::compile(std::move(f)); CHECK_TRUE(s != nullptr);
    SequencePlayer p; p.bind(s); PlaybackFrame out; for (int i = 0; i < 1000; ++i) { CHECK_TRUE(p.seek(i / 10.0,out)); CHECK(out.events.empty()); CHECK(p.sequence().get() == s.get()); }
    CHECK(std::abs(out.evaluation.transforms[0].value[0] - 99.9) < 1e-9);
}
TEST_SUITE_END
