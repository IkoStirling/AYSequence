#include <AYSequence/LightShowAssetIO.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYIO/File.h>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <cmath>

namespace ayt::sequence {
using Json=nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FixtureState,position,color,pan,tilt,intensity,range,innerAngle,outerAngle,volumetric,goboRotation,goboScale,gobo,shadow,apertureRadius,goboFocus,iris,shadowSoftness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FixtureKey,time,value,interpolation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FixtureTrack,id,group,goboTexturePath,keys,sweepPan,sweepTilt,sweepFrequency,phase,strobeFrequency,strobeDuty)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ShowPoint,id,position,color,intensity,radius)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FormationKey,time,points,interpolation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LightShow,name,duration,loop,fixtures,formations)
namespace {
constexpr std::size_t maxBytes=128*1024*1024;
bool fail(std::string* error,const std::string& text){if(error)*error=text;return false;}
bool pathValid(const std::string& path){return !path.empty()&&path.find('\0')==std::string::npos&&asset_format::isCanonicalPath(path,asset_format::Id::LightShow);}
Json document(const LightShow& show){return Json{{"type","authoring.light-show"},{"version",4},{"show",show}};}
bool sameSchema(const Json& input,const Json& canonical) {
    if(canonical.is_number_float())return input.is_number()&&std::isfinite(input.get<double>());
    if(canonical.is_number_integer())return input.is_number_integer()&&input==canonical;
    if(canonical.is_object()) {
        if(!input.is_object()||input.size()!=canonical.size())return false;
        for(const auto& [name,value]:canonical.items())if(!input.contains(name)||!sameSchema(input.at(name),value))return false;
        return true;
    }
    if(canonical.is_array()) {
        if(!input.is_array()||input.size()!=canonical.size())return false;
        for(size_t i=0;i<canonical.size();++i)if(!sameSchema(input[i],canonical[i]))return false;
        return true;
    }
    return input.type()==canonical.type()&&input==canonical;
}
}
bool decodeLightShow(std::string_view bytes,LightShow& output,std::string* error) {
    if(bytes.size()>maxBytes)return fail(error,"Light show exceeds128MiB");
    try {
        std::vector<std::unordered_set<std::string>> keys;
        auto json=Json::parse(bytes.begin(),bytes.end(),[&](int depth,Json::parse_event_t event,Json& value){
            if(depth>32)throw std::runtime_error("Light show nesting exceeds32");
            if(event==Json::parse_event_t::object_start)keys.emplace_back();
            if(event==Json::parse_event_t::key&&!keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("Duplicate light show JSON key");
            if(event==Json::parse_event_t::object_end)keys.pop_back();return true;
        });
        if(!json.is_object()||json.size()!=3||json.at("type")!="authoring.light-show"
            || !json.at("version").is_number_integer() || (json.at("version")!=1&&json.at("version")!=2&&json.at("version")!=3&&json.at("version")!=4))
            return fail(error,"Unsupported light show schema");
        // Check outer budgets before copying JSON arrays into runtime structures.
        const auto& data=json.at("show");
        if(data.at("fixtures").size()>256||data.at("formations").size()>4096)return fail(error,"Light show collection budget exceeded");
        std::size_t samples=0;
        for(const auto& f:data.at("fixtures")){samples+=f.at("keys").size();if(samples>1000000)return fail(error,"Light show sample budget exceeded");}
        for(const auto& f:data.at("formations")){samples+=f.at("points").size();if(samples>1000000)return fail(error,"Light show sample budget exceeded");}
        // Bound collections before upgrading. Schema1 has no aperture field;
        // reject smuggled v2 fields, then retain its original point-source model.
        if(json.at("version")==1) {
            for(auto& fixture:json.at("show").at("fixtures"))for(auto& key:fixture.at("keys")) {
                auto& value=key.at("value");
                if(value.contains("apertureRadius"))return fail(error,"Schema1 cannot contain apertureRadius");
                value["apertureRadius"]=0.0f;
            }
            json["version"]=2;
        }
        if(json.at("version")==2) {
            for(auto& fixture:json.at("show").at("fixtures"))for(auto& key:fixture.at("keys")) {
                auto& value=key.at("value");
                if(value.contains("goboFocus")||value.contains("iris"))
                    return fail(error,"Schema2 cannot contain goboFocus or iris");
                value["goboFocus"]=0.0f;
                value["iris"]=1.0f;
            }
            json["version"]=3;
        }
        if(json.at("version")==3) {
            for(auto& fixture:json.at("show").at("fixtures"))for(auto& key:fixture.at("keys")) {
                auto& value=key.at("value");
                if(value.contains("shadowSoftness"))return fail(error,"Schema3 cannot contain shadowSoftness");
                value["shadowSoftness"]=0.0f;
            }
            for(auto& fixture:json.at("show").at("fixtures")) {
                if(fixture.contains("goboTexturePath"))return fail(error,"Schema3 cannot contain goboTexturePath");
                fixture["goboTexturePath"]="";
            }
            json["version"]=4;
        }
        auto show=data.get<LightShow>();
        // Shape/value checks reject unknown fields and lossy enum/int/vector
        // conversion; ordinary decimal floats may round to their runtime type.
        if(!sameSchema(json,document(show)))return fail(error,"Unknown field or lossy light show value");
        auto compiled=CompiledLightShow::compile(std::move(show),error);if(!compiled)return false;
        output=compiled->data();if(error)error->clear();return true;
    }catch(const std::exception& e){return fail(error,e.what());}
}
bool encodeLightShow(const LightShow& show,std::string& output,std::string* error) {
    const auto compiled=CompiledLightShow::compile(show,error);if(!compiled)return false;
    try {
        auto bytes=document(compiled->data()).dump(2);
        if(bytes.size()>maxBytes)return fail(error,"Light show exceeds128MiB");
        output=std::move(bytes);if(error)error->clear();return true;
    }catch(const std::exception& e){return fail(error,e.what());}
}
bool loadLightShow(const std::string& path,LightShow& output,std::string* error) {
    if(!pathValid(path))return fail(error,"Expected explicit .lshow path");
    io::File file(path,io::File::Mode::BinaryRead);
    const auto size=file.size();if(!file.isOpen()||size>maxBytes)return fail(error,"Cannot open light show or file too large");
    std::string bytes(size,'\0');if(file.read(bytes.data(),bytes.size())!=bytes.size())return fail(error,"Incomplete light show read");
    return decodeLightShow(bytes,output,error);
}
bool saveLightShow(const std::string& path,const LightShow& show,std::string* error) {
    if(!pathValid(path))return fail(error,"Expected explicit .lshow path");
    std::string bytes;if(!encodeLightShow(show,bytes,error))return false;
    return io::File::atomicWrite(path,bytes.data(),bytes.size())||fail(error,"Atomic light show save failed");
}
} // namespace ayt::sequence
