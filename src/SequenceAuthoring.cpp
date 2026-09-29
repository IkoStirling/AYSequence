#include <AYSequence/SequenceAuthoring.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace ayt::sequence {
namespace {
void sortRows(Sequence &s) {
    for (auto &t : s.transforms)
        std::stable_sort(t.keys.begin(), t.keys.end(),
                         [](const auto &a, const auto &b) { return a.time < b.time; });
    for (auto &t : s.animations)
        std::stable_sort(t.sections.begin(), t.sections.end(),
                         [](const auto &a, const auto &b) { return a.start < b.start; });
    for (auto &t : s.eventTracks)
        std::stable_sort(t.events.begin(), t.events.end(),
                         [](const auto &a, const auto &b) { return a.time < b.time; });
}
std::unordered_set<std::string> allIds(const Sequence &s) {
    std::unordered_set<std::string> ids{s.id};
    for (auto &b : s.bindings)
        ids.insert(b.id);
    for (auto &t : s.transforms) {
        ids.insert(t.id);
        for (auto &k : t.keys)
            ids.insert(k.id);
    }
    for (auto &t : s.animations) {
        ids.insert(t.id);
        for (auto &k : t.sections)
            ids.insert(k.id);
    }
    for (auto &t : s.eventTracks) {
        ids.insert(t.id);
        for (auto &k : t.events)
            ids.insert(k.id);
    }
    return ids;
}
} // namespace
SequenceAuthoring::SequenceAuthoring() {
    Sequence s;
    s.id = "sequence";
    _snapshot = CompiledSequence::compile(std::move(s));
}
bool SequenceAuthoring::replace(Sequence s, Diagnostic *error) {
    if (_editing) {
        if (error)
            *error = {"edit", "Nested authoring mutation rejected"};
        return false;
    }
    if (s == _snapshot->data()) {
        if (error)
            *error = {};
        return true;
    }
    auto next = CompiledSequence::compile(std::move(s), error);
    if (!next)
        return false;
    _snapshot = std::move(next);
    ++_revision;
    return true;
}
bool SequenceAuthoring::edit(const std::function<bool(Sequence &)> &mutation, Diagnostic *error) {
    if (_editing) {
        if (error)
            *error = {"edit", "Nested authoring mutation rejected"};
        return false;
    }
    Sequence staged = _snapshot->data();
    struct Guard {
        bool &flag;
        ~Guard() { flag = false; }
    } guard{_editing};
    _editing = true;
    try {
        if (!mutation || !mutation(staged)) {
            if (error)
                *error = {"edit", "Edit rejected or item no longer exists"};
            return false;
        }
        _editing = false;
        return replace(std::move(staged), error);
    } catch (...) {
        if (error)
            *error = {"edit", "Edit threw; original snapshot retained"};
        return false;
    }
}
std::string SequenceAuthoring::nextId(const std::string &prefix) const {
    const auto ids = allIds(_snapshot->data());
    for (std::uint64_t n = 1;; ++n) {
        auto id = prefix + "." + std::to_string(n);
        if (!ids.contains(id))
            return id;
    }
}
bool updateSequenceKey(Sequence &s, const std::string &id, double time, Value value) {
    for (auto &t : s.transforms)
        for (auto &k : t.keys)
            if (k.id == id) {
                k.time = time;
                k.value = value;
                sortRows(s);
                return true;
            }
    return false;
}
bool moveSequenceItems(Sequence &s, const std::vector<std::string> &ids, double delta) {
    if (!std::isfinite(delta) || ids.empty())
        return false;
    std::unordered_set<std::string> remaining(ids.begin(), ids.end());
    if (remaining.size() != ids.size())
        return false;
    for (auto &t : s.transforms)
        for (auto &k : t.keys)
            if (remaining.erase(k.id))
                k.time += delta;
    for (auto &t : s.eventTracks)
        for (auto &k : t.events)
            if (remaining.erase(k.id))
                k.time += delta;
    if (!remaining.empty())
        return false;
    sortRows(s);
    return true;
}
bool removeSequenceItems(Sequence &s, const std::vector<std::string> &ids) {
    std::unordered_set<std::string> remaining(ids.begin(), ids.end());
    if (remaining.empty() || remaining.size() != ids.size())
        return false;
    for (auto &t : s.transforms)
        std::erase_if(t.keys, [&](const auto &k) { return remaining.erase(k.id) != 0; });
    for (auto &t : s.animations)
        std::erase_if(t.sections, [&](const auto &k) { return remaining.erase(k.id) != 0; });
    for (auto &t : s.eventTracks)
        std::erase_if(t.events, [&](const auto &k) { return remaining.erase(k.id) != 0; });
    std::erase_if(s.transforms, [](const auto &t) { return t.keys.empty(); });
    std::erase_if(s.animations, [](const auto &t) { return t.sections.empty(); });
    return remaining.empty();
}
bool moveSequenceSection(Sequence &s, const std::string &id, double start) {
    for (auto &t : s.animations)
        for (auto &k : t.sections)
            if (k.id == id) {
                k.end += start - k.start;
                k.start = start;
                sortRows(s);
                return true;
            }
    return false;
}
bool trimSequenceSection(Sequence &s, const std::string &id, double start, double end) {
    for (auto &t : s.animations)
        for (auto &k : t.sections)
            if (k.id == id) {
                k.sourceOffset += (start - k.start) * k.speed;
                k.start = start;
                k.end = end;
                sortRows(s);
                return true;
            }
    return false;
}
} // namespace ayt::sequence
