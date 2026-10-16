#include <AYSequence/LightShow.h>
#include <AYSequence/LightShowAssetIO.h>
#include <AYTest.h>
#include <AYTestFixtures.h>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace ayt::sequence;
namespace {
LightShow showFixture() {
    LightShow data; data.duration=4; data.loop=false;
    FixtureState a,b; b.position={4,9,2}; b.intensity=10; b.color={0,1,0}; b.gobo=ShowGobo::Ring;
    data.fixtures={{7,"rig",{{0,a},{4,b}}}};
    ShowPoint p,q; p.id=q.id=42; q.position={4,8,12}; q.color={0,1,0}; q.intensity=8;
    data.formations={{0,{p},ShowInterpolation::Linear},{4,{q}}};
    return data;
}
}
TEST_SUITE(LightShowCoreTests)
TEST_CASE(absolute_time_interpolates_fixture_parameters_and_stable_point_ids) {
    auto show=CompiledLightShow::compile(showFixture()); CHECK(show!=nullptr);
    LightShowFrame frame; CHECK(show->evaluate(2,frame));
    CHECK(frame.fixtures[0].id==7); CHECK(frame.fixtures[0].value.position[0]==2);
    CHECK(frame.fixtures[0].value.intensity==20); CHECK(frame.fixtures[0].value.color[0]==.5f);
    CHECK(frame.fixtures[0].value.gobo==ShowGobo::Open);
    CHECK(frame.points[0].id==42); CHECK(frame.points[0].position[2]==6); CHECK(frame.points[0].intensity==6);
    CHECK(show->evaluate(4,frame)); CHECK(frame.fixtures[0].value.gobo==ShowGobo::Ring);
}
TEST_CASE(point_identity_is_canonical_and_never_interpolates_between_different_ids) {
    auto data=showFixture();
    for(auto& k:data.formations) { auto p=k.points[0];p.id=8;p.position[0]=80;k.points.push_back(p); }
    std::reverse(data.formations[1].points.begin(),data.formations[1].points.end());
    auto show=CompiledLightShow::compile(data); CHECK(show!=nullptr); LightShowFrame frame;
    CHECK(show->evaluate(2,frame)); CHECK(frame.points[0].id==8); CHECK(frame.points[0].position[0]==80);
    data.formations[1].points[0].id=99; CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.formations[0].points.push_back(data.formations[0].points[0]); CHECK(!CompiledLightShow::compile(data));
}
TEST_CASE(invalid_input_and_evaluation_preserve_existing_state) {
    auto data=showFixture();auto show=CompiledLightShow::compile(data);LightShowFrame frame;
    show->evaluate(2,frame);CHECK(!show->evaluate(-1,frame));CHECK(frame.time==2);
    CHECK(!show->evaluate(std::numeric_limits<double>::quiet_NaN(),frame));CHECK(frame.time==2);
    data.fixtures[0].keys[0].value.outerAngle=.01f;CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.fixtures[0].keys[0].value.innerAngle=.2f;
    data.fixtures[0].keys[0].value.outerAngle=std::nextafter(.2f,1.f);CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.fixtures[0].keys[0].value.color[0]=-1;CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.formations[0].points[0].radius=0;CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.fixtures[0].keys[1].time=0;CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.fixtures[0].keys[0].value.gobo=static_cast<ShowGobo>(99);CHECK(!CompiledLightShow::compile(data));
}
TEST_CASE(sweep_and_strobe_depend_on_absolute_show_time) {
    auto data=showFixture();auto& track=data.fixtures[0];track.sweepPan=1;track.sweepFrequency=1;track.strobeFrequency=2;
    auto show=CompiledLightShow::compile(data);LightShowFrame a,b;show->evaluate(.25,a);show->evaluate(.25,b);
    CHECK(std::abs(a.fixtures[0].value.pan-1)<1e-5f);CHECK(a.fixtures[0].value.intensity==0);
    CHECK(a.fixtures[0].value.pan==b.fixtures[0].value.pan);
    show->evaluate(.25,b,false);CHECK(b.fixtures[0].value.pan==0);CHECK(b.fixtures[0].value.intensity>0);
    show->evaluate(0,a);CHECK(a.fixtures[0].value.intensity==30);
}
TEST_CASE(player_pause_seek_loop_and_once_hold_endpoint) {
    LightShowPlayer player;CHECK(!player.advance(1));player.bind(CompiledLightShow::compile(showFixture()));
    player.play();CHECK(player.advance(1));player.pause();CHECK(player.advance(2));CHECK(player.time()==1);
    CHECK(player.seek(2));CHECK(!player.seek(5));CHECK(player.time()==2);
    player.play();CHECK(player.advance(100));CHECK(player.time()==4);CHECK(!player.playing());
    auto data=showFixture();data.loop=true;player.bind(CompiledLightShow::compile(data));player.play();
    CHECK(player.advance(9));CHECK(player.time()==1);CHECK(player.advance(std::numeric_limits<double>::max()));
    CHECK(std::isfinite(player.time()));CHECK(player.time()<4);
    CHECK(!player.advance(-1));player.stop();CHECK(player.time()==0);
}
TEST_CASE(step_smooth_and_pre_first_key_hold_are_explicit) {
    auto data=showFixture();data.fixtures[0].keys[0].time=1;
    data.fixtures[0].keys[0].interpolation=ShowInterpolation::Step;
    data.formations[0].interpolation=ShowInterpolation::Smooth;
    auto show=CompiledLightShow::compile(data);LightShowFrame frame;show->evaluate(0,frame);
    CHECK(frame.fixtures[0].value.position[0]==0);show->evaluate(1,frame);
    CHECK(frame.points[0].position[0]==.625f);show->evaluate(3,frame);
    CHECK(frame.fixtures[0].value.position[0]==0);
}
TEST_CASE(example_scales_to_ten_thousand_points_and_reuses_frame_storage) {
    auto data=makeLightShowExample(32,10000);std::string error;
    auto show=CompiledLightShow::compile(data,&error);CHECK(show!=nullptr);CHECK(error.empty());
    LightShowFrame frame;CHECK(show->evaluate(0,frame));auto* points=frame.points.data();
    for(int i=0;i<120;++i)CHECK(show->evaluate(i/60.,frame));
    CHECK(frame.points.data()==points);CHECK(frame.points.size()==10000);CHECK(frame.fixtures.size()==32);
    data.fixtures.clear();data.formations.clear();CHECK(!CompiledLightShow::compile(data,&error));CHECK(!error.empty());
}
TEST_CASE(validation_enforces_ids_times_nonfinite_and_collection_budgets) {
    auto data=showFixture();data.fixtures.push_back(data.fixtures[0]);CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.fixtures[0].sweepFrequency=std::numeric_limits<float>::infinity();CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.formations[1].time=0;CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.formations[1].points.clear();CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.formations[0].points[0].position[2]=std::numeric_limits<float>::quiet_NaN();CHECK(!CompiledLightShow::compile(data));
    data=showFixture();data.duration=86401;CHECK(!CompiledLightShow::compile(data));
    data=makeLightShowExample(256,0);data.fixtures.push_back(data.fixtures[0]);CHECK(!CompiledLightShow::compile(data));
}
TEST_SUITE_END

