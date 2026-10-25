#include <AYSequence/SequenceResource.h>

#include <AYAssetFormat/AssetFormat.h>
#include <AYResource/IResourceLoader.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/ResourceRegistry.h>
#include <AYSequence/SequenceAssetIO.h>
#include <AYSequence/SequenceCooked.h>

#include <mutex>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace ayt::sequence {
namespace {
class Loader final : public ayt::resource::IResourceLoader {
public:
    bool canLoad(const std::string& path) const override {
        return ayt::asset_format::matchesPath(path,
            ayt::asset_format::Id::SceneSequence)
            || ayt::asset_format::matchesPath(path,
                ayt::asset_format::Id::SceneSequenceCooked);
    }
    const char* getResourceType() const override { return "SceneSequence"; }
    std::shared_ptr<ayt::resource::IResource> load(
        const std::string& path) override {
        auto value = std::make_shared<SequenceResource>();
        return value->load(path) ? value : nullptr;
    }
    std::shared_ptr<ayt::resource::IResource> loadFromBinary(
        const void* data, std::size_t size) override {
        auto value = std::make_shared<SequenceResource>();
        return value->decode(data, size) ? value : nullptr;
    }
    std::shared_ptr<ayt::resource::IResource> loadAsync(
        const std::string& path,
        std::function<void(std::shared_ptr<ayt::resource::IResource>)> callback) override {
        auto result = load(path);
        if (callback) callback(result);
        return result;
    }
};
std::unique_ptr<ayt::resource::IResourceLoader> createLoader() {
    return std::make_unique<Loader>();
}
}

bool SequenceResource::load(const std::string& path) {
    if (ayt::asset_format::matchesPath(path,
        ayt::asset_format::Id::SceneSequenceCooked)) {
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(path, sizeError);
        if (sizeError || size < 32 || size > 32 + 16 * 1024 * 1024)
            return false;
        std::ifstream stream(path, std::ios::binary);
        if (!stream) return false;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (stream.gcount() != static_cast<std::streamsize>(bytes.size())
            || stream.peek() != std::char_traits<char>::eof()
            || !decode(bytes.data(), bytes.size())) return false;
        _path = path;
        return true;
    }
    Sequence source;
    if (!loadSequence(path, source)) return false;
    auto compiled = CompiledSequence::compile(std::move(source));
    if (!compiled) return false;
    _snapshot = std::move(compiled);
    std::error_code sizeError;
    const auto bytes = std::filesystem::file_size(path, sizeError);
    _sourceBytes = sizeError ? 0 : static_cast<std::size_t>(bytes);
    _dependencySignature = 0;
    _path = path;
    _type = "SceneSequence";
    _loaded = true;
    return true;
}

bool SequenceResource::decode(const void* data, std::size_t size) {
    if (!data || size == 0 || size > 32 + 16 * 1024 * 1024) return false;
    Sequence source;
    SequenceCookedMetadata cooked;
    const bool binary = size >= 4
        && std::string_view(static_cast<const char*>(data), 4) == "AYSQ";
    if (binary) {
        if (!decodeCookedSequence({static_cast<const std::uint8_t*>(data),
            size}, source, &cooked)) return false;
    } else if (size <= 16 * 1024 * 1024) {
        if (!decodeSequence(std::string_view(static_cast<const char*>(data),
            size), source)) return false;
    } else return false;
    auto compiled = CompiledSequence::compile(std::move(source));
    if (!compiled) return false;
    _snapshot = std::move(compiled);
    _sourceBytes = size;
    _dependencySignature = binary ? cooked.dependencySignature : 0;
    _type = "SceneSequence";
    _loaded = true;
    return true;
}

bool SequenceResource::unload() {
    _snapshot.reset();
    _sourceBytes = 0;
    _dependencySignature = 0;
    _loaded = false;
    return true;
}

std::size_t SequenceResource::sizeInBytes() const {
    return sizeof(*this) + _sourceBytes;
}

void registerSequenceResourceLoader() {
    static std::once_flag once;
    std::call_once(once, [] {
        const auto& format = ayt::asset_format::describe(
            ayt::asset_format::Id::SceneSequence);
        ayt::resource::ResourceRegistry::registerExtension(
            std::string(format.suffix), "SceneSequence");
        for (const auto alias : format.readAliases)
            ayt::resource::ResourceRegistry::registerExtension(
                std::string(alias), "SceneSequence");
        ayt::resource::ResourceRegistry::registerExtension(
            std::string(ayt::asset_format::suffix(
                ayt::asset_format::Id::SceneSequenceCooked)),
            "SceneSequence");
        ayt::resource::ResourceRegistry::registerLoader(
            "SceneSequence", &createLoader);
    });
}

bool reloadSequenceResource(const std::string& path) {
    registerSequenceResourceLoader();
    auto& manager = ayt::resource::ResourceManager::instance();
    manager.unloadResource(path);
    return manager.load<SequenceResource>(path) != nullptr;
}

} // namespace ayt::sequence
