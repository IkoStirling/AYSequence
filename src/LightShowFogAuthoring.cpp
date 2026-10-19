#include <AYSequence/LightShowAuthoring.h>
#include <algorithm>
#include <cmath>
namespace ayt::sequence {
bool paintFogDensityGrid(const ShowFogGrid& grid,const FogDensityBrush& brush,ShowFogGrid& output,std::string* error){
 const auto fail=[&](const char* text){if(error)*error=text;return false;};
 if(!grid.width||!grid.height||!grid.depth||grid.width>64||grid.height>64||grid.depth>64||grid.values.size()!=size_t(grid.width)*grid.height*grid.depth||brush.center[0]>=grid.width||brush.center[1]>=grid.height||brush.center[2]>=grid.depth||!brush.radius||brush.radius>16||!std::isfinite(brush.strength)||brush.strength<0||brush.strength>1)return fail("Invalid fog brush/grid");
 auto edited=grid;const int r=brush.radius,cx=brush.center[0],cy=brush.center[1],cz=brush.center[2];
 const int z0=brush.volume?std::max(0,cz-r):cz,z1=brush.volume?std::min(int(grid.depth)-1,cz+r):cz;
 for(int z=z0;z<=z1;++z)for(int y=std::max(0,cy-r);y<=std::min(int(grid.height)-1,cy+r);++y)for(int x=std::max(0,cx-r);x<=std::min(int(grid.width)-1,cx+r);++x){
  const float distance=std::sqrt(float((x-cx)*(x-cx)+(y-cy)*(y-cy)+(z-cz)*(z-cz)));if(distance>r)continue;
  const float weight=brush.strength*(1-distance/r);auto& value=edited.values[x+grid.width*(y+grid.height*z)];value=uint8_t(std::clamp(std::lround(value+(int(brush.value)-value)*weight),0l,255l));
 }
 output=std::move(edited);if(error)error->clear();return true;
}
bool paintLightShowFogDensity(const LightShow& source,const FogDensityBrush& brush,LightShow& output,std::string* error){
 auto compiled=CompiledLightShow::compile(source,error);if(!compiled)return false;auto data=compiled->data();
 if(!paintFogDensityGrid(data.fog.grid,brush,data.fog.grid,error))return false;
 auto result=CompiledLightShow::compile(std::move(data),error);if(!result)return false;output=result->data();if(error)error->clear();return true;
}
}
