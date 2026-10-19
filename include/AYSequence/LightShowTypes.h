#pragma once
#include <array>
#include <cstdint>
namespace ayt::sequence {
using ShowVec3=std::array<float,3>;
enum class ShowInterpolation:std::uint8_t {Step,Linear,Smooth};
}
