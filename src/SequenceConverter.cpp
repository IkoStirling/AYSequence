#include <AYSequence/SequenceConverter.h>

#include <AYSequence/SequenceAssetIO.h>
#include <AYSequence/SequenceCooked.h>
#include <AYSequence/SequenceDependencies.h>
#include <AYAssetFormat/AssetFormat.h>
#include <AYIO/File.h>
#include <AYStorage/Guid.h>

#include <filesystem>
#include <stdexcept>

namespace ayt::resource {

ConversionResult SequenceConverter::convert() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto root = fs::canonical(fs::u8path(_root), ec);
    if (ec || !fs::is_directory(root))
        throw std::runtime_error("sequence Assets root is unavailable");
    const auto sourcePath = fs::u8path(_source);
    if (!fs::is_regular_file(fs::symlink_status(sourcePath, ec)) || ec)
        throw std::runtime_error("sequence source must be an ordinary file");
    const auto source = fs::canonical(sourcePath, ec);
    if (ec) throw std::runtime_error("sequence source is unavailable");
    const auto relative = source.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()
        || !ayt::asset_format::isCanonicalPath(relative.generic_string(),
            ayt::asset_format::Id::SceneSequence))
        throw std::runtime_error("sequence source is outside Assets or has wrong suffix");
    for (const auto& part : relative)
        if (part == ".." || part == ".")
            throw std::runtime_error("sequence source is outside Assets");
    ayt::sequence::Sequence authored;
    ayt::sequence::Diagnostic diagnostic;
    if (!ayt::sequence::loadSequence(source.string(), authored, &diagnostic))
        throw std::runtime_error("invalid sequence: " + diagnostic.message);
    ayt::sequence::SequenceDependencyManifest manifest;
    if (!ayt::sequence::buildSequenceDependencyManifest(authored, root,
            manifest, &diagnostic))
        throw std::runtime_error("invalid sequence dependency: " + diagnostic.message);
    std::vector<std::uint8_t> bytes;
    if (!ayt::sequence::encodeCookedSequence(authored, root, bytes, &diagnostic))
        throw std::runtime_error("sequence cook failed: " + diagnostic.message);
    auto outputName = relative;
    outputName.replace_extension(ayt::asset_format::suffix(
        ayt::asset_format::Id::SceneSequenceCooked));
    if (!_output.empty()) {
        const auto output = fs::u8path(_output) / outputName;
        fs::create_directories(output.parent_path());
        if (!ayt::io::File::atomicWrite(output.string(), bytes.data(), bytes.size()))
            throw std::runtime_error("sequence output write failed");
    }
    ConversionResult result;
    result.importerContractTag = "scene-sequence-cooked-v1";
    ConversionResult::ConvertedResource resource;
    resource.path = outputName.generic_string();
    resource.type = "SceneSequence";
    resource.size = bytes.size();
    resource.guid = ayt::storage::Guid::computeFromData(bytes.data(), bytes.size());
    result.resources.push_back(std::move(resource));
    for (const auto& dependency : manifest.entries)
        result.dependencies.push_back({outputName.generic_string(), dependency.path});
    return result;
}

} // namespace ayt::resource
