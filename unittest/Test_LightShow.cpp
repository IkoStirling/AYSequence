#include <AYSequence/LightShow.h>
#include <AYSequence/LightShowAuthoring.h>
#include <AYSequence/LightShowAssetIO.h>
#include <AYTest.h>
#include <AYTestFixtures.h>
#include <algorithm>
#include <cmath>
#include <fstream>
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
TEST_SUITE(LightShowAuthoringTests)
TEST_CASE(move_copy_remove_and_interpolation_are_atomic_and_keep_stable_ids) {
    auto data=showFixture();const std::array<uint32_t,1> ids{7};LightShow edited;std::string error;
    CHECK(editLightShowKeys(data,ids,true,4,3,LightShowKeyEdit::Move,ShowInterpolation::Linear,edited,&error));
    CHECK(data.fixtures[0].keys[1].time==4);CHECK(edited.fixtures[0].keys[1].time==3);CHECK(edited.formations[1].time==3);
    CHECK(edited.formations[1].points[0].id==42);CHECK(edited.fixtures[0].id==7);
    CHECK(editLightShowKeys(edited,ids,true,3,2,LightShowKeyEdit::Copy,ShowInterpolation::Linear,edited));
    CHECK(edited.fixtures[0].keys.size()==3);CHECK(edited.formations.size()==3);
    CHECK(editLightShowKeys(edited,ids,true,2,2,LightShowKeyEdit::Interpolation,ShowInterpolation::Step,edited));
    CHECK(edited.fixtures[0].keys[1].interpolation==ShowInterpolation::Step);CHECK(edited.formations[1].interpolation==ShowInterpolation::Step);
    CHECK(editLightShowKeys(edited,ids,true,2,2,LightShowKeyEdit::Remove,ShowInterpolation::Linear,edited));
    CHECK(edited.fixtures[0].keys.size()==2);CHECK(edited.formations.size()==2);
    CHECK(!editLightShowKeys(edited,ids,true,3,0,LightShowKeyEdit::Move,ShowInterpolation::Linear,edited,&error));
    CHECK(!error.empty());CHECK(edited.formations[1].time==3);
}
TEST_CASE(key_edit_rejects_last_key_missing_targets_invalid_times_and_partial_collisions) {
    auto data=showFixture();data.fixtures.push_back(data.fixtures[0]);data.fixtures[1].id=8;
    data.fixtures[1].keys[1].time=3;const std::array<uint32_t,2> ids{7,8};LightShow output=showFixture();
    CHECK(!editLightShowKeys(data,ids,false,0,3,LightShowKeyEdit::Move,ShowInterpolation::Linear,output));
    CHECK(output.fixtures.size()==1);CHECK(output.fixtures[0].keys[0].time==0);
    CHECK(!editLightShowKeys(data,ids,true,0,5,LightShowKeyEdit::Copy,ShowInterpolation::Linear,output));
    CHECK(!editLightShowKeys(data,ids,true,0,std::numeric_limits<double>::quiet_NaN(),LightShowKeyEdit::Move,ShowInterpolation::Linear,output));
    CHECK(!editLightShowKeys(data,std::array<uint32_t,1>{99},false,0,2,LightShowKeyEdit::Copy,ShowInterpolation::Linear,output));
    CHECK(!editLightShowKeys(data,std::array<uint32_t,2>{7,7},false,0,2,LightShowKeyEdit::Copy,ShowInterpolation::Linear,output));
    CHECK(!editLightShowKeys(data,ids,true,1,2,LightShowKeyEdit::Move,ShowInterpolation::Linear,output));
    data.fixtures[0].keys.resize(1);
    CHECK(!editLightShowKeys(data,ids,false,0,0,LightShowKeyEdit::Remove,ShowInterpolation::Linear,output));
    CHECK(output.fixtures[0].keys.size()==2);
}
TEST_SUITE_END

