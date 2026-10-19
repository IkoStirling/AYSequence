#include <AYSequence/LightShow.h>
#include <AYTest.h>
#include <AYSequence/LightShowAuthoring.h>
#include <AYSequence/LightShowAssetIO.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
using namespace ayt::sequence;
TEST_SUITE(LightShowFogTests)
TEST_CASE(physical_fog_track_interpolates_at_absolute_time_and_shares_immutable_voxels) {
 LightShow data;data.duration=4;data.loop=false;ShowFogKey first;first.value.density=.1f;first.value.local=true;ShowFogKey last=first;last.time=2;last.value.density=.3f;last.value.center={2,4,6};last.value.enabled=false;
 data.fog.keys={first,last};data.fog.grid={2,2,2,{0,32,64,96,128,160,192,255}};
 auto compiled=CompiledLightShow::compile(data);CHECK(bool(compiled));if(!compiled)return;
 data.fog.grid.values[0]=255;LightShowFrame frame;CHECK(compiled->evaluate(1,frame));CHECK(frame.fog.authored);CHECK(frame.fog.value.enabled);CHECK(std::abs(frame.fog.value.density-.2f)<1e-6f);CHECK(frame.fog.value.center[2]==3);CHECK(frame.fog.grid);CHECK(frame.fog.grid->values[0]==0);
 const auto saved=frame.fog.grid;CHECK(compiled->evaluate(2,frame));CHECK(!frame.fog.value.enabled);CHECK(frame.fog.grid.get()==saved.get());CHECK(compiled->evaluate(0,frame));CHECK(frame.fog.value.density==.1f);
 LightShow legacy;legacy.fixtures.push_back({1,"",{{0,{}}}});auto original=CompiledLightShow::compile(legacy);CHECK(bool(original));CHECK(original->evaluate(0,frame));CHECK(!frame.fog.authored);CHECK(!frame.fog.grid);CHECK(saved->values[7]==255);
}
TEST_CASE(fog_validation_bounds_grid_time_and_physical_values_without_renderer_dependency) {
 LightShow data;data.duration=4;data.fog.keys.push_back({});CHECK(bool(CompiledLightShow::compile(data)));
 auto invalid=data;invalid.fog.keys[0].value.density=-1;CHECK(!CompiledLightShow::compile(invalid));invalid=data;invalid.fog.keys[0].value.noiseScale=std::numeric_limits<float>::infinity();CHECK(!CompiledLightShow::compile(invalid));invalid=data;invalid.fog.keys[0].time=5;CHECK(!CompiledLightShow::compile(invalid));invalid=data;invalid.fog.keys.push_back({});CHECK(!CompiledLightShow::compile(invalid));
 invalid=data;invalid.fog.grid={1,1,1,{255}};CHECK(!CompiledLightShow::compile(invalid));invalid.fog.keys[0].value.local=true;CHECK(bool(CompiledLightShow::compile(invalid)));invalid.fog.grid.width=65;CHECK(!CompiledLightShow::compile(invalid));invalid=data;invalid.fog.grid.width=1;CHECK(!CompiledLightShow::compile(invalid));invalid=data;invalid.fog.keys.resize(4097);CHECK(!CompiledLightShow::compile(invalid));
}
TEST_CASE(fog_interpolation_uses_stepped_flags_and_declared_curve) {
 LightShow data;data.duration=4;ShowFogKey a;a.value.density=.1f;a.interpolation=ShowInterpolation::Smooth;ShowFogKey b=a;b.time=2;b.value.density=.3f;b.value.local=true;data.fog.keys={a,b};
 auto compiled=CompiledLightShow::compile(data);CHECK(bool(compiled));LightShowFrame frame;CHECK(compiled->evaluate(.5,frame));CHECK(std::abs(frame.fog.value.density-.13125f)<1e-6f);CHECK(!frame.fog.value.local);CHECK(compiled->evaluate(2,frame));CHECK(frame.fog.value.local);
 data.fog.keys[0].interpolation=ShowInterpolation::Step;compiled=CompiledLightShow::compile(data);CHECK(compiled->evaluate(1,frame));CHECK(frame.fog.value.density==.1f);
}
TEST_SUITE_END

