#include "LightShowFogInternal.h"
#include <algorithm>
#include <cmath>
namespace ayt::sequence::detail {
bool validFogState(const ShowFogState& v) noexcept {
 const auto finite=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
 if(!finite(v.density,0,1)||!finite(v.scattering,0,1)||!finite(v.anisotropy,-.9f,.9f)||!finite(v.maxDistance,.1f,500)||!finite(v.noiseStrength,0,1)||!finite(v.noiseScale,.01f,2))return false;
 for(int axis=0;axis<3;++axis)if(!finite(v.center[axis],-100000,100000)||!finite(v.halfExtent[axis],.01f,10000)||!finite(v.tint[axis],0,1))return false;
 return true;
}
bool validateFogTrack(const ShowFogTrack& track,double duration,std::string* error){
 const auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(track.keys.size()>4096)return fail("Fog key count exceeds4096");
 const auto& g=track.grid;
 if(g.values.empty()){if(g.width||g.height||g.depth)return fail("Empty fog grid must have zero dimensions");}
 else if(track.keys.empty()||!g.width||!g.height||!g.depth||g.width>64||g.height>64||g.depth>64||g.values.size()!=size_t(g.width)*g.height*g.depth)return fail("Invalid bounded fog grid");
 double previous=-1;
 for(const auto& k:track.keys){
  const auto& v=k.value;
  if(!std::isfinite(k.time)||k.time<0||k.time>duration||k.time<=previous||k.interpolation>ShowInterpolation::Smooth)return fail("Invalid fog key time/interpolation");
  previous=k.time;
  if(!validFogState(v))return fail("Invalid physical fog parameters/bounds/tint");
  if(!g.values.empty()&&!v.local)return fail("Authored fog grid requires a local box on every key");
 }
 return true;
}
ShowFogState evaluateFogTrack(const ShowFogTrack& track,double time){
 if(track.keys.empty())return {};
 const auto it=std::upper_bound(track.keys.begin(),track.keys.end(),time,[](double t,const auto& k){return t<k.time;});
 if(it==track.keys.begin())return it->value;
 const auto& a=*(it-1);if(it==track.keys.end()||a.interpolation==ShowInterpolation::Step)return a.value;
 const auto& b=*it;float t=float(std::clamp((time-a.time)/(b.time-a.time),0.0,1.0));if(a.interpolation==ShowInterpolation::Smooth)t=t*t*(3-2*t);
 auto value=a.value;const auto mix=[&](float x,float y){return x+(y-x)*t;};
 value.density=mix(a.value.density,b.value.density);value.scattering=mix(a.value.scattering,b.value.scattering);value.anisotropy=mix(a.value.anisotropy,b.value.anisotropy);value.maxDistance=mix(a.value.maxDistance,b.value.maxDistance);value.noiseStrength=mix(a.value.noiseStrength,b.value.noiseStrength);value.noiseScale=mix(a.value.noiseScale,b.value.noiseScale);
 for(int axis=0;axis<3;++axis){value.center[axis]=mix(a.value.center[axis],b.value.center[axis]);value.halfExtent[axis]=mix(a.value.halfExtent[axis],b.value.halfExtent[axis]);value.tint[axis]=mix(a.value.tint[axis],b.value.tint[axis]);}
 return value;
}
}
