# AYSequence

## 灯光与发光点演出（2026-10-16）

LightShow.h提供独立的owned快照、灯具键/扫动/频闪、稳定ID点阵编队和绝对时间播放器。
LightShowAssetIO.h写严格schema4的.lshow JSON，读取schema1时出光口半径置零，
读取schema1/2时图案焦点清晰且光阑全开，读取schema1/2/3时Gobo贴图路径为空，AYIO原子保存。
灯具支持有限出光口半径、聚光核心半角和外层发散半角；默认窄角、无图案。
图案柔焦/光阑可关键帧插值。Texture Gobo使用灯具级相对贴图路径，
shadowSoftness[0,4]可关键帧插值，控制可选阴影边缘过滤半径。
Studio在Renderer创建纹理并按灯具ID绑定Presenter；路径须位于.lshow文件夹或子目录。
播放器可由外部绝对时钟同步；
启用AYAudio构建时，Studio可用Music...选择本地WAV/MP3/OGG，音乐播放头驱动灯光；
暂停、重启和时间拖动同步控制音乐。Clear music恢复独立预览时钟，--music可从命令行加载。
未启用AYAudio时，Studio仍可构建和独立播放，但--music会明确报错。
analyzeFormationClearance连续检查稳定ID编队在过渡中的最小间距，输出有界诊断。
planFormationTransition可为发生交叉的一段编队插入中间高度键，并再次连续检查间距；
Studio的Plan route按钮对当前时间所在段使用该功能。规划失败不会改写演出。
AssetIO的exportLightShowBundle把已保存演出、实际引用的Gobo图片和可选音乐复制到新建的
.lshowpkg文件夹，写bundle.json；缺少素材或超过单文件128MiB/总计512MiB时清理新文件夹。
Studio的Export show folder...导出当前节目，打开包内.lshow时自动读取包内音乐（需AYAudio）。
可选AYSequence::RendererIntegration提供LightShowPresenter；Windows的AYLightShow_Studio
支持真实渲染预览、时间跳转、灯具关键帧、编队键、调色和保存/撤销。
Studio顶部时间轴同屏显示灯具键、编队键和已载入音乐的抽样波形；点击或拖动时间轴
可暂停并定位播放头，随后用灯具滑条和Add / update key编辑当前时刻。
角度编辑以度显示，数据为弧度；--steps 8..64控制积分上限，低分辨率默认48，
无图案光束可按弦长自适应减少采样。
工作台提供航线间距检查、1080p自动质量预算，以及--point-budget、--volume-lights等手动覆盖。
Smoke box按钮或--smoke-box x y z halfX halfY halfZ可切换/配置局部雾盒。
Studio默认启用缓慢变化的密度纹理，Animated fog on/off按钮或--static-fog可作静态对照。
高分辨率默认保留最多10000可见点并优先GPU实例化；--point-cpu用于CPU四边形对照。
隐藏验收JSON同时报告CPU墙钟、Renderer GPU帧计时、Presenter/渲染耗时及实例化状态。
--preset single配合--scene air|floor|occluders提供单灯空中/落地/遮挡对照；--patterns显式打开图案示例。
Core不依赖Renderer/World，演出tick与呈现由调用者显式拥有。
能力边界、命令和预算见[演出系统](../../AYDocs/LIGHTING-SHOW-SYSTEM.md)；无自动cook/loader。

场景序列编排模块，独立于骨骼动画求值与编辑器产品层。

**当前状态：S1–S4 已实现，2026-09-29。** 独立 owned 快照求值、Once 播放器、
Transform/动画源时间/事件轨道、严格 JSON 和原子 loose `.seq` 保存加载（旧 `.ayseq` 可读取）；
单场景 Transform 绑定、独占控制与基态恢复、可选 Host 单一 tick 集成。
实际骨骼片段采样、姿势 lease/恢复与安全事件派发；独立作者核心/Editor 页面已接入，发布仍未交付，
无默认 Host 装配。

模块边界和播放契约见 [design.md](design.md)；连续实施顺序、优先级与验收见
[最小实现计划](../../AYDocs/SEQUENCE-IMPLEMENTATION-PLAN.md)。

## 仓库与构建