TEST_SUITE(LightShowFogAssetTests)
TEST_CASE(schema8_roundtrips_bounded_fog_and_rejects_lossy_or_excessive_voxels_transactionally) {
 LightShow data;data.name="Fog";data.duration=4;ShowFogKey key;key.value.local=true;data.fog.keys={key};data.fog.grid={2,2,2,{0,32,64,96,128,160,192,255}};
 std::string bytes,error;CHECK(encodeLightShow(data,bytes,&error));LightShow result;CHECK(decodeLightShow(bytes,result,&error));CHECK(result.fog.grid.values==data.fog.grid.values);CHECK(result.fog.keys.size()==1);
 std::string again;CHECK(encodeLightShow(result,again,&error));CHECK(bytes==again);
 const auto original=nlohmann::json::parse(bytes);auto invalid=original;invalid["show"]["fog"]["grid"]["values"][0]=256;CHECK(!decodeLightShow(invalid.dump(),result,&error));CHECK(result.name=="Fog");CHECK(result.fog.grid.values[0]==0);
 invalid=original;invalid["show"]["fog"]["grid"]["values"][0]=255.0;CHECK(!decodeLightShow(invalid.dump(),result,&error));
 invalid=original;invalid["show"]["fog"]["grid"]["width"]=65;CHECK(!decodeLightShow(invalid.dump(),result,&error));
 invalid=original;invalid["show"]["fog"]["keys"]=std::vector<nlohmann::json>(4097,original["show"]["fog"]["keys"][0]);CHECK(!decodeLightShow(invalid.dump(),result,&error));
 invalid=original;invalid["show"]["fog"]["grid"]["values"]=std::vector<unsigned>(262145,255);CHECK(!decodeLightShow(invalid.dump(),result,&error));CHECK(error=="Fog collection budget exceeded");
 invalid=original;invalid["version"]=9;CHECK(!decodeLightShow(invalid.dump(),result,&error));CHECK(result.fog.grid.values.size()==8);
}
TEST_SUITE_END

TEST_SUITE(LightShowFogAuthoringTests)
TEST_CASE(fog_key_edits_and_density_strokes_are_atomic_alias_safe_and_preserve_other_tracks) {
 LightShow data;data.duration=4;data.name="Fog";ShowFogKey key;key.value.local=true;data.fog.keys={key};data.fog.grid={5,5,5,std::vector<uint8_t>(125,255)};
 LightShow edited;CHECK(editLightShowKeys(data,{},false,0,2,LightShowKeyEdit::Copy,ShowInterpolation::Linear,edited,nullptr,true));CHECK(edited.fog.keys.size()==2);CHECK(edited.fog.grid.values==data.fog.grid.values);
 CHECK(editLightShowKeys(edited,{},false,2,3,LightShowKeyEdit::Move,ShowInterpolation::Linear,edited,nullptr,true));CHECK(edited.fog.keys.back().time==3);
 const auto before=edited.fog.grid.values;CHECK(!editLightShowKeys(edited,{},false,3,0,LightShowKeyEdit::Copy,ShowInterpolation::Linear,edited,nullptr,true));CHECK(edited.fog.keys.back().time==3);
 CHECK(!editLightShowKeys(data,{},false,0,0,LightShowKeyEdit::Remove,ShowInterpolation::Linear,edited,nullptr,true));CHECK(edited.fog.keys.size()==2);
 FogDensityBrush brush;brush.center={2,2,2};brush.radius=2;brush.value=0;CHECK(paintLightShowFogDensity(data,brush,edited));CHECK(edited.fog.grid.values[2+5*(2+5*2)]==0);CHECK(edited.fog.grid.values[3+5*(2+5*2)]==128);CHECK(edited.fog.grid.values[2+5*(2+5*3)]==255);CHECK(data.fog.grid.values==before);
 brush.volume=true;CHECK(paintLightShowFogDensity(data,brush,edited));CHECK(edited.fog.grid.values[2+5*(2+5*3)]==128);brush.value=255;CHECK(paintLightShowFogDensity(edited,brush,edited));CHECK(edited.fog.grid.values[2+5*(2+5*2)]==255);
 ShowFogGrid gridPreview=data.fog.grid;CHECK(paintFogDensityGrid(gridPreview,brush,gridPreview));CHECK(gridPreview.values[2+5*(2+5*2)]==255);
 brush.center[0]=5;CHECK(!paintLightShowFogDensity(data,brush,edited));CHECK(edited.fog.grid.values[2+5*(2+5*2)]==255);
 brush.center[0]=2;brush.strength=std::numeric_limits<float>::quiet_NaN();CHECK(!paintLightShowFogDensity(data,brush,edited));
}
TEST_SUITE_END
