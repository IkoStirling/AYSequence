#pragma once
#include <AYSequence/SceneAnimationDriver.h>
#include <functional>

namespace ayt::resource { class Animation; class ResourceManager; class ISkeleton; }
namespace ayt::sequence {
/// Structural preflight shared by driver and authoring preview. Call before
/// initializing an AnimationPlayer from untrusted skeleton data. Does not sample.
bool validateAnimationSequenceSkeleton(const ayt::resource::ISkeleton& skeleton, Diagnostic* error = nullptr);
/// Resolver runs only during bind on the main thread, never during sampling.
/// The driver validates and copies standard Animation assets into immutable
/// snapshots; no borrowed mutable Clip, cache, callback or Notify is retained.
using AnimationResolver = std::function<std::shared_ptr<const ayt::resource::Animation>(const std::string&)>;
std::unique_ptr<SceneAnimationDriver> makeAnimationSequenceDriver(AnimationResolver resolver);
/// Use the host's resources(), with loaders/root already configured. Reject
/// failed/pending/placeholder loads. Manager is borrowed through bind only.
std::unique_ptr<SceneAnimationDriver> makeResourceAnimationSequenceDriver(ayt::resource::ResourceManager& resources);
} // namespace ayt::sequence