- 独立仓库位于 `AYRuntime/AYSequence`，使用 `main` 分支，并登记为根仓 submodule。
- 根工程使用 `AY_ENABLE_SEQUENCE=ON` 显式启用，默认 OFF；启用前需初始化 submodule。
- `AYSequence` / `AYSequence::Core` 是独立静态核心，无其他引擎 target 依赖。
  `AYSequenceAssetIO` / `AYSequence::AssetIO` 可选，依赖 AYIO/JSON，不是 Resource loader。
  `AYSequenceEntityIntegration` / `AYSequence::EntityIntegration` 可选，依赖
  Core、AYEntityCore、AYGameLoop；不依赖 AYScene、Renderer 或 AYAnimation。
  `AYSequenceAnimationIntegration` / `AYSequence::AnimationIntegration` 可选，依赖
  Entity integration 与 AYEntityAnimationIntegration/AYAnimation/AYResource。

可在配置好 C++ 编译器的环境中单独配置：

```powershell
cmake -S AYRuntime/AYSequence -B out/build/sequence-bootstrap -DAY_SEQUENCE_BUILD_ASSET_IO=OFF -DAY_SEQUENCE_BUILD_ENTITY_INTEGRATION=OFF -DAY_SEQUENCE_BUILD_ANIMATION_INTEGRATION=OFF
cmake --build out/build/sequence-bootstrap
```

## 预期依赖边界

运行时核心不依赖 AYEditor、AYUI、AYScene、AYRenderer、AYAudio 或 AYAnimation。
World/动画/音频/相机桥接由独立可选 integration targets 承担；UI-free 作者核心
通过 AYEditor 薄适配接入，复用已有公共作者控件。Entity integration 已实现；
骨骼 Animation integration、作者核心和 Editor extension 已实现；音频/相机 integration 尚未创建。

## S1 使用

`Sequence` 构造值数据，`CompiledSequence::compile` 返回校验后的不可变快照。
为 `SequencePlayer` 绑定快照，再使用 play/pause/stop/seek/advance；advance 输出
owned 属性样本、动画源时间和事件，不执行场景写入或用户回调。
可选 `SequenceAssetIO.h` 提供 encode/decode/load/save；显式路径由调用者确定，
原子保存通过 AYIO，尚未注册 ResourceManager loader。具体边界见设计和测试。

## S2 使用

链接 `AYSequence::EntityIntegration`，包含 `SceneSequenceSession.h`。宿主提供
已初始化的 World、已有实体和与 sequence bindings 一致的显式 ID 表：

```cpp
auto compiled = ayt::sequence::CompiledSequence::compile(authored);
auto session = std::make_shared<ayt::sequence::SceneSequenceSession>();
ayt::sequence::Diagnostic error;
if (session->bind(world, compiled,
                  {{"left", left->getId()}, {"right", right->getId()}}, &error)
    && session->play(&error)) {
    ayt::sequence::SceneFrame frame;
    session->advance(0.5, frame, &error); // 手动模式的唯一 tick owner。
    session->seek(2.0, frame, &error);    // 无事件；不重拍基态。
    session->stop();                     // 恢复仍未被外部改写的受控对象。
}
```

ID 只在当前 World 解析，不持久化、不冒充 GUID。对象删除、组件替换、清场及
World teardown 安全撤销绑定；重新使用需显式 bind。第一版独占整个 Transform，
拒绝 physics、Q16.16 `SimTransformComponent` 和 binary32 `DetSimTransformComponent`，
包括播放后动态添加的新权威；其他 gameplay writer 须宿主显式让出控制。所有权检测
不是 setter 锁；外部写入后不会用旧基态覆盖新值。无 animation driver 的 bind
仍拒绝动画轨道；S3 使用下面的可选适配。

Host 模式显式安装 `SequenceRuntimeModule`（requires `AYEntity.Runtime`），经其
已安装的 `SequenceSubSystem::addSession` 接入。Gameplay phase 先于 Entity.World，
Game clock/Scaled、每帧一次；不要另调手动 advance 或重复 World 更新。
默认 Host 不自动装配；最多 64 sessions。`takeFrames` 仅保留当帧 owned 输出，
派发每个事件前须检查 session 的 `isCurrent(generation)`；seek/stop/rebind/新一轮
播放/换 World 使旧批次失效。S3 提供下述显式安全派发器，不在 tick 内执行回调。

## S3 骨骼片段与事件

目标须先经既有角色加载流程准备好 loaded SkeletonComponent、skinMatrices 与
匹配 jointCount。链接 `AYSequence::AnimationIntegration`，使用宿主的资源服务：

