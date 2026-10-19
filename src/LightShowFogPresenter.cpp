#include <AYSequence/LightShowPresenter.h>
#include <AYRenderer.h>
namespace ayt::sequence {
bool LightShowPresenter::applyFogFrame(render::Renderer& renderer,const LightShowFrame& frame,std::string* error){
 if(_fogRenderer!=&renderer){_fogRenderer=&renderer;_fogSource.reset();_fogProfile.reset();_fogTexture={};}
 const auto& source=frame.fog;
 if(!source.authored||!source.grid){
  const auto current=renderer.volumetricDensityField();
  if(source.authored||(_fogTexture.isValid()&&current.texture.id==_fogTexture.id))if(!renderer.setVolumetricDensityField({},error))return false;
  if(_fogTexture.isValid())renderer.destroyTexture(_fogTexture);_fogSource.reset();_fogProfile.reset();
  if(!source.authored)return true;
 }else{
  const auto& g=*source.grid;
  if(!source.value.local||!g.width||!g.height||!g.depth||g.width>64||g.height>64||g.depth>64||g.values.size()!=size_t(g.width)*g.height*g.depth){if(error)*error="Invalid authored local density grid";return false;}
  if(_fogSource!=source.grid){
   const bool same=_fogSource&&_fogSource->width==g.width&&_fogSource->height==g.height&&_fogSource->depth==g.depth&&_fogSource->values==g.values;
   if(!same){
    auto profile=render::VolumetricDensityField::create(g.width,g.height,g.depth,g.values,error);if(!profile)return false;
    auto texture=render::createVolumetricDensityTexture(renderer,*profile);if(!texture.isValid()){if(error)*error="Fog density atlas unavailable";return false;}
    if(!renderer.setVolumetricDensityField({profile,texture},error)){renderer.destroyTexture(texture);return false;}
    if(_fogTexture.isValid())renderer.destroyTexture(_fogTexture);_fogProfile=std::move(profile);_fogTexture=texture;
   }
   _fogSource=source.grid;
  }
  if(!renderer.setVolumetricDensityField({_fogProfile,_fogTexture},error))return false;
 }
 auto settings=renderer.volumetricSettings();const auto& value=source.value;
 settings.enabled=value.enabled;settings.local=value.local;settings.density=value.density;settings.scattering=value.scattering;settings.anisotropy=value.anisotropy;settings.maxDistance=value.maxDistance;settings.noiseStrength=value.noiseStrength;settings.noiseScale=value.noiseScale;settings.noiseTime=float(frame.time);
 for(int axis=0;axis<3;++axis){settings.center[axis]=value.center[axis];settings.halfExtent[axis]=value.halfExtent[axis];settings.tint[axis]=value.tint[axis];}
 renderer.setVolumetricSettings(settings);return true;
}
}
