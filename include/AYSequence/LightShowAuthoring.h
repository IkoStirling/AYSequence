#pragma once
#include <AYSequence/LightShow.h>
#include <span>
namespace ayt::sequence {
enum class LightShowKeyEdit : uint8_t { Move, Copy, Remove, Interpolation };
/// Atomic edit of keys at sourceTime (absolute tolerance1e-6s). Select fixture
/// IDs and/or the formation lane. Tracks without a matching key are skipped;
/// at least one key must match. Destination collisions, missing/duplicate IDs,
/// out-of-range times and removing a fixture's last key reject the whole edit.
/// Interpolation changes the outgoing segment. Host owns undo/redo snapshots.
/// Input/output may alias; failure preserves output and no events/I/O occur.
bool editLightShowKeys(const LightShow& source,std::span<const uint32_t> fixtureIds,
    bool formations,double sourceTime,double destinationTime,LightShowKeyEdit operation,
    ShowInterpolation interpolation,LightShow& output,std::string* error=nullptr);
} // namespace ayt::sequence