```cpp
#include <AYSequence/AnimationSequenceDriver.h>
auto driver = ayt::sequence::makeResourceAnimationSequenceDriver(*host.resources()); // 须非空。
session->bind(world, compiled, entities, std::move(driver), &error);
session->play(&error);
ayt::sequence::SceneFrame frame;
session->advance(deltaSeconds, frame, &error); // Host 模式改为消费 takeFrames。
std::size_t delivered = 0;
ayt::sequence::dispatchSequenceEvents(session, std::move(frame),
    [](const ayt::sequence::Event& event) { /* 宿主按名称路由；无内嵌脚本 */ },
    delivered, &error);
```

资源根与 loaders 由宿主配置；适配通过 `resolveAssetPath` 后调用 ResourceManager，
Ready 状态才接受，绑定阶段复制 standard Animation 与 Skeleton 快照。也提供
`makeAnimationSequenceDriver(resolver)` 供测试/工具注入；resolver 只在 bind 调用，
不得改变 World 生命周期。Clip 后续 unload、cache eviction 或源 Clip 修改不会
影响当前快照；重新绑定才应用修改。目标 skeleton 原地变动则安全停止，不隐式重定向。

仅普通 Override 骨名 TRS：Linear/Step/CubicHermite，拒绝 Float/additive/未知骨名、
重复通道、坏数组/时间/Quaternion、非匹配实际 sourceDuration 与越界片段。
骨架 1..1024 bones、每 Clip 4096 tracks/100000 keys、合计键/切线 payload 64 MiB。
兼容验证基于目标拓扑与轨道骨名/类型；现有 `.ayanm` 不携带源骨架拓扑，不能据此
证明同名骨架的语义兼容。宿主须使用已针对目标骨架制作/烘焙的 Clip，不自动重定向。
适配用独立 AnimationPlayer setTime/evaluate，不 tick，不触发 Clip Notify；只有
Sequence 自身 event 轨道可派发。空档回到捕获基态，不沿用上一片段姿势。

`externalPoseOwner` 期间原 Animation/BlendSpace/StateMachine 都冻结，不修改原
播放器、组件资产字段或图状态；pause 保持序列姿势。stop/完成/析构恢复未被外部
改写的蒙皮矩阵并释放 lease，原控制器随后从原播放时间继续，而非追赶已过墙钟时间。
完成的终点样本是 owned 输出，完成即恢复，不承诺终点姿势再呈现一帧。

派发器按值持有 session、frame 和 sink；批次带 owner/batch/generation，拒绝重放、
跨 session、过期和嵌套派发。每个回调前检查实体与控制权；回调 stop/seek/rebind、
删除对象、换/关 World 后取消剩余事件；异常停止恢复并返回失败及已进入回调数。
Host 的事件须取出后显式派发，不在系统 tick 遍历 session 列表时调用用户代码。

## 验证记录（2026-09-29）

S3 full：AYSequence 65/65（fast 15 / integration 49 / stress 1）、AYAnimation 296/296，
合计 361/361；0 跳过/问题/漏报，inventory 无遗漏/重复。报告
`out/sequence-s3-commit-check/a92dfe5f8b3448f981cfbb8c22c6ca12/cases.json`。
AYScene / AYGameLoop 定向 CTest 均通过。SDK doctor：52 capabilities、47 modules、
11 search cases、57 entry symbols，0 errors / warnings。不是 Editor 视觉或全平台回归。
关闭序列骨骼适配，以及进一步关闭整个 AYAnimation，均通过基础序列 full 47/47；
独立 Core（三个 integration 选项 OFF）构建通过。裁剪报告：
`out/sequence-s3-no-runtime-animation/7c0ef9e721e9457faf1c3ce97aa32d38/cases.json`。

S2 full 回归：40/40 用例，fast 15 / integration 24 / stress 1；inventory 无遗漏/重复，
报告 `out/sequence-s2-final/74b9640b1c774b7c9a2d0690baa71ecd/cases.json`。
SDK doctor：52 capabilities、47 modules、11 search cases、55 entry symbols，
0 errors、0 warnings。AYScene / AYGameLoop 定向 CTest 均通过；真实 Host 主循环用例
验证序列写入先于 Entity.World 更新。上述不是 Editor 视觉或全引擎回归。

S1 full 回归：21/21 用例，fast 15 / integration 5 / stress 1；inventory 无遗漏/重复，
报告 `out/sequence-s1-final/c597bec64f554afeb6544455469742e6/cases.json`。
SDK doctor：52 capabilities、47 modules，0 errors、0 warnings。
独立 Core 在不链接其他引擎库时配置/编译通过；S0 历史记录保留如下。