TEST_SUITE(LightShowCoreTests)
TEST_CASE(aperture_and_two_cone_angles_interpolate_and_reject_invalid_optics) {
    auto data=showFixture();auto& keys=data.fixtures[0].keys;
    keys[0].value.apertureRadius=.02f;keys[1].value.apertureRadius=.1f;
    keys[0].value.innerAngle=.02f;keys[1].value.innerAngle=.04f;
    keys[0].value.outerAngle=.04f;keys[1].value.outerAngle=.08f;
    auto show=CompiledLightShow::compile(data);CHECK(show!=nullptr);LightShowFrame frame;
    CHECK(show->evaluate(2,frame));const auto& value=frame.fixtures[0].value;
    CHECK(std::abs(value.apertureRadius-.06f)<1e-6f);
    CHECK(std::abs(value.innerAngle-.03f)<1e-6f);CHECK(std::abs(value.outerAngle-.06f)<1e-6f);
    keys[0].value.apertureRadius=-1;CHECK(!CompiledLightShow::compile(data));
    keys[0].value.apertureRadius=std::numeric_limits<float>::quiet_NaN();CHECK(!CompiledLightShow::compile(data));
    keys[0].value.apertureRadius=1.01f;CHECK(!CompiledLightShow::compile(data));
    keys[0].value.apertureRadius=.02f;keys[0].value.goboFocus=.25f;keys[1].value.goboFocus=.75f;
    keys[0].value.iris=.2f;keys[1].value.iris=.8f;
    show=CompiledLightShow::compile(data);CHECK(show!=nullptr);CHECK(show->evaluate(2,frame));
    CHECK(std::abs(frame.fixtures[0].value.goboFocus-.5f)<1e-6f);
    CHECK(std::abs(frame.fixtures[0].value.iris-.5f)<1e-6f);
    keys[0].value.iris=0;CHECK(!CompiledLightShow::compile(data));
    keys[0].value.iris=.2f;keys[0].value.goboFocus=1.1f;CHECK(!CompiledLightShow::compile(data));
}
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
TEST_CASE(formation_clearance_detects_between_frame_crossings_and_bounds_work) {
    auto data=showFixture();
    for(auto& key:data.formations)key.points.clear();
    ShowPoint a,b;a.id=1;b.id=2;
    a.position={-2,0,0};b.position={2,0,0};
    data.formations[0].points={a,b};
    a.position={2,0,0};b.position={-2,0,0};
    data.formations[1].points={a,b};
    auto show=CompiledLightShow::compile(data);CHECK(show!=nullptr);
    FormationClearanceReport report;
    CHECK(show->analyzeFormationClearance(.5f,100,10,report));
    CHECK(report.complete);CHECK(report.conflicts.size()==1);
    CHECK(report.conflicts[0].firstId==1);CHECK(report.conflicts[0].secondId==2);
    CHECK(std::abs(report.conflicts[0].time-2)<1e-6);CHECK(report.conflicts[0].distance==0);
    data.formations[0].interpolation=ShowInterpolation::Smooth;
    show=CompiledLightShow::compile(data);
    CHECK(show->analyzeFormationClearance(.5f,100,10,report));
    CHECK(report.conflicts.size()==1);CHECK(std::abs(report.conflicts[0].time-2)<1e-6);
    CHECK(show->analyzeFormationClearance(.5f,100,1,report));CHECK(!report.complete);
    const auto previous=report.checkedPairs;
    CHECK(!show->analyzeFormationClearance(-1,100,10,report));CHECK(report.checkedPairs==previous);
    auto bounded=data;
    ShowPoint c;c.id=3;c.position={0,0,0};
    for(auto& key:bounded.formations)key.points.push_back(c);
    auto crowded=CompiledLightShow::compile(bounded);CHECK(crowded!=nullptr);
    CHECK(crowded->analyzeFormationClearance(.5f,1,10,report));
    CHECK(!report.complete);CHECK(report.checkedPairs==1);
    data.formations[0].interpolation=ShowInterpolation::Step;
    show=CompiledLightShow::compile(data);
    CHECK(show->analyzeFormationClearance(.5f,100,10,report));
    CHECK(report.conflicts.empty());
}
TEST_CASE(formation_route_planner_detours_crossings_and_preserves_output_on_failure) {
    auto crossing=showFixture();
    ShowPoint a,b;a.id=1;b.id=2;
    a.position={-2,0,0};b.position={2,0,0};
    crossing.formations[0].points={a,b};
    a.position={2,0,0};b.position={-2,0,0};
    crossing.formations[1].points={a,b};
    LightShow planned;std::string error;
    CHECK(planFormationTransition(crossing,0,{},planned,&error));CHECK(error.empty());
    CHECK(planned.formations.size()==3);CHECK(planned.formations[1].time==2);
    CHECK(planned.formations[1].points[0].id==1);
    auto compiled=CompiledLightShow::compile(planned);CHECK(compiled!=nullptr);
    FormationClearanceReport report;
    CHECK(compiled->analyzeFormationClearance(.5f,100,10,report));
    CHECK(report.complete);CHECK(report.conflicts.empty());
    LightShow same;CHECK(planFormationTransition(planned,0,{},same,&error));
    CHECK(same.formations.size()==planned.formations.size());

    const auto previous=planned.formations.size();
    FormationRouteOptions tooLow;tooLow.maxVerticalDetour=.1f;
    CHECK(!planFormationTransition(crossing,0,tooLow,planned,&error));
    CHECK(planned.formations.size()==previous);CHECK(!error.empty());
    CHECK(!planFormationTransition(crossing,3,{},planned,&error));
    CHECK(planned.formations.size()==previous);
    CHECK(!planFormationTransition(crossing,std::numeric_limits<std::size_t>::max(),{},planned,&error));
    CHECK(planned.formations.size()==previous);
    crossing.formations[0].points[1].position=crossing.formations[0].points[0].position;
    CHECK(!planFormationTransition(crossing,0,{},planned,&error));
    CHECK(planned.formations.size()==previous);
    crossing.formations[0].interpolation=ShowInterpolation::Step;
    CHECK(!planFormationTransition(crossing,0,{},planned,&error));
    CHECK(planned.formations.size()==previous);
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
TEST_CASE(external_clock_owns_time_without_accumulated_frame_drift) {
    auto data=showFixture();data.loop=true;LightShowPlayer player;
    player.bind(CompiledLightShow::compile(data));player.play();
    CHECK(player.synchronize(10.25));CHECK(std::abs(player.time()-2.25)<1e-9);
    CHECK(player.synchronize(10.25));CHECK(std::abs(player.time()-2.25)<1e-9);
    player.pause();CHECK(player.synchronize(12));CHECK(std::abs(player.time()-2.25)<1e-9);
    CHECK(!player.synchronize(-1));CHECK(std::abs(player.time()-2.25)<1e-9);
    data.loop=false;player.bind(CompiledLightShow::compile(data));player.play();
    CHECK(player.synchronize(8));CHECK(player.time()==4);CHECK(!player.playing());
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
    bytes.replace(angle,end-angle,"\"innerAngle\": 0.01");CHECK(decodeLightShow(bytes,copy,&error));
    CHECK(std::abs(copy.fixtures[0].keys[0].value.apertureRadius-.035f)<1e-6f);
}
TEST_CASE(schema1_through_3_migrate_optics_and_texture_path) {
    std::string bytes,error;CHECK(encodeLightShow(showFixture(),bytes));LightShow output;
    CHECK(bytes.find("\"version\": 4")!=std::string::npos);
    const auto removeField=[](std::string& document,const char* field){
        size_t at=0;
        while((at=document.find(field,at))!=std::string::npos){
            const auto end=document.find(',',at);document.erase(at,end+1-at);
        }
    };
    auto legacy=bytes;legacy.replace(legacy.find("\"version\": 4"),12,"\"version\": 3");
    CHECK(!decodeLightShow(legacy,output,&error));
    removeField(legacy,"\"goboTexturePath\"");removeField(legacy,"\"shadowSoftness\"");
    CHECK(decodeLightShow(legacy,output,&error));
    CHECK(output.fixtures[0].goboTexturePath.empty());
    CHECK(output.fixtures[0].keys[0].value.shadowSoftness==0);
    legacy.replace(legacy.find("\"version\": 3"),12,"\"version\": 2");
    CHECK(!decodeLightShow(legacy,output,&error)); // v3 optics are not legal in v2.
    removeField(legacy,"\"goboFocus\"");removeField(legacy,"\"iris\"");
    CHECK(decodeLightShow(legacy,output,&error));
    CHECK(output.fixtures[0].keys[0].value.goboFocus==0);
    CHECK(output.fixtures[0].keys[0].value.iris==1);
    legacy.replace(legacy.find("\"version\": 2"),12,"\"version\": 1");
    CHECK(!decodeLightShow(legacy,output,&error)); // v2 aperture is not legal in v1.
    removeField(legacy,"\"apertureRadius\"");CHECK(decodeLightShow(legacy,output,&error));
    for(const auto& key:output.fixtures[0].keys)CHECK(key.value.apertureRadius==0);
    CHECK(encodeLightShow(output,legacy));CHECK(legacy.find("\"version\": 4")!=std::string::npos);
    removeField(bytes,"\"apertureRadius\"");CHECK(!decodeLightShow(bytes,output,&error));
    CHECK(output.fixtures[0].keys[0].value.apertureRadius==0); // failed decode preserves output.
}
TEST_CASE(texture_gobo_requires_a_relative_fixture_path_and_roundtrips) {
    auto data=showFixture();auto& track=data.fixtures[0];track.keys[0].value.gobo=ShowGobo::Texture;
    CHECK(!CompiledLightShow::compile(data));
    track.goboTexturePath="gobos/star.png";
    CHECK(CompiledLightShow::compile(data)!=nullptr);
    std::string bytes,error;CHECK(encodeLightShow(data,bytes,&error));
    LightShow copy;CHECK(decodeLightShow(bytes,copy,&error));
    CHECK(copy.fixtures[0].goboTexturePath=="gobos/star.png");
    CHECK(copy.fixtures[0].keys[0].value.gobo==ShowGobo::Texture);
    track.goboTexturePath="../escape.png";CHECK(!CompiledLightShow::compile(data));
    track.goboTexturePath="C:/absolute.png";CHECK(!CompiledLightShow::compile(data));
    auto missing=bytes;const auto at=missing.find("\"goboTexturePath\"");
    missing.erase(at,missing.find(',',at)-at+1);
    CHECK(!decodeLightShow(missing,copy,&error));
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
TEST_CASE(bundle_copies_referenced_images_and_music_for_reopening) {
    ayt::test::ScratchDirectory scratch("light-show-bundle");
    const auto root=scratch.path(),showPath=root/"show.lshow",music=root/"song.ogg",bundle=root/"export";
    std::filesystem::create_directory(root/"gobos");
    {std::ofstream image(root/"gobos"/"star.png",std::ios::binary);image<<"image";}
    {std::ofstream audio(music,std::ios::binary);audio<<"music";}
    auto show=showFixture();show.fixtures[0].goboTexturePath="gobos/star.png";
    show.fixtures[0].keys[0].value.gobo=ShowGobo::Texture;
    CHECK(saveLightShow(showPath.string(),show));
    std::string error,packagedMusic="unchanged";
    CHECK(exportLightShowBundle(showPath.string(),bundle.string(),music.string(),&error));
    CHECK(error.empty());CHECK(std::filesystem::exists(bundle/"show.lshow"));
    CHECK(std::filesystem::exists(bundle/"gobos"/"star.png"));
    LightShow reopened;CHECK(loadLightShow((bundle/"show.lshow").string(),reopened));
    CHECK(bundledLightShowMusic((bundle/"show.lshow").string(),packagedMusic,&error));
    CHECK(std::filesystem::path(packagedMusic)==std::filesystem::canonical(bundle/"__music"/"track.ogg"));
    CHECK(!exportLightShowBundle(showPath.string(),bundle.string(),music.string(),&error));
    CHECK(std::filesystem::exists(bundle/"show.lshow"));
}
TEST_CASE(bundle_rejects_missing_assets_without_publishing) {
    ayt::test::ScratchDirectory scratch("light-show-bundle-missing");
    const auto root=scratch.path(),showPath=root/"show.lshow",bundle=root/"export";
    auto show=showFixture();show.fixtures[0].goboTexturePath="missing.png";
    show.fixtures[0].keys[0].value.gobo=ShowGobo::Texture;
    CHECK(saveLightShow(showPath.string(),show));std::string error,music="keep";
    CHECK(!exportLightShowBundle(showPath.string(),bundle.string(),{},&error));
    CHECK(!std::filesystem::exists(bundle));
    CHECK(bundledLightShowMusic(showPath.string(),music,&error));CHECK(music.empty());
}
TEST_SUITE_END
