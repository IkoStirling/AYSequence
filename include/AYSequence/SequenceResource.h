#pragma once

#include <AYResource/IResource.h>
#include <AYSequence/Sequence.h>

#include <memory>

namespace ayt::sequence {

/// Optional AYResource CPU wrapper for validated loose .seq or cooked .sqc
/// files and explicitly supplied bytes. A returned compiled snapshot remains valid after
/// ResourceManager eviction or a later reload.
class SequenceResource final : public ayt::resource::IResource {
public:
    bool load(const std::string& path) override;
    bool reload(const std::string& path) override { return load(path); }
    bool unload() override;
    std::size_t sizeInBytes() const override;
    [[nodiscard]] std::shared_ptr<const CompiledSequence> snapshot() const noexcept {
        return _snapshot;
    }
    [[nodiscard]] std::uint64_t dependencySignature() const noexcept {
        return _dependencySignature;
    }
    /// Decode validated .seq JSON bytes without changing an existing good
    /// snapshot on failure. The loader owns the optional resource path.
    [[nodiscard]] bool decode(const void* data, std::size_t size);

private:
    std::shared_ptr<const CompiledSequence> _snapshot;
    std::size_t _sourceBytes = 0;
    std::uint64_t _dependencySignature = 0;
};

/// Explicitly register .seq, cooked .sqc, and the read-only .ayseq alias before requesting
/// SequenceResource from ResourceManager. Registration is process-global.
void registerSequenceResourceLoader();

/// Discard Manager cache/failure state, then load again. Existing copied
/// CompiledSequence snapshots remain owned by their callers.
[[nodiscard]] bool reloadSequenceResource(const std::string& path);

} // namespace ayt::sequence