- MSVC 独立 CMake 配置/构建通过；INTERFACE 目标没有编译单元，构建无任务属预期。
- umbrella header 的 C++23 语法编译检查通过。
- 根工程 `windows-headless-debug` 配置显式开启 `AY_ENABLE_SEQUENCE=ON` 通过，
  未开启 Animation、Audio、Device、Renderer 或 Editor，验证占位目标不强拉这些依赖。
- Agent SDK doctor：51 capabilities、47 modules，0 errors、0 warnings。
- 这些仅是初始化/构建检查，不是序列功能用例或完整引擎回归。

## S4 编辑

开启 `AY_ENABLE_SEQUENCE` 及 AssetIO/Entity/Animation integration 后构建 AYEditor。
File → New Scene Sequence 创建 Assets/sequences 下的 `.seq`，Content Browser 双击打开。
填写 Assets 相对 `.scn` 路径（旧 `.ayscene` 仍可读取），增加 binding 并指定唯一实体名称；选中 binding 后
增加 Position/Rotation/Scale 轨道或动画片段。事件轨道不绑定对象。
DopeSheet 拖动关键帧时间，右侧数值字段编辑分量/事件；section block 中部移动，
两端裁剪，也可数值修改 source offset/speed/duration。重叠/越界原子拒绝。
一次拖动一条撤销，取消恢复，Save 原子保存；重开保持全部 ID。独立场景的对象/
骨骼预览不触发脚本，不污染作者场景；只能读取已保存场景，不是完整蒙皮模型视口。
未设置 scenePath 时允许纯数值播放。设置定位后写 schema 2，旧 schema 1 可读。
`AYSequence::EditorCore` 只链接 Core，不依赖 UI/World，历史由宿主提供。

## Extended diagnostic observations (2026-10-15)

SceneSequenceSession::debugSnapshots enumerates only the requested World on its owner thread (64 sessions/256 bindings each) and copies playback, Transform ownership and active section observations. Optional independent event history defaults off and caps128/session; it records actual crossed events, then marks delivery after successful gameplay callbacks. Observation never advances playback or drains/replays SceneFrame. Clear and disabling only remove diagnostic events; the original sink, generation and once-only batch rules remain authoritative.

Regression sources are included. Runtime tests and interactive editor acceptance are deferred for this content task; untested changes remain uncommitted.

## 2026-10-16 main 合并兼容检查

Spatial 实体的 Transform 为不可移除的固有姿态。序列回归保留“实体删除后
原子失败、不留下部分写入”的检查，并独立验证被拒绝的 Transform 移除不会
使绑定失效：播放继续，停止后恢复基态。只调整旧测试假设，未修改运行时逻辑。
SceneSequenceTests：29/29 用例、179 断言通过。报告：
out/main-consolidation-20261016/sequence-accepted.json。

## 2026-10-18 关键帧编排与聚合照明

`AYSequence::EditorCore` 的 `LightShowAuthoring.h/editLightShowKeys` 对选定灯具ID和/或
编队键执行Move/Copy/Remove/Interpolation。源时间容差1e-6秒，缺少匹配键的轨道跳过，
至少一个键须命中；目标时间冲突、缺失/重复ID、越界或删除最后一个灯具键时整次拒绝。
先验证输入、暂存编辑再编译发布，输入输出可别名；不做I/O，不推进播放器，历史由宿主拥有。
Studio第三行提供Fixture/Formation lane、Previous/Next精确定位、目标秒数、移动/复制/删除
和Step/Linear/Smooth设置，沿用原有快照撤销重做。

Presenter默认仍关闭点阵照明。setPointLighting传入EmissivePointLightingSettings可启用
最多64个世界网格聚合点光；点阵显示maxPoints不影响聚合。灯具优先，共享256灯包与
表面预算，Point不产生体积/阴影；pointLightingStats报告的是准备量，实际提交看Renderer统计。
Studio的Point light on/off切换0/16；--point-lights 0..64、--point-light-cell、
--point-light-range、--point-light-scale设置预览参数，均为会话设置，不写schema4。
`setConstantFlux`/`--constant-flux`/Flux zoom on/off对所有灯具启用相同的远场角光通变焦
归一化；参考核心/外场半角2°/3°，表面与体积共享比例，Gobo/光阑仍吸收能量。
有限出光口近场、真实镜头焦面与棱镜尚未模拟。
