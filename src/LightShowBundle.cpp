#include <AYSequence/LightShowAssetIO.h>
#include <AYIO/File.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace ayt::sequence {
namespace {
namespace fs=std::filesystem;
bool fail(std::string* error,const std::string& message){if(error)*error=message;return false;}
bool inside(const fs::path& child,const fs::path& base){
    auto a=child.begin(),b=base.begin();
    for(;b!=base.end();++a,++b)if(a==child.end()||*a!=*b)return false;
    return true;
}
bool relativeAsset(const fs::path& path){
    if(path.empty()||path.is_absolute()||path.has_root_name()||path.has_root_directory())return false;
    return std::none_of(path.begin(),path.end(),[](const auto& part){return part==".."||part==".";});
}
bool copyBounded(const fs::path& from,const fs::path& to,std::uintmax_t& total,std::string* error){
    std::error_code ec;
    const auto size=fs::file_size(from,ec);
    if(ec||size>128ull*1024*1024||total+size>512ull*1024*1024)
        return fail(error,"Bundle asset is missing or exceeds the 128 MiB / 512 MiB budget");
    fs::create_directories(to.parent_path(),ec);
    if(ec||!fs::copy_file(from,to,fs::copy_options::none,ec)||ec)
        return fail(error,"Cannot copy bundle asset");
    total+=size;return true;
}
}
bool exportLightShowBundle(const std::string& sourceShowPath,const std::string& bundleDirectory,
    const std::string& musicPath,std::string* error){
    LightShow show;
    if(!loadLightShow(sourceShowPath,show,error))return false;
    if(bundleDirectory.empty())return fail(error,"Bundle directory is empty");
    const fs::path source(sourceShowPath),destination(bundleDirectory);
    std::error_code ec;
    const auto sourceBase=fs::canonical(source.parent_path().empty()?fs::path("."):source.parent_path(),ec);
    if(ec)return fail(error,"Cannot resolve the source show folder");
    std::set<fs::path> assets;
    for(const auto& fixture:show.fixtures){
        std::vector<std::string> references;
        if(std::any_of(fixture.keys.begin(),fixture.keys.end(),[](const auto& key){return key.value.gobo==ShowGobo::Texture;}))references.push_back(fixture.goboTexturePath);
        if(!fixture.iesProfilePath.empty())references.push_back(fixture.iesProfilePath);
        for(const auto& reference:references){
        const auto relative=fs::u8path(reference).lexically_normal();
        if(!relativeAsset(relative)||relative==fs::path("bundle.json")||relative==source.filename())
            return fail(error,"Invalid bundled Gobo path");
        const auto resolved=fs::canonical(sourceBase/relative,ec);
        if(ec||!inside(resolved,sourceBase)||!fs::is_regular_file(resolved))
            return fail(error,"Gobo image is missing or leaves the source folder");
        assets.insert(relative);
        }
    }
    fs::path musicSource,musicRelative;
    if(!musicPath.empty()){
        musicSource=fs::canonical(fs::path(musicPath),ec);
        if(ec||!fs::is_regular_file(musicSource))return fail(error,"Music file is missing");
        auto extension=musicSource.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(extension!=".wav"&&extension!=".mp3"&&extension!=".ogg")return fail(error,"Expected WAV, MP3 or OGG music");
        musicRelative=fs::path("__music")/("track"+extension);
        if(assets.contains(musicRelative))return fail(error,"Music path conflicts with a Gobo image");
    }
    const auto parent=destination.parent_path().empty()?fs::path("."):destination.parent_path();
    if(!fs::is_directory(parent)||fs::exists(destination))return fail(error,"Choose a new bundle folder");
    if(!fs::create_directory(destination,ec)||ec)return fail(error,"Cannot create bundle folder");
    bool success=false;
    do {
        std::uintmax_t total=0;
        if(!copyBounded(source,destination/source.filename(),total,error))break;
        bool copied=true;
        for(const auto& relative:assets){
            const auto resolved=fs::canonical(sourceBase/relative,ec);
            if(ec||!inside(resolved,sourceBase)||!copyBounded(resolved,destination/relative,total,error)){copied=false;break;}
        }
        if(!copied)break;
        if(!musicPath.empty()&&!copyBounded(musicSource,destination/musicRelative,total,error))break;
        const nlohmann::json manifest={{"type","authoring.light-show-bundle"},{"version",1},
            {"show",source.filename().generic_string()},{"music",musicRelative.generic_string()}};
        const auto content=manifest.dump(2);
        io::File file((destination/"bundle.json").string(),io::File::Mode::BinaryWrite);
        if(!file.isOpen()||file.write(content.data(),content.size())!=content.size()){
            fail(error,"Cannot write bundle manifest");break;
        }
        success=true;
    }while(false);
    if(!success){std::error_code ignored;fs::remove_all(destination,ignored);return false;}
    if(error)error->clear();return true;
}
bool bundledLightShowMusic(const std::string& showPath,std::string& musicPath,std::string* error){
    const fs::path show(showPath),manifest=show.parent_path()/"bundle.json";
    std::error_code ec;
    if(!fs::exists(manifest,ec)){if(ec)return fail(error,"Cannot inspect bundle manifest");musicPath.clear();if(error)error->clear();return true;}
    if(fs::file_size(manifest,ec)>4096||ec)return fail(error,"Bundle manifest is too large");
    try{
        io::File file(manifest.string(),io::File::Mode::BinaryRead);
        if(!file.isOpen())return fail(error,"Cannot read bundle manifest");
        std::string bytes(file.size(),'\0');
        if(file.read(bytes.data(),bytes.size())!=bytes.size())return fail(error,"Incomplete bundle manifest");
        const auto data=nlohmann::json::parse(bytes);
        if(!data.is_object()||data.size()!=4||data.at("type")!="authoring.light-show-bundle"
            ||data.at("version")!=1||!data.at("show").is_string()||!data.at("music").is_string()
            ||data.at("show").get<std::string>()!=show.filename().generic_string())
            return fail(error,"Invalid bundle manifest");
        const auto relative=fs::u8path(data.at("music").get<std::string>());
        if(relative.empty()){musicPath.clear();if(error)error->clear();return true;}
        if(!relativeAsset(relative))return fail(error,"Invalid bundle music path");
        const auto base=fs::canonical(show.parent_path(),ec);
        if(ec)return fail(error,"Cannot resolve bundle folder");
        const auto resolved=fs::canonical(base/relative,ec);
        if(ec||!inside(resolved,base)||!fs::is_regular_file(resolved))return fail(error,"Bundle music is missing or leaves the bundle folder");
        musicPath=resolved.string();if(error)error->clear();return true;
    }catch(const std::exception& e){return fail(error,e.what());}
}
} // namespace ayt::sequence
