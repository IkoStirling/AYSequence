#pragma once
#include <AYSequence/LightShow.h>
#include <string_view>
namespace ayt::sequence {
/// Strict schema5 authoring.light-show JSON, .lshow. Reads schema1 with zero
/// aperture, schema1/2 with sharp full-open pattern optics and schema1/2/3
/// with empty Gobo path and zero shadow softness; with schema1..4 using Legacy intensity and no IES; writes schema5. Reject unknown/missing
/// fields, duplicate keys, nesting>32, bytes>128MiB and invalid runtime values.
/// Failure preserves output. Explicit loose files; no automatic cook/loader.
bool decodeLightShow(std::string_view bytes,LightShow& output,std::string* error=nullptr);
bool encodeLightShow(const LightShow& show,std::string& output,std::string* error=nullptr);
bool loadLightShow(const std::string& path,LightShow& output,std::string* error=nullptr);
/// Canonical extension, validated atomic single-file publication through AYIO.
bool saveLightShow(const std::string& path,const LightShow& show,std::string* error=nullptr);
/// Publish a portable directory containing the validated .lshow, referenced Gobo
/// images, optional music, and a small manifest. The destination must not exist;
/// failed exports remove only the directory created by this call.
bool exportLightShowBundle(const std::string& sourceShowPath,const std::string& bundleDirectory,
    const std::string& musicPath={},std::string* error=nullptr);
/// Find the optional packaged music beside a .lshow; an ordinary show has none.
/// Rejects malformed manifests and paths leaving the bundle directory.
bool bundledLightShowMusic(const std::string& showPath,std::string& musicPath,std::string* error=nullptr);
} // namespace ayt::sequence
