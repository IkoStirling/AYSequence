#include <AYSequence/SequenceAssetIO.h>
#include <AYSequence/SequenceCooked.h>

#include <AYAssetFormat/AssetFormat.h>
#include <AYIO/File.h>

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: AYSequenceCookTool <source.seq> <Assets-root> <output.sqc>\n";
        return 2;
    }
    const std::string source = argv[1], output = argv[3];
    if (!ayt::asset_format::isCanonicalPath(source,
            ayt::asset_format::Id::SceneSequence)
        || !ayt::asset_format::isCanonicalPath(output,
            ayt::asset_format::Id::SceneSequenceCooked)) {
        std::cerr << "expected canonical .seq source and .sqc output\n";
        return 2;
    }
    ayt::sequence::Sequence sequence;
    ayt::sequence::Diagnostic error;
    if (!ayt::sequence::loadSequence(source, sequence, &error)) {
        std::cerr << error.location << ": " << error.message << '\n';
        return 1;
    }
    std::vector<std::uint8_t> cooked;
    if (!ayt::sequence::encodeCookedSequence(sequence, argv[2], cooked,
            &error)) {
        std::cerr << error.location << ": " << error.message << '\n';
        return 1;
    }
    if (!ayt::io::File::atomicWrite(output, cooked.data(), cooked.size())) {
        std::cerr << "atomic output write failed\n";
        return 1;
    }
    std::cout << "cooked " << sequence.id << " (" << cooked.size()
        << " bytes)\n";
    return 0;
}
