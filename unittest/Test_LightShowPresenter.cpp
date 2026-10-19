#include <AYSequence/LightShowPresenter.h>
#include <AYRenderer.h>
#include <AYTest.h>
#include <cmath>
using namespace ayt;
TEST_SUITE(LightShowPresenterTests)
TEST_CASE(profiling_defaults_off_reports_completed_and_clears_after_failure_or_disable) {
 render::Renderer renderer;render::InitDesc init;init.backend=render::Backend::Noop;CHECK(renderer.initialize(init));
 sequence::LightShowPresenter presenter;sequence::LightShowFrame frame;frame.fixtures.push_back({1,{}});frame.points.push_back({1,{0,0,0},{1,0,0},1,.05f});
 const auto identity=math::Float4x4::identity();render::RenderScene scene;std::string error;
 CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{}, {},&error));CHECK(!presenter.frameStats().enabled);CHECK(presenter.frameStats().totalMs==0);
 presenter.setProfilingEnabled(true);scene.clear();CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{}, {},&error));
 const auto snapshot=presenter.frameStats();CHECK(snapshot.enabled);CHECK(snapshot.completed);CHECK(std::isfinite(snapshot.totalMs));CHECK(snapshot.totalMs>0);
 const double sum=snapshot.lightsMs+snapshot.pointDataMs+snapshot.aggregationMs+snapshot.packetMs+snapshot.fogMs+snapshot.housingsMs+snapshot.pointsMs+snapshot.emittersMs;CHECK(sum>=0);CHECK(snapshot.totalMs+1e-6>=sum);
 frame.fixtures[0].value.gobo=sequence::ShowGobo::Texture;CHECK(!presenter.append(renderer,scene,frame,identity,identity,64,64,{}, {},&error));CHECK(presenter.frameStats().enabled);CHECK(!presenter.frameStats().completed);CHECK(presenter.frameStats().pointsMs==0);CHECK(snapshot.completed);
 presenter.setProfilingEnabled(false);CHECK(!presenter.frameStats().enabled);CHECK(presenter.frameStats().totalMs==0);renderer.shutdown();
}
TEST_CASE(authored_fog_preserves_host_quality_reuses_atlas_and_leaves_legacy_medium_control) {
 render::Renderer renderer;render::InitDesc init;init.backend=render::Backend::Noop;CHECK(renderer.initialize(init));
 auto settings=renderer.volumetricSettings();settings.steps=24;settings.downsample=4;settings.froxel=true;settings.froxelHistory=true;renderer.setVolumetricSettings(settings);
 sequence::LightShowPresenter presenter;sequence::LightShowFrame frame;frame.time=2;frame.fog.authored=true;frame.fog.value.local=true;frame.fog.value.density=.2f;frame.fog.value.tint={1,.5f,0};frame.fog.grid=std::make_shared<const sequence::ShowFogGrid>(sequence::ShowFogGrid{2,2,2,{0,32,64,96,128,160,192,255}});
 const auto identity=math::Float4x4::identity();render::RenderScene scene;CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));settings=renderer.volumetricSettings();CHECK(settings.density==.2f);CHECK(settings.noiseTime==2);CHECK(settings.steps==24);CHECK(settings.downsample==4);CHECK(settings.froxel);CHECK(settings.froxelHistory);CHECK(renderer.volumetricDensityField().profile);
 const auto texture=renderer.volumetricDensityField().texture.id;CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));CHECK(renderer.volumetricDensityField().texture.id==texture);
 frame.fog.grid=std::make_shared<const sequence::ShowFogGrid>(*frame.fog.grid);CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));CHECK(renderer.volumetricDensityField().texture.id==texture);
 frame.fog.value.density=-1;CHECK(!presenter.append(renderer,scene,frame,identity,identity,64,64));CHECK(renderer.volumetricSettings().density==.2f);
 frame.fog.value.density=.2f;frame.fog.grid.reset();frame.fog.value.local=false;CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));CHECK(!renderer.volumetricDensityField().profile);CHECK(!renderer.volumetricSettings().local);
 frame.fog.authored=false;settings=renderer.volumetricSettings();settings.density=.07f;renderer.setVolumetricSettings(settings);CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));CHECK(renderer.volumetricSettings().density==.07f);renderer.shutdown();
}
TEST_SUITE_END
