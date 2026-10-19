#pragma once
#include <AYSequence/LightShow.h>
#include <span>
namespace ayt::sequence {
enum class LightShowKeyEdit : uint8_t { Move, Copy, Remove, Interpolation };
/// Atomic edit of keys at sourceTime (absolute tolerance1e-6s). Select fixture
/// IDs and/or the formation lane; optional fog=true joins the same transaction. Tracks without a matching key are skipped;
/// at least one key must match. Destination collisions, missing/duplicate IDs,
/// out-of-range times and removing a fixture's last key reject the whole edit.
/// Interpolation changes the outgoing segment. Host owns undo/redo snapshots.
/// Input/output may alias; failure preserves output and no events/I/O occur.
bool editLightShowKeys(const LightShow& source,std::span<const uint32_t> fixtureIds,
    bool formations,double sourceTime,double destinationTime,LightShowKeyEdit operation,
    ShowInterpolation interpolation,LightShow& output,std::string* error=nullptr,bool fog=false);
struct FogDensityBrush {
    std::array<uint16_t,3> center{0,0,0};
    uint16_t radius=1; // [1,16] cells; smooth radial falloff
    uint8_t value=0;
    float strength=1; // [0,1]
    bool volume=false; // false paints only the selected Z layer
};
/// Atomic grid-only stamp for interactive stroke previews; no show compilation or GPU.
/// Invalid input preserves output; source/output may alias, at most262144 bytes copied.
bool paintFogDensityGrid(const ShowFogGrid& source,const FogDensityBrush& brush,
    ShowFogGrid& output,std::string* error=nullptr);
/// Atomic density stroke on an existing bounded local fog grid. Does not alter keys.
/// Invalid source/brush preserves output; source and output may alias. Host owns undo.
bool paintLightShowFogDensity(const LightShow& source,const FogDensityBrush& brush,
    LightShow& output,std::string* error=nullptr);
} // namespace ayt::sequence
