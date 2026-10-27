#include <AYSequence/DialoguePackageSource.h>

#include <AYLocalization.h>
#include <AYResource/assetsImpl/Audio.h>
#include <AYSequence/SequenceResource.h>
#include <AYStorage/IPackageReader.h>

#include <algorithm>
#include <exception>
#include <utility>

namespace ayt::sequence {
namespace {
constexpr std::uint64_t kJsonLimit = 1u * 1024u * 1024u;
constexpr std::uint64_t kSequenceLimit = 32u + 16u * 1024u * 1024u;
constexpr std::uint64_t kVoiceLimit = 128u * 1024u * 1024u;

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}
bool portableEntry(std::string_view path) {
    if (path.empty() || path.size() > 1024 || path.front() == '/' ||
        path.find('\\') != path.npos || path.find(':') != path.npos)
        return false;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find('/', begin);
        const auto part = path.substr(begin, end == path.npos ? path.npos : end - begin);
        if (part.empty() || part == "." || part == "..") return false;
        for (const unsigned char ch : part) if (ch < 0x20) return false;
        if (end == path.npos) return true;
        begin = end + 1;
    }
    return false;
}
} // namespace

bool DialoguePackageSource::open(std::string packagePath,
    std::string bindingsEntry, std::string* error) {
    if (packagePath.empty() || !portableEntry(bindingsEntry) ||
        !bindingsEntry.ends_with(".json"))
        return fail(error, "Dialogue package needs a portable JSON binding entry");
    try {
        auto reader = storage::IPackageReader::open(packagePath);
        if (!reader) return fail(error, "Dialogue package could not be opened");
        std::map<std::string, PinnedEntry, std::less<>> pinned;
        const auto pin = [&](const std::string& path, std::uint64_t limit) {
            const auto entry = reader->getEntry(path);
            if (!entry || entry->size > limit) return false;
            pinned.insert_or_assign(path, PinnedEntry{entry->size, entry->hash});
            return true;
        };
        if (!pin(bindingsEntry, kJsonLimit))
            return fail(error, "Dialogue binding entry missing or over budget");
        const auto bytes = reader->read(bindingsEntry);
        if (bytes.size() != pinned.at(bindingsEntry).size)
            return fail(error, "Dialogue binding entry read is incomplete");
        const auto decoded = progression::dialogue::decodeBindingsJson(
            std::string_view(bytes.empty() ? "" :
                reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (!decoded) return fail(error, decoded.error().detail);
        for (const auto& path : decoded->documents)
            if (!pin(path, kJsonLimit))
                return fail(error, "Published dialogue document missing or over budget: " + path);
        for (const auto& path : decoded->locales)
            if (!pin(path, kJsonLimit))
                return fail(error, "Published dialogue locale missing or over budget: " + path);
        for (const auto& [key, path] : decoded->voice) {
            (void)key;
            if (!pin(path, kVoiceLimit))
                return fail(error, "Published dialogue voice missing or over budget: " + path);
        }
        for (const auto& [key, path] : decoded->sequence) {
            (void)key;
            if (!pin(path, kSequenceLimit))
                return fail(error, "Published dialogue sequence missing or over budget: " + path);
        }
        DialoguePackageSource candidate;
        candidate._packagePath = std::move(packagePath);
        candidate._bindingsEntry = std::move(bindingsEntry);
        candidate._bindings = *decoded;
        candidate._pinned.swap(pinned);
        candidate._ready = true;
        _packagePath.swap(candidate._packagePath);
        _bindingsEntry.swap(candidate._bindingsEntry);
        std::swap(_bindings.schema, candidate._bindings.schema);
        _bindings.documents.swap(candidate._bindings.documents);
        _bindings.locales.swap(candidate._bindings.locales);
        _bindings.voice.swap(candidate._bindings.voice);
        _bindings.sequence.swap(candidate._bindings.sequence);
        _pinned.swap(candidate._pinned);
        std::swap(_ready, candidate._ready);
        if (error) error->clear();
        return true;
    } catch (const std::exception& ex) {
        return fail(error, ex.what());
    }
}

bool DialoguePackageSource::read(std::string_view path,
    std::vector<std::uint8_t>& out, std::string* error) const {
    if (!_ready) return fail(error, "Dialogue package is not open");
    const auto pinned = _pinned.find(path);
    if (pinned == _pinned.end())
        return fail(error, "Dialogue entry is not declared by the manifest");
    try {
        auto reader = storage::IPackageReader::open(_packagePath);
        if (!reader) return fail(error, "Dialogue package could not be reopened");
        const auto same = [&](std::string_view entryPath) {
            const auto expected = _pinned.find(entryPath);
            const auto current = reader->getEntry(std::string(entryPath));
            return expected != _pinned.end() && current &&
                current->size == expected->second.size &&
                current->hash == expected->second.hash;
        };
        if (!same(_bindingsEntry) || !same(path))
            return fail(error, "Dialogue package entry changed since binding");
        auto bytes = reader->read(std::string(path));
        if (bytes.size() != pinned->second.size)
            return fail(error, "Dialogue package entry read is incomplete");
        out.swap(bytes);
        if (error) error->clear();
        return true;
    } catch (const std::exception& ex) {
        return fail(error, ex.what());
    }
}

std::optional<progression::dialogue::Document>
DialoguePackageSource::loadDocument(std::string_view path,
    std::string* error) const {
    if (!_ready || std::find(_bindings.documents.begin(),
        _bindings.documents.end(), path) == _bindings.documents.end()) {
        fail(error, "Dialogue document is not bound");
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes;
    if (!read(path, bytes, error)) return std::nullopt;
    const auto decoded = progression::dialogue::decodeDocumentJson(
        std::string_view(bytes.empty() ? "" :
            reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!decoded) {
        fail(error, decoded.error().detail);
        return std::nullopt;
    }
    return *decoded;
}

bool DialoguePackageSource::loadLocale(std::string_view path,
    localization::Localization& target, std::string* error) const {
    if (!_ready || std::find(_bindings.locales.begin(),
        _bindings.locales.end(), path) == _bindings.locales.end())
        return fail(error, "Dialogue locale is not bound");
    std::vector<std::uint8_t> bytes;
    if (!read(path, bytes, error)) return false;
    const std::string_view json(bytes.empty() ? "" :
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!target.loadFromJson(json))
        return fail(error, "Published dialogue locale is invalid");
    return true;
}

std::shared_ptr<const CompiledSequence> DialoguePackageSource::loadSequence(
    std::string_view key, std::string* error) const {
    const auto path = _ready ? _bindings.sequencePath(key) : std::string_view{};
    if (path.empty()) {
        fail(error, "Dialogue sequence key is not bound");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    if (!read(path, bytes, error)) return {};
    SequenceResource resource;
    if (!resource.decode(bytes.data(), bytes.size())) {
        fail(error, "Published dialogue sequence is invalid");
        return {};
    }
    return resource.snapshot();
}

std::shared_ptr<resource::IAudio> DialoguePackageSource::loadVoice(
    std::string_view key, std::string* error) const {
    const auto path = _ready ? _bindings.voicePath(key) : std::string_view{};
    if (path.empty()) {
        fail(error, "Dialogue voice key is not bound");
        return {};
    }
    std::vector<std::uint8_t> bytes;
    if (!read(path, bytes, error)) return {};
    auto audio = std::make_shared<resource::Audio>();
    if (!audio->loadFromBinary(bytes.data(), bytes.size())) {
        fail(error, "Published dialogue voice is invalid");
        return {};
    }
    return audio;
}

DialogueSequenceResolver DialoguePackageSource::sequenceResolver() const {
    return [this](std::string_view key) { return loadSequence(key); };
}

DialogueVoicePlayer::Loader DialoguePackageSource::voiceLoader() const {
    return [this](const std::string& key) { return loadVoice(key); };
}

} // namespace ayt::sequence
