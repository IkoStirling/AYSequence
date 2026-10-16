#pragma once
#include <AYSequence/LightShow.h>
#include <AYRenderer/EmissivePointCloud.h>
#include <AYRenderer/PerformanceLighting.h>
namespace ayt::sequence {
/// Optional AYSequence::RendererIntegration. Feed one evaluated show frame on
/// the Renderer owner thread. Main Deferred lights and CPU-owned point batches
/// share the caller's camera/time. Does not advance the player or mutate World.
class LightShowPresenter final {
public:
    bool append(render::Renderer& renderer,render::RenderScene& scene,const LightShowFrame& frame,
        const math::Float4x4& view,const math::Float4x4& projection,uint16_t width,uint16_t height,
        render::PerformanceLightBudget lightBudget={},render::EmissivePointSettings pointSettings={},std::string* error=nullptr);
    render::EmissivePointStats pointStats() const noexcept { return _points.stats(); }
private:
    render::EmissivePointCloud _points;
    std::vector<render::PerformanceLight> _lights;
    std::vector<render::EmissivePoint> _pointData;
};
} // namespace ayt::sequence
