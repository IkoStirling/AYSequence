#include <AYSequence/AnimationSequenceDriver.h>
#include <AYAnimation/AnimationPlayer.h>
#include <AYEntity/World.h>
#include <AYEntity/EntityImpl.h>
#include <AYEntity/components/SkeletonComponent.h>
#include <AYResource/ResourceManager.h>
#include <AYResource/AssetPath.h>
#include <AYResource/assetsImpl/Animation.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace ayt::sequence {
namespace {
using namespace ayt::resource;
using ayt::entity::SkeletonComponent;
using Matrix = ayt::math::Float4x4;
bool finite(float v) { return std::isfinite(v) && std::abs(v) <= 1e6f; }
bool matrixFinite(const Matrix& m) {
    for (const auto& row : m.row) if (!finite(row.x) || !finite(row.y) || !finite(row.z) || !finite(row.w)) return false;
    return true;
}
bool sameMatrix(const Matrix& a, const Matrix& b) { return std::memcmp(&a,&b,sizeof(Matrix)) == 0; }
bool sameBone(const Bone& a, const Bone& b) {
    const auto vec = [](const auto& x, const auto& y) { return x.x==y.x && x.y==y.y && x.z==y.z; };
    return a.name==b.name && a.parentIndex==b.parentIndex && sameMatrix(a.inverseBindMatrix,b.inverseBindMatrix)
        && vec(a.localPosition,b.localPosition) && vec(a.localScale,b.localScale)
        && vec(a.localRotation,b.localRotation) && a.localRotation.w==b.localRotation.w;
}
bool validBone(const Bone& b, std::size_t index) {
    const auto& p=b.localPosition; const auto& q=b.localRotation; const auto& s=b.localScale;
    const double norm=double(q.x)*q.x+double(q.y)*q.y+double(q.z)*q.z+double(q.w)*q.w;
    return !b.name.empty() && b.name.size()<=128 && b.name.find('\0')==std::string::npos
        && b.parentIndex>=-1 && (b.parentIndex<0 || std::size_t(b.parentIndex)<index)
        && finite(p.x) && finite(p.y) && finite(p.z) && finite(q.x) && finite(q.y) && finite(q.z) && finite(q.w)
        && std::abs(norm-1)<=1e-4 && finite(s.x) && finite(s.y) && finite(s.z)
        && s.x>=1e-6f && s.y>=1e-6f && s.z>=1e-6f && matrixFinite(b.inverseBindMatrix);
}
class Driver final : public SceneAnimationDriver {
    struct Clip {
        std::shared_ptr<const Animation> asset;
        std::unique_ptr<ayt::anim::AnimationPlayer,ayt::anim::AnimationPlayerDeleter> player;
    };
    struct Target {
        Target() = default;
        Target(const Target&) = delete;
        Target& operator=(const Target&) = delete;
        Target(Target&&) noexcept = default;
        Target& operator=(Target&&) noexcept = default;
        std::string binding, path;
        ayt::entity::EntityHandle handle;
        SkeletonComponent* identity = nullptr;
        std::shared_ptr<Skeleton> source, snapshot;
        std::map<std::string,Clip> clips;
        Matrix* buffer = nullptr;
        ayt::anim::AnimationPlayer* originalPlayer = nullptr;
        float originalTime = 0;
        bool originalPaused = false, held = false, dirty = false;
        std::vector<Matrix> baseline, expected, pending;
    };
    AnimationResolver _resolver;
    ayt::entity::World* _world = nullptr;
    std::vector<Target> _targets;
    SkeletonComponent* component(const Target& t) const {
        if (!_world || _world->getEntityHandle(t.handle.id)!=t.handle || !_world->findEntity(t.handle.id)) return nullptr;
        auto* store=_world->getStorage<SkeletonComponent>();
        return store ? static_cast<SkeletonComponent*>(store->get(t.handle.id)) : nullptr;
    }
    bool unchanged(const Target& t, bool pose) const {
        const auto* c=component(t);
        if (!c || c!=t.identity || c->layoutMagic!=SkeletonComponent::kLayoutMagic || !c->loaded
            || c->skeleton.get()!=t.source.get() || c->skeletonPath!=t.path
            || c->jointCount!=t.snapshot->getBoneCount() || !c->skinMatrices
            || c->skeleton->getBoneCount()!=t.snapshot->getBoneCount()) return false;
        for (std::size_t i=0;i<c->jointCount;++i)
            if (!sameBone(c->skeleton->getBones()[i],t.snapshot->getBones()[i])) return false;
        if (!pose) return true;
        if (c->externalPoseOwner!=this || c->skinMatrices!=t.buffer || c->player.get()!=t.originalPlayer
            || (c->player && (c->player->getTime()!=t.originalTime || c->player->isPaused()!=t.originalPaused))) return false;
        for (std::size_t i=0;i<c->jointCount;++i) if (!sameMatrix(c->skinMatrices[i],t.expected[i])) return false;
        return true;
    }
    static bool fail(Diagnostic* e, const std::string& where, const char* message) {
        if (e) *e={where,message}; return false;
    }
    static std::shared_ptr<Animation> snapshotClip(const Animation& src, const Skeleton& skeleton,
                                                  std::size_t& bytes, Diagnostic* e, const std::string& path) {
        const auto& tracks=src.getTracks();
        if (!finite(src.getDuration()) || src.getDuration()<=0 || src.getDuration()>86400
            || !finite(src.getTicksPerSecond()) || src.getTicksPerSecond()<=0 || tracks.empty() || tracks.size()>4096)
            { fail(e,path,"Invalid Clip duration, ticksPerSecond or track limit"); return {}; }
        auto out=std::make_shared<Animation>(); out->setDuration(src.getDuration()); out->setTicksPerSecond(src.getTicksPerSecond());
        std::set<std::pair<std::string,std::string>> channels;
        std::size_t keys=0;
        for (const auto& t : tracks) {
            const bool rotation=t.property=="rotation";
            const std::size_t width=rotation ? 4 : 3;
            if (t.nodeName.size()>128 || skeleton.findBone(t.nodeName.c_str())<0 || t.nodeName.find('\0')!=std::string::npos
                || (t.property!="position" && t.property!="rotation" && t.property!="scale")
                || t.valueType!=(rotation ? AnimTrackType::Quaternion : AnimTrackType::Vector3)
                || t.blendMode!=AnimBlendMode::Override || !channels.emplace(t.nodeName,t.property).second
                || t.times.empty() || t.times.size()>100000 || (keys+=t.times.size())>100000
                || t.values.size()!=t.times.size()*width
                || (t.interpolation!=AnimInterpolation::Linear && t.interpolation!=AnimInterpolation::Step && t.interpolation!=AnimInterpolation::CubicHermite)
                || (t.interpolation==AnimInterpolation::CubicHermite && (t.inTangents.size()!=t.values.size() || t.outTangents.size()!=t.values.size()))
                || (!t.inTangents.empty() && t.inTangents.size()!=t.values.size()) || (!t.outTangents.empty() && t.outTangents.size()!=t.values.size()))
                { fail(e,path,"Incompatible bone, unsupported channel/blend or malformed key/tangent arrays"); return {}; }
            for (std::size_t i=0;i<t.times.size();++i) {
                const double time=double(t.times[i])/src.getTicksPerSecond();
                if (!std::isfinite(time) || time<0 || time>src.getDuration()+1e-6 || (i && t.times[i]<=t.times[i-1]))
                    { fail(e,path,"Invalid or unsorted Clip key time"); return {}; }
                double norm=0;
                for (std::size_t j=0;j<width;++j) {
                    const float v=t.values[i*width+j]; norm+=double(v)*v;
                    if (!finite(v) || (t.property=="scale" && v<1e-6f)) { fail(e,path,"Invalid Clip value"); return {}; }
                }
                if (rotation && std::abs(norm-1)>1e-4) { fail(e,path,"Non-unit Clip Quaternion"); return {}; }
            }
            for (const auto* tangent : {&t.inTangents,&t.outTangents})
                for (float v : *tangent) if (!finite(v)) { fail(e,path,"Non-finite tangent"); return {}; }
            bytes+=(t.times.size()+t.values.size()+t.inTangents.size()+t.outTangents.size())*sizeof(float);
            if (bytes>64*1024*1024) { fail(e,path,"Aggregate Clip snapshot budget exceeds 64 MiB"); return {}; }
            out->addTrack(t);
        }
        return out;
    }
public:
    explicit Driver(AnimationResolver resolver) : _resolver(std::move(resolver)) {}
    bool prepare(ayt::entity::World& w, const CompiledSequence& sequence,
                 const std::map<std::string,std::uint32_t>& ids, Diagnostic* e) override {
        if (!_resolver) return fail(e,"animation","Missing animation resolver");
        _world=&w;
        std::map<std::string,std::shared_ptr<const Animation>> assets;
        std::size_t bytes=0;
        for (const auto& track : sequence.data().animations) {
            Target t; t.binding=track.binding; t.handle=w.getEntityHandle(ids.at(track.binding));
            auto* c=component(t);
            if (!c || c->layoutMagic!=SkeletonComponent::kLayoutMagic || !c->loaded || !c->skeleton || !c->skinMatrices
                || !c->jointCount || c->jointCount>1024 || c->jointCount!=c->skeleton->getBoneCount())
                return fail(e,track.binding,"Target must have a prepared loaded SkeletonComponent (1..1024 bones)");
            t.identity=c; t.source=c->skeleton; t.path=c->skeletonPath; t.snapshot=std::make_shared<Skeleton>();
            if (!validateAnimationSequenceSkeleton(*c->skeleton, e)) return false;
            t.snapshot->setBoneCount(c->jointCount);
            for (std::size_t i=0;i<c->jointCount;++i) {
                const auto& b=c->skeleton->getBones()[i];
                t.snapshot->setBone(i,b);
            }
            t.pending.resize(c->jointCount); t.expected.resize(c->jointCount); t.baseline.resize(c->jointCount);
            for (const auto& section : track.sections) {
                if (!t.clips.contains(section.source)) {
                    auto& src=assets[section.source];
                    try { if (!src) src=_resolver(section.source); }
                    catch (...) { return fail(e,section.source,"Clip resolver threw during preparation"); }
                    if (!src) return fail(e,section.source,"Clip load failed, pending or placeholder");
                    auto snapshot=snapshotClip(*src,*t.snapshot,bytes,e,section.source); if (!snapshot) return false;
                    Clip clip; clip.asset=std::move(snapshot); clip.player=ayt::anim::AnimationPlayer::create();
                    clip.player->setSkeleton(t.snapshot); clip.player->play(clip.asset.get()); clip.player->setLoop(false);
                    t.clips.emplace(section.source,std::move(clip));
                }
                const double actual=t.clips.at(section.source).asset->getDuration();
                const double tolerance=std::max(1e-6,actual*1e-6);
                if (std::abs(actual-section.sourceDuration)>tolerance || section.sourceOffset+(section.end-section.start)*section.speed>actual+tolerance)
                    return fail(e,section.id,"Declared source duration/range differs from actual Clip");
            }
            _targets.push_back(std::move(t));
        }
        _resolver={};
        return check(e);
    }
    bool acquire(Diagnostic* e) override {
        if (!check(e)) return false;
        for (const auto& t : _targets) {
            const auto* c=component(t);
            if (c->externalPoseOwner && c->externalPoseOwner!=this) return fail(e,t.binding,"Another controller owns skeletal pose");
        }
        for (auto& t : _targets) if (!t.held) {
            auto* c=component(t); t.buffer=c->skinMatrices; t.originalPlayer=c->player.get();
            t.originalTime=c->player ? c->player->getTime() : 0; t.originalPaused=c->player && c->player->isPaused();
            std::copy_n(c->skinMatrices,c->jointCount,t.baseline.begin()); t.expected=t.baseline;
            c->externalPoseOwner=this; t.held=true; t.dirty=false;
        }
        return true;
    }
    bool check(Diagnostic* e) override {
        for (const auto& t : _targets) if (!unchanged(t,t.held)) return fail(e,t.binding,"Skeleton target, asset, lease or pose changed externally");
        return true;
    }
    bool sample(const Evaluation& evaluation, Diagnostic* e) override {
        if (!check(e)) return false;
        for (auto& t : _targets) {
            // Gaps are deterministic: the captured pre-sequence baseline, never
            // the last section's pose. No clip Notify or Float sink is evaluated.
            t.pending=t.baseline;
            for (const auto& sample : evaluation.animations) if (sample.binding==t.binding) {
                auto& clip=t.clips.at(sample.source); clip.player->setTime(static_cast<float>(sample.sourceTime)); clip.player->evaluate();
                std::copy_n(clip.player->getBoneSkinMatrices(),t.pending.size(),t.pending.begin());
            }
            for (const auto& matrix : t.pending) if (!matrixFinite(matrix)) return fail(e,t.binding,"Non-finite/out-of-budget sampled pose");
        }
        return true;
    }
    void apply() noexcept override {
        for (auto& t : _targets) { std::copy(t.pending.begin(),t.pending.end(),t.buffer); std::copy(t.pending.begin(),t.pending.end(),t.expected.begin()); t.dirty=true; }
    }
    void release() noexcept override {
        for (auto& t : _targets) if (t.held) {
            auto* c=component(t);
            if (c && c==t.identity && c->externalPoseOwner==this) {
                if (t.dirty && unchanged(t,true)) std::copy(t.baseline.begin(),t.baseline.end(),t.buffer);
                c->externalPoseOwner=nullptr;
            }
            t.held=false; t.dirty=false;
        }
    }
};
} // namespace
bool validateAnimationSequenceSkeleton(const ayt::resource::ISkeleton& skeleton, Diagnostic* error) {
    if (error) *error = {};
    const auto count = skeleton.getBoneCount();
    if (!count || count > 1024 || !skeleton.getBones()) {
        if (error) *error = {"skeleton", "Expected 1..1024 readable bones"};
        return false;
    }
    std::set<std::string> names;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& bone = skeleton.getBones()[i];
        if (!validBone(bone, i) || !names.insert(bone.name).second) {
            if (error) *error = {"skeleton", "Invalid skeleton names, topology or rest transforms"};
            return false;
        }
    }
    return true;
}
std::unique_ptr<SceneAnimationDriver> makeAnimationSequenceDriver(AnimationResolver resolver) {
    return std::make_unique<Driver>(std::move(resolver));
}
std::unique_ptr<SceneAnimationDriver> makeResourceAnimationSequenceDriver(ayt::resource::ResourceManager& resources) {
    return makeAnimationSequenceDriver([&resources](const std::string& path) -> std::shared_ptr<const Animation> {
        const auto resolved=ayt::resource::resolveAssetPath({},path);
        auto clip=resources.load<Animation>(resolved);
        return resources.getLoadState(resolved)==ResourceLoadState::Ready ? clip : nullptr;
    });
}
} // namespace ayt::sequence