TEST_SUITE(LightShowAssetTests)
TEST_CASE(roundtrip_preserves_lights_formations_patterns_and_absolute_time) {
    auto data=makeLightShowExample(16,1000);std::string bytes,error;
    CHECK(encodeLightShow(data,bytes,&error));LightShow copy;CHECK(decodeLightShow(bytes,copy,&error));
    std::string again;CHECK(encodeLightShow(copy,again));CHECK(bytes==again);
    CHECK(copy.fixtures.size()==16);CHECK(copy.formations[1].points.size()==1000);
    const auto angle=bytes.find("\"innerAngle\":");const auto end=bytes.find(',',angle);
    bytes.replace(angle,end-angle,"\"innerAngle\": 0.1");CHECK(decodeLightShow(bytes,copy,&error));
}
TEST_CASE(rejects_unknown_fields_duplicate_keys_and_lossy_enum_conversions) {
    std::string bytes;CHECK(encodeLightShow(showFixture(),bytes));LightShow output=showFixture();
    auto bad=bytes;bad.insert(bad.find('{')+1,"\"unknown\":1,");CHECK(!decodeLightShow(bad,output));CHECK(output.duration==4);
    bad=bytes;const auto pos=bad.find("\"version\"");bad.insert(pos,"\"version\":1,");CHECK(!decodeLightShow(bad,output));
    bad=bytes;const auto gobo=bad.find("\"gobo\": 0");bad.replace(gobo,9,"\"gobo\": 256");CHECK(!decodeLightShow(bad,output));
    CHECK(!decodeLightShow("{}",output));CHECK(!decodeLightShow("[]",output));CHECK(output.duration==4);
}
TEST_CASE(atomic_file_publication_preserves_previous_file_on_invalid_edit) {
    ayt::test::ScratchDirectory scratch("light-show");const auto path=(scratch.path()/"show.lshow").string();
    auto data=showFixture();CHECK(saveLightShow(path,data));LightShow copy;CHECK(loadLightShow(path,copy));
    data.duration=-1;CHECK(!saveLightShow(path,data));CHECK(loadLightShow(path,copy));CHECK(copy.duration==4);
    CHECK(!saveLightShow(path+".json",showFixture()));CHECK(!loadLightShow(path+".missing",copy));
    std::string bytes="old";CHECK(!encodeLightShow(data,bytes));CHECK(bytes=="old");
}
TEST_SUITE_END
