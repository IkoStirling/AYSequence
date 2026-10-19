#pragma once
#include <AYSequence/LightShowTypes.h>
#include <memory>
#include <vector>
namespace ayt::sequence {
/// Authored physical medium; rendering quality and temporal options stay with the host.
struct ShowFogState {
 bool enabled=true,local=false;
 float density=.025f,scattering=.8f,anisotropy=.2f,maxDistance=100;
 ShowVec3 center{0,0,0},halfExtent{10,10,10},tint{1,1,1};
 float noiseStrength=0,noiseScale=.2f;
};
struct ShowFogKey {double time=0;ShowFogState value;ShowInterpolation interpolation=ShowInterpolation::Linear;};
/// X-major normalized extinction multipliers, eight bits/cell, at most64 per axis.
/// Empty data requires zero dimensions and means uniform density. Local box maps [-1,1]^3.
struct ShowFogGrid {std::uint8_t width=0,height=0,depth=0;std::vector<std::uint8_t> values;};
struct ShowFogTrack {std::vector<ShowFogKey> keys;ShowFogGrid grid;};
struct EvaluatedShowFog {
 bool authored=false;ShowFogState value;
 /// Compiled immutable grid shared across evaluated frames, without voxel copies.
 std::shared_ptr<const ShowFogGrid> grid;
};
}
