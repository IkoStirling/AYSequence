#pragma once
#include <AYSequence/LightShow.h>
#include <string_view>
namespace ayt::sequence {
/// Strict schema2 authoring.light-show JSON, .lshow. Reads schema1 with zero
/// aperture to retain its point-source profile; writes schema2. Reject unknown/missing
/// fields, duplicate keys, nesting>32, bytes>128MiB and invalid runtime values.
/// Failure preserves output. Explicit loose files; no automatic cook/loader.
bool decodeLightShow(std::string_view bytes,LightShow& output,std::string* error=nullptr);
bool encodeLightShow(const LightShow& show,std::string& output,std::string* error=nullptr);
bool loadLightShow(const std::string& path,LightShow& output,std::string* error=nullptr);
/// Canonical extension, validated atomic single-file publication through AYIO.
bool saveLightShow(const std::string& path,const LightShow& show,std::string* error=nullptr);
} // namespace ayt::sequence
