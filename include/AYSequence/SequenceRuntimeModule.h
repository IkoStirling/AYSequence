#pragma once
#include <AYSequence/SceneSequenceSession.h>
#include <AYGameLoop/SubSystemModule.h>
#include <functional>
#include <limits>

namespace ayt::sequence {
using WorldProvider = std::function<ayt::entity::World*()>;
struct HostedSceneFrame { std::shared_ptr<SceneSequenceSession> session; SceneFrame frame; };

/// Optional Host adapter. Sole tick lease, no World ownership. Outputs are
/// drained by the host; callbacks/animation pose application are not installed.
class SequenceSubSystem final : public ayt::game::ISubSystem {
public:
    explicit SequenceSubSystem(WorldProvider provider);
    ~SequenceSubSystem() override;
    SequenceSubSystem(const SequenceSubSystem&) = delete;
    SequenceSubSystem& operator=(const SequenceSubSystem&) = delete;
    const char* getName() const override { return "Sequence"; }
    const ayt::game::SubSystemDescriptor& getDescriptor() const override;
    bool initialize() override;
    void update(float) override {} // Only explicit Gameplay phase advances sessions.
    void fixedUpdate(float) override {}
    void tick(ayt::game::FramePhase phase, const ayt::game::FrameContext& context) override;
    void shutdown() override;
    bool addSession(std::shared_ptr<SceneSequenceSession> session, Diagnostic* error = nullptr);
    void removeSession(const std::shared_ptr<SceneSequenceSession>& session);
    /// Drain this frame's outputs; do not execute stale batches after stop/rebind.
    std::vector<HostedSceneFrame> takeFrames();
private:
    WorldProvider _provider;
    std::vector<std::shared_ptr<SceneSequenceSession>> _sessions;
    std::vector<HostedSceneFrame> _frames;
    std::uint64_t _lastFrame = std::numeric_limits<std::uint64_t>::max();
    bool _initialized = false;
};

/// Explicit optional startup node AYSequence.Runtime, requires AYEntity.Runtime.
/// Not installed by default. Default provider resolves the active World per tick.
class SequenceRuntimeModule final : public ayt::game::SubSystemModule {
public:
    explicit SequenceRuntimeModule(WorldProvider provider = {});
    SequenceRuntimeModule(const SequenceRuntimeModule&) = delete;
    SequenceRuntimeModule& operator=(const SequenceRuntimeModule&) = delete;
};
} // namespace ayt::sequence
