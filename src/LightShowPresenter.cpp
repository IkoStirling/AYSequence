#include <AYSequence/LightShowPresenter.h>
#include <AYRenderer.h>
#include <cmath>
namespace ayt::sequence {
void LightShowPresenter::bindGoboTexture(std::uint32_t fixtureId,render::TextureHandle texture) {
    if(texture.isValid())_goboTextures[fixtureId]=texture;
    else _goboTextures.erase(fixtureId);
}
bool LightShowPresenter::append(render::Renderer& renderer,render::RenderScene& scene,const LightShowFrame& frame,
    const math::Float4x4& view,const math::Float4x4& projection,uint16_t width,uint16_t height,
    render::PerformanceLightBudget lightBudget,render::EmissivePointSettings pointSettings,std::string* error) {
    _lights.resize(frame.fixtures.size());
    for(size_t i=0;i<_lights.size();++i) {
        const auto& source=frame.fixtures[i];const auto& s=source.value;
        auto& target=_lights[i];target.id=source.id;
        const math::FVector3 direction(std::sin(s.tilt)*std::sin(s.pan),-std::cos(s.tilt),std::sin(s.tilt)*std::cos(s.pan));
        target.light=render::Light::spot({s.position[0],s.position[1],s.position[2]},direction,s.range,s.intensity,
            std::cos(s.innerAngle),std::cos(s.outerAngle),{s.color[0],s.color[1],s.color[2]});
        target.light.castShadow=s.shadow;target.volumetric=s.volumetric;
        target.apertureRadius=s.apertureRadius;
        target.pattern=static_cast<render::LightPattern>(s.gobo);target.patternRotation=s.goboRotation;target.patternScale=s.goboScale;
        target.patternFocus=s.goboFocus;target.iris=s.iris;
        target.shadowSoftness=s.shadowSoftness;
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
    _pointData.resize(frame.points.size());
    for(size_t i=0;i<_pointData.size();++i) {
        const auto& p=frame.points[i];
        _pointData[i]={p.id,{p.position[0],p.position[1],p.position[2]},{p.color[0],p.color[1],p.color[2]},p.intensity,p.radius};
    }
    if(!renderer.setPerformanceLights(_lights,lightBudget,error))return false;
    return _points.append(renderer,scene,_pointData,view,projection,width,height,pointSettings,error);
}
} // namespace ayt::sequence
