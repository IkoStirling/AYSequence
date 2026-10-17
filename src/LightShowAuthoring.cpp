#include <AYSequence/LightShowAuthoring.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace ayt::sequence {
bool editLightShowKeys(const LightShow& source,std::span<const uint32_t> fixtureIds,
    bool formations,double sourceTime,double destinationTime,LightShowKeyEdit operation,
    ShowInterpolation interpolation,LightShow& output,std::string* error) {
    const auto fail=[&](const char* message){if(error)*error=message;return false;};
    if(operation>LightShowKeyEdit::Interpolation||interpolation>ShowInterpolation::Smooth
        ||!std::isfinite(sourceTime)||sourceTime<0||sourceTime>source.duration
        ||!std::isfinite(destinationTime)||destinationTime<0||destinationTime>source.duration)
        return fail("Invalid key operation/time/interpolation");
    auto compiled=CompiledLightShow::compile(source,error);if(!compiled)return false;
    auto data=compiled->data();std::unordered_set<uint32_t> selected;
    for(auto id:fixtureIds){
        if(!selected.insert(id).second||std::none_of(data.fixtures.begin(),data.fixtures.end(),
            [&](const auto& track){return track.id==id;}))return fail("Unknown/duplicate selected fixture ID");
    }
    bool changed=false;
    const auto apply=[&](auto& keys){
        auto it=std::find_if(keys.begin(),keys.end(),[&](const auto& key){return std::abs(key.time-sourceTime)<=1e-6;});
        if(it==keys.end())return true;
        changed=true;
        if(operation==LightShowKeyEdit::Interpolation){it->interpolation=interpolation;return true;}
        if(operation==LightShowKeyEdit::Remove){keys.erase(it);return true;}
        if(std::abs(it->time-destinationTime)<=1e-6)return true;
        if(std::any_of(keys.begin(),keys.end(),[&](const auto& key){return std::abs(key.time-destinationTime)<=1e-6;}))
            return false;
        auto key=*it;key.time=destinationTime;
        if(operation==LightShowKeyEdit::Move)keys.erase(it);
        keys.insert(std::lower_bound(keys.begin(),keys.end(),destinationTime,[](const auto& k,double time){return k.time<time;}),std::move(key));
        return true;
    };
    for(auto& track:data.fixtures)if(selected.contains(track.id)&&!apply(track.keys))return fail("Destination already has a fixture key");
    if(formations&&!apply(data.formations))return fail("Destination already has a formation key");
    if(!changed)return fail("No selected key at the current time");
    auto result=CompiledLightShow::compile(std::move(data),error);if(!result)return false;
    output=result->data();if(error)error->clear();return true;
}
} // namespace ayt::sequence
