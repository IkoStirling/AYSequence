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
TEST_CASE(point_lighting_history_is_optional_bounded_and_explicit_seek_reset_restores_preview) {
 render::Renderer renderer;render::InitDesc init;init.backend=render::Backend::Noop;CHECK(renderer.initialize(init));
 sequence::LightShowPresenter presenter;sequence::LightShowFrame frame;frame.points={{1,{0,0,0},{1,1,1},2,.05f},{2,{16,0,0},{1,1,1},1,.05f}};
 render::EmissivePointLightingSettings settings;settings.maxLights=1;settings.smoothCells=true;settings.contactDistance=1;
 presenter.setPointLighting(settings,.1f);const auto identity=math::Float4x4::identity();render::RenderScene scene;
 render::EmissivePointSettings pointSettings;pointSettings.maxPoints=0;
 CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{},pointSettings));CHECK(presenter.pointLightingStats().historyReset);CHECK(presenter.pointLightingStats().selectedLights==1);CHECK(presenter.pointStats().visible==0);
 frame.points[1].intensity=3;frame.time=.05;
 CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{},pointSettings));CHECK(presenter.pointLightingStats().fadingLights==1);CHECK(presenter.pointLightingStats().deferredCells==1);
 presenter.resetPointLightingHistory();CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{},pointSettings));CHECK(presenter.pointLightingStats().historyReset);CHECK(presenter.pointLightingStats().fadingLights==0);
 frame.points[0].intensity=frame.points[1].intensity=0;frame.time=.06;
 CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{},pointSettings));CHECK(presenter.pointLightingStats().selectedLights==0);
 settings.maxLights=0;presenter.setPointLighting(settings);CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64,{},pointSettings));CHECK(presenter.pointLightingStats().source==2);CHECK(presenter.pointLightingStats().selectedLights==0);
 renderer.shutdown();
}

TEST_CASE(explicit_cache_release_is_idempotent_and_presenter_can_reopen_on_live_renderer) {
 render::Renderer renderer;render::InitDesc init;init.backend=render::Backend::Noop;CHECK(renderer.initialize(init));
 sequence::LightShowPresenter presenter;presenter.setFixtureEmitters(true);presenter.setFixtureHousings(true);presenter.setProfilingEnabled(true);
 sequence::LightShowFrame frame;frame.fixtures.push_back({1,{}});frame.points.push_back({1,{0,0,0},{1,1,1},2,.1f});
 frame.fog.authored=true;frame.fog.value.local=true;frame.fog.grid=std::make_shared<const sequence::ShowFogGrid>(sequence::ShowFogGrid{2,2,2,{255,255,255,255,255,255,255,255}});
 const auto identity=math::Float4x4::identity();render::RenderScene scene;
 CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));const auto oldAtlas=renderer.volumetricDensityField().texture.id;CHECK(oldAtlas!=0);CHECK(presenter.pointStats().source==1);CHECK(presenter.fixtureHousingStats().fixtures==1);
 scene.clear();presenter.releaseResources(renderer);presenter.releaseResources(renderer);CHECK(!renderer.volumetricDensityField().profile);CHECK(presenter.pointStats().source==0);CHECK(presenter.fixtureHousingStats().fixtures==0);CHECK(!presenter.frameStats().completed);
 CHECK(presenter.append(renderer,scene,frame,identity,identity,64,64));CHECK(renderer.volumetricDensityField().texture.id!=oldAtlas);CHECK(presenter.pointStats().source==1);CHECK(presenter.fixtureHousingStats().fixtures==1);CHECK(presenter.frameStats().completed);
 scene.clear();presenter.releaseResources(renderer);renderer.shutdown();
}
TEST_SUITE_END
