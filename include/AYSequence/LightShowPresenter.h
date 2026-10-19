#pragma once
#include <AYSequence/LightShow.h>
#include <AYRenderer/EmissivePointCloud.h>
#include <AYRenderer/PerformanceLighting.h>
#include <AYRenderer/FixtureEmitters.h>
#include <unordered_map>
namespace ayt::sequence {
/// Optional AYSequence::RendererIntegration. Feed one evaluated show frame on
/// the Renderer owner thread. Main Deferred lights and CPU-owned point batches
/// share the caller's camera/time. Does not advance the player or mutate World.
class LightShowPresenter final {
public:
    /// Bind a live Renderer-owned texture to a fixture ID. A Texture key without
    /// a binding fails append before replacing the active light packet.
    void bindGoboTexture(std::uint32_t fixtureId,render::TextureHandle texture);
    /// Session presentation option, not serialized. Apply the same approximate
    /// angular-flux zoom model to all fixtures; see PerformanceLight::constantFlux.
    void setConstantFlux(bool enabled) noexcept { _constantFlux=enabled; }
    /// Pair the parsed immutable IES and Renderer-owned angular atlas for a
    /// fixture. A configured but missing binding fails before packet replacement.
    void bindIesProfile(std::uint32_t id,std::shared_ptr<const render::IesProfile> profile,render::TextureHandle atlas);
    void clearIesProfiles() noexcept { _iesProfiles.clear(); }
    void clearGoboTextures() noexcept { _goboTextures.clear(); }
    bool append(render::Renderer& renderer,render::RenderScene& scene,const LightShowFrame& frame,
        const math::Float4x4& view,const math::Float4x4& projection,uint16_t width,uint16_t height,
        render::PerformanceLightBudget lightBudget={},render::EmissivePointSettings pointSettings={},std::string* error=nullptr);
    /// Optional cell-based surface lighting. Budget0 keeps the original unlit
    /// point-cloud behavior. Fixture lights have priority; packet capacity256
    /// and surfaceLights budget are shared. Does not depend on maxPoints.
    void setPointLighting(render::EmissivePointLightingSettings settings) noexcept { _pointLighting=settings; }
    render::EmissivePointLightingStats pointLightingStats() const noexcept { return _pointLightingStats; }
    /// Optional visible, oriented HDR fixture exits. Default off preserves
    /// existing hosts. Uses Renderer medium settings and scene depth; intensity,
    /// iris, color, strobe and constantFlux follow the same evaluated frame.
    void setFixtureEmitters(bool enabled,render::FixtureEmitterSettings settings={}) noexcept {
        _fixtureEmittersEnabled=enabled;_fixtureEmitterSettings=settings;
    }
    render::FixtureEmitterStats fixtureEmitterStats() const noexcept { return _fixtureEmittersEnabled?_emitters.stats():render::FixtureEmitterStats{}; }
    render::EmissivePointStats pointStats() const noexcept { return _points.stats(); }
private:
    std::unordered_map<std::uint32_t,render::TextureHandle> _goboTextures;
    struct IesBinding {std::shared_ptr<const render::IesProfile> profile;render::TextureHandle atlas;};
    std::unordered_map<std::uint32_t,IesBinding> _iesProfiles;
    bool _constantFlux=false,_fixtureEmittersEnabled=false;
    render::FixtureEmitterSettings _fixtureEmitterSettings;
    render::FixtureEmitters _emitters;
    render::EmissivePointCloud _points;
    render::EmissivePointLightingSettings _pointLighting{0};
    render::EmissivePointLightingStats _pointLightingStats;
    std::vector<render::PerformanceLight> _aggregateLights;
    std::vector<render::PerformanceLight> _lights;
    std::vector<render::EmissivePoint> _pointData;
};
} // namespace ayt::sequence
