#include <AYSequence/LightShowPresenter.h>
#include <AYRenderer.h>
#include <cmath>
#include <algorithm>
#include <chrono>
namespace ayt::sequence {
void LightShowPresenter::bindGoboTexture(std::uint32_t fixtureId,render::TextureHandle texture) {
    if(texture.isValid())_goboTextures[fixtureId]=texture;
    else _goboTextures.erase(fixtureId);
}
void LightShowPresenter::bindIesProfile(std::uint32_t id,std::shared_ptr<const render::IesProfile> profile,render::TextureHandle atlas){
    if(profile&&atlas.isValid())_iesProfiles[id]={std::move(profile),atlas};else _iesProfiles.erase(id);
}
bool LightShowPresenter::append(render::Renderer& renderer,render::RenderScene& scene,const LightShowFrame& frame,
    const math::Float4x4& view,const math::Float4x4& projection,uint16_t width,uint16_t height,
    render::PerformanceLightBudget lightBudget,render::EmissivePointSettings pointSettings,std::string* error) {
    _frameStats={};_frameStats.enabled=_profiling;
    using Clock=std::chrono::steady_clock;
    const auto start=_profiling?Clock::now():Clock::time_point{};auto mark=start;
    struct Finish {LightShowPresenterFrameStats& stats;Clock::time_point start;~Finish(){if(stats.enabled)stats.totalMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();}} finish{_frameStats,start};
    const auto lap=[&](double& target){if(_profiling){const auto now=Clock::now();target=std::chrono::duration<double,std::milli>(now-mark).count();mark=now;}};
    _lights.resize(frame.fixtures.size());
    for(size_t i=0;i<_lights.size();++i) {
        const auto& source=frame.fixtures[i];const auto& s=source.value;
        auto& target=_lights[i];target.id=source.id;
        const math::FVector3 direction(std::sin(s.tilt)*std::sin(s.pan),-std::cos(s.tilt),std::sin(s.tilt)*std::cos(s.pan));
        target.light=render::Light::spot({s.position[0],s.position[1],s.position[2]},direction,s.range,s.intensity,
            std::cos(s.innerAngle),std::cos(s.outerAngle),{s.color[0],s.color[1],s.color[2]});
        target.light.castShadow=s.shadow;target.volumetric=s.volumetric;
        target.apertureRadius=s.apertureRadius;target.constantFlux=_constantFlux;
        target.pattern=static_cast<render::LightPattern>(s.gobo);target.patternRotation=s.goboRotation;target.patternScale=s.goboScale;
        target.patternFocus=s.goboFocus;target.iris=s.iris;
        target.shadowSoftness=s.shadowSoftness;target.areaShadow=s.areaShadow;target.areaShadowBias=s.areaShadowBias;target.intensityUnit=static_cast<render::LightIntensityUnit>(s.intensityUnit);
        target.focusDistance=s.focusDistance;target.prismCount=s.prismCount;target.prismAngle=s.prismAngle;target.prismRotation=s.prismRotation;
        if(source.requiresIes&&!_iesProfiles.contains(source.id)){if(error)*error="IES profile is not bound for fixture "+std::to_string(source.id);return false;}
        target.iesProfile.reset();target.iesTexture={};
        const auto ies=_iesProfiles.find(source.id);if(ies!=_iesProfiles.end()){target.iesProfile=ies->second.profile;target.iesTexture=ies->second.atlas;}

        target.goboTexture={};
        if(s.gobo==ShowGobo::Texture) {
            const auto found=_goboTextures.find(source.id);
            if(found==_goboTextures.end()) {
                if(error)*error="Texture Gobo is not bound for fixture "+std::to_string(source.id);
                return false;
            }
            target.goboTexture=found->second;
        }
    }
    lap(_frameStats.lightsMs);
    _pointData.resize(frame.points.size());
    for(size_t i=0;i<_pointData.size();++i) {
        const auto& p=frame.points[i];
        _pointData[i]={p.id,{p.position[0],p.position[1],p.position[2]},{p.color[0],p.color[1],p.color[2]},p.intensity,p.radius};
    }
    lap(_frameStats.pointDataMs);
    render::EmissivePointLightingStats aggregateStats;
    auto settings=_pointLighting;
    if(settings.maxLights>64){if(error)*error="Point light budget exceeds64";return false;}
    if(settings.maxLights>0){
        settings.maxLights=std::min<uint16_t>(settings.maxLights,uint16_t(256-_lights.size()));
        if(!render::buildEmissivePointLights(_pointData,settings,_aggregateLights,aggregateStats,error))return false;
    }else{_aggregateLights.clear();aggregateStats.source=uint32_t(_pointData.size());}
    for(auto light:_aggregateLights){
        uint32_t id=0;
        while(std::any_of(_lights.begin(),_lights.end(),[&](const auto& l){return l.id==id;}))++id;
        light.id=id;light.priority=-1;_lights.push_back(light);
    }
    lap(_frameStats.aggregationMs);
    if(!renderer.setPerformanceLights(_lights,lightBudget,error))return false;
    lap(_frameStats.packetMs);
    _pointLightingStats=aggregateStats;
    if(_fixtureHousingsEnabled&&!_housings.append(renderer,scene,std::span<const render::PerformanceLight>(_lights).first(frame.fixtures.size()),_fixtureHousingSettings,error))return false;
    lap(_frameStats.housingsMs);
    if(!_points.append(renderer,scene,_pointData,view,projection,width,height,pointSettings,error))return false;
    lap(_frameStats.pointsMs);
    const bool completed=!_fixtureEmittersEnabled||_emitters.append(renderer,scene,
        std::span<const render::PerformanceLight>(_lights).first(frame.fixtures.size()),view,projection,width,height,_fixtureEmitterSettings,error);
    lap(_frameStats.emittersMs);if(_profiling)_frameStats.completed=completed;return completed;
}
} // namespace ayt::sequence
