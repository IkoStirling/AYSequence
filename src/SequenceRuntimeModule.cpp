#include <AYSequence/SequenceRuntimeModule.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityRuntimeModule.h>
#include <utility>

namespace ayt::sequence {
SequenceRuntimeModule::SequenceRuntimeModule(WorldProvider provider)
    : SubSystemModule(
        ayt::module::ModuleDescriptor{.id="AYSequence.Runtime", .displayName="Scene Sequence", .version="0.1.0",
            .dependencies={ayt::module::ModuleDependency::required(std::string(ayt::entity::kEntityRuntimeModuleId))}},
        "Sequence", [provider=std::move(provider)]() mutable {
            return std::make_unique<SequenceSubSystem>(provider ? provider : WorldProvider([] { return &ayt::entity::World::instance(); }));
        }) {}
SequenceSubSystem::SequenceSubSystem(WorldProvider provider) : _provider(std::move(provider)) {}
SequenceSubSystem::~SequenceSubSystem() { shutdown(); }
const ayt::game::SubSystemDescriptor& SequenceSubSystem::getDescriptor() const {
    static const ayt::game::SubSystemDescriptor descriptor{
        .name="Sequence", .timeType=ayt::game::SubSystemDescriptor::TimeType::Scaled,
        .phases=ayt::game::phaseBit(ayt::game::FramePhase::Gameplay), .clock=ayt::game::ClockDomain::Game,
        .initializeAfter={"Entity"}, .phasePriority=1000, .writes={"Simulation.World"}};
    return descriptor;
}
bool SequenceSubSystem::initialize() { _lastFrame = std::numeric_limits<std::uint64_t>::max(); _initialized = static_cast<bool>(_provider); return _initialized; }
std::vector<HostedSceneFrame> SequenceSubSystem::takeFrames() { return std::exchange(_frames,{}); }
} // namespace ayt::sequence
