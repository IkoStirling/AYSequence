#pragma once

#include <AYResource/IConverter.h>

namespace ayt::resource {

/// Optional ProjectBuild importer for a validated scene sequence. Referenced
/// runtime assets remain separate package files with explicit dependency edges.
class SequenceConverter final : public IConverter {
public:
    explicit SequenceConverter(std::string source) : _source(std::move(source)) {}
    void setSourcePath(const std::string& path) override { _source = path; }
    void setOutputDir(const std::string& path) override { _output = path; }
    void setLoadOption(LoadOption) override {}
    void setAssetRoot(const std::string& path) { _root = path; }
    ConversionResult convert() override;
    bool isValid() const override { return !_source.empty() && !_root.empty(); }
    const char* getSourceType() const override { return "SceneSequence"; }
private:
    std::string _source, _output, _root;
};

} // namespace ayt::resource
