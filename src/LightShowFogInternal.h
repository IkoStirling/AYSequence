#pragma once
#include <AYSequence/LightShowFog.h>
#include <string>
namespace ayt::sequence::detail {
bool validFogState(const ShowFogState& state) noexcept;
bool validateFogTrack(const ShowFogTrack& track,double duration,std::string* error);
ShowFogState evaluateFogTrack(const ShowFogTrack& track,double time);
}
