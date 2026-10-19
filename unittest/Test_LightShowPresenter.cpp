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
 const double sum=snapshot.lightsMs+snapshot.pointDataMs+snapshot.aggregationMs+snapshot.packetMs+snapshot.housingsMs+snapshot.pointsMs+snapshot.emittersMs;CHECK(sum>=0);CHECK(snapshot.totalMs+1e-6>=sum);
 frame.fixtures[0].value.gobo=sequence::ShowGobo::Texture;CHECK(!presenter.append(renderer,scene,frame,identity,identity,64,64,{}, {},&error));CHECK(presenter.frameStats().enabled);CHECK(!presenter.frameStats().completed);CHECK(presenter.frameStats().pointsMs==0);CHECK(snapshot.completed);
 presenter.setProfilingEnabled(false);CHECK(!presenter.frameStats().enabled);CHECK(presenter.frameStats().totalMs==0);renderer.shutdown();
}
TEST_SUITE_END
