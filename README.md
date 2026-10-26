# AYSequence

## 项目发布接线（2026-10-26）

启用 Sequence 与 AYResource 作者构建时，ProjectBuild 自动把选为 Cook 的
`.seq` 转成 `.sqc`。场景、动画、音频引用必须同时作为 Assets 中的原路径资源
发布；缺失、排除或另行转换该路径会在规划期被拒绝。依赖文件字节参与序列
缓存键，构建执行前与暂存后会重检内容签名。CookShip 也会校验 `.sqc` 的
包封和引用闭包，失败保留原包。`AYSequenceCookTool` 仍可单独用于离线转换。
该流程不负责把字幕键解析为文本，也不自动启动声音或场景播放。

## 文档预览生命周期（2026-10-20）

LightShowPresenter::releaseResources(renderer) 为 Editor 关闭文档显式回收点阵、出光口、
灯壳与雾 atlas 缓存；先解绑所有 packet，使用同一存活设备，重复调用无副作用。
不销毁调用者绑定的 Gobo/IES 纹理，选项可在同一设备上再次 append；设备重建应重建 Presenter。
主编辑器的 .lshow 页现提供主 Deferred、多轨/灯组/光学与雾关键帧、编队、预算HUD和JSON切换。
Studio 保留音乐/波形、密度笔刷与资源包导出。AssetIO 当前写 schema8，兼容读取1..7。


## 灯光与发光点演出（2026-10-16）

LightShow.h提供独立的owned快照、灯具键/扫动/频闪、稳定ID点阵编队和绝对时间播放器。
LightShowAssetIO.h写严格schema8的.lshow JSON，读取schema1时出光口半径置零，
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
骨骼 Animation integration、作者核心、Editor extension、可选音频预载适配及 3D 相机租约已实现。

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

### 镜头切点与音频提示

`Sequence::cameraCuts` 选择已声明的绑定；对应摄像机的位置/旋转仍可用原有
Transform 轨道制作。求值输出当前切点的 `cutId`、`binding` 和作者设定的
`blendSeconds`；纯求值本身不修改主相机状态。
`SceneSequenceSession::enableCameraCuts(viewportId)` 可显式接入 World-owned 3D
相机选择。绑定的实体须有目标 viewport 的 `PerspectiveCameraComponent`；播放、
定位和逐步求值使用切点的相机及过渡秒数，停止、完成、重绑与 World 销毁会
释放租约并恢复原有选镜。不同序列不能同时租用同一 viewport。未启用时仍只
返回纯求值结果，游戏也可自管镜头。
`audioCues` 记录便携 `.aud` 路径、时间和增益，播放越过
时间点时在 owned `PlaybackFrame` 中出现。seek、暂停和零时间步不发声。
`dispatchSequenceCues` 将事件和音频提示按时间派发，同时间点先事件后音频，
保留 SceneSession 的一次性批次与 World 生命周期校验。可选
`AYSequence::AudioIntegration` 的 `SequenceAudioPlayer` 在播放前通过宿主提供的
loader 预载 `.aud`、去重注册到 AYAudio，派发时只查找并播放。宿主可把
`player.play(cue)` 作为音频 sink，停止或 seek 时调用 `player.stop()`；重载
序列时重新 `prepare()`。它最多拥有 256 个不同资源和 256 个活跃声音，失败
会清空本次预载。`AudioSubSystem` 必须比适配器存活更久；设备重启后须重新预载。
游戏负责选择资源根、播放总线与报错策略。

加入任一新轨道时 `.seq` 写为 schema 3，仍可读 schema 1/2。通用
`SequenceAuthoring::edit` 及移动/删除辅助函数支持这两类作者数据；当前 AYEditor
序列页面尚无专门的镜头/音频轨道控件，可由外部作者工具通过同一数据模型生成。

### 本地化文字区间

`Sequence::textCues` 提供带 `textKey`、可选 `speakerKey` 和 `[start,end)`
区间的本地化文字提示。`CompiledSequence::evaluate` 与每个播放帧都返回当前
有效的 owned 提示；seek、暂停或零时间步也可恢复正确字幕，区间结束即消失。
文字键的本地化查询、字体与 UI 绘制由游戏负责。含文字提示的 `.seq` 写
schema 4；schema 1/2/3 保持可读。作者编辑可移动/删除文字提示，Cooked
`.sqc` 与 ResourceManager 加载走相同校验。当前编辑器尚无专门的字幕轨道控件。

可选 `AYSequence::UIIntegration` 提供 `SequenceTextPresenter`：将游戏创建并持有的
最多 16 组 AYUI 说话人/正文 TextLabel 绑定到当前 `Evaluation::textCues`，经
AYLocalization 解析键并更新文字与可见性。播放帧、暂停、直接 seek 或切换语言后
都可重新应用同一求值；区间结束会隐藏旧标签。多条重叠提示按作者顺序使用多个
槽位，槽位不足时整体拒绝并保留当前 UI。缺失翻译显示稳定键以便内容排查。
游戏仍负责布局、字体样式、播放时钟与 UI 树生命周期。

可选 `AYSequence::DialogueIntegration` 的 `DialogueSequencePlayback` 可作为
`AYProgression::dialogue::PresentationController` 的 sink 一部分：宿主以
`sequenceCueKey` 解析已编译序列，适配器在新节点启动一次性 `SequencePlayer`，
对相同交付键不重启；解析失败保留旧播放并可重试。读档呈现帧不重播序列，
空键会停止旧序列。宿主以单一时钟调用 `advance`，再将求值/事件/音频提示
交给现有 Scene、UI 与音频接口；此桥不绑定 World 实体或自动提交镜头。
同时启用 AYAudio/AYResource 时，`AYSequence::DialogueAudioIntegration` 的
`DialogueVoicePlayer` 可从同一交付帧解析 `voiceCueKey` 并在 Voice 总线播放。
切换节点会停止旧声音并释放旧 clip；同键不重播，资源暂缺时保留旧播放并
允许重试。读档帧只清除旧声音，设备重启后旧句柄失效且可重新加载。
宿主负责资源键到音频资产的映射、字幕同步和 AudioSubSystem 生命周期。

### 可选 ResourceManager 加载

根构建同时启用 AYResource 与 Sequence AssetIO 时，链接
`AYSequence::Resources`，调用 `registerSequenceResourceLoader()` 后可使用
`ResourceManager::load<SequenceResource>(path)` 获取缓存的已验证编译快照。
注册 `.seq`、旧 `.ayseq` 只读别名及运行时 `.sqc`。加载器可对传入的 JSON 或
`.sqc` 字节执行相同校验；
重载失败保留调用方当前 `SequenceResource` 中的旧快照，旧 `CompiledSequence`
拷贝在成功重载或缓存驱逐后仍有效。`reloadSequenceResource(path)` 用于清除管理器
的缓存/失败状态并重试散文件。此接线不自动预加载依赖的动画或音频资源，也不
自动转换 `.seq`。构建侧可先用
`buildSequenceDependencyManifest(sequence, assetsRoot, output)` 核对场景、动画和音频
引用均位于 Assets 内的普通文件，并得到去重路径、大小、内容指纹及整体签名。
发布前应重算以识别计划后的修改。`AYSequenceCookTool source.seq Assets-root
output.sqc` 将严格作者文件转换为有界、带校验和及依赖内容签名的 `.sqc`；编码
失败不替换已有输出。启用资源作者构建时，ProjectBuild 可将选为 Cook 的
`.seq` 转为同相对路径 `.sqc`，并对所引用的场景、动画与音频执行发布闭包校验；
CookShip 在替换包前复核 `.sqc` 及依赖。签名用于内容识别，
不是可信认证或运行时依赖存在性证明。

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

## 2026-10-19 正视灯具预览

Presenter的`setFixtureEmitters(true, settings)`可启用Renderer可见发光口，默认关闭以保留宿主
原行为。它使用同一求值帧的灯具位置、Pan/Tilt、RGB、强度/频闪、口径和iris；
setConstantFlux会同时影响表面光、体积与灯口。读取Renderer当前烟雾设置做灯口中心视线透射。
不写.lshow，不改变schema4，也不生成ECS组件或每灯材质；关闭不保留旧批次到新场景。

Studio默认开启，Emitters on/off切换；Source front/side/back对准首灯，便于核对背面不发光。
--preset source建立固定单灯发光口对照，--view source-front|source-side|source-back选择视角；
--scene source-occluded|source-half提供完整/半遮挡。--no-emitters禁用，--emitter-scale和
--emitter-glow分别设置HDR标定比例与微弱侧向辉光。隐藏JSON的emitterPrepared/emitterBackFacing/emitterClamped
报告CPU准备、背向和HDR夹取数量，不代表深度遮挡后的实际可见灯口数。
发光口是近似圆盘；不模拟灯体外壳、镜片Gobo像、星芒或镜头鬼影。


## 2026-10-19 光度与IES

PerformanceLight.intensityUnit支持Legacy/Candela/Lumen；Legacy保留历史表现，物理模式以米为世界尺度，
颜色按Rec.709线性亮度归一化（黑色无光），表面/体积/可见灯口共用performanceLightIntensity。
Lumen以smoothstep光锥与峰值归一化IES的有效立体角换算，iris/Gobo是后续吸收；
Candela直接指定峰值方向强度。constantFlux只对Legacy生效。有限口径近场、range截断和RGB色谱仍是近似。
illuminanceLux提供无截断无遮挡的cd×cos/r²诊断，不把显示器像素宣称为测光仪。

IesProfile::parse只接受LM-63-1995/2002/2019、Type C、TILT=NONE、ballast字段均为1；
最多4MiB/262144测量样本，严格递增角度，支持旋转/90°/180°/360°对称，360°接缝需一致。
不支持Type A/B、外部/内嵌TILT或非单位ballast；失败保留旧对象。颜色不由IES指定。
createIesTexture上传257×129线性RGBA8角度图，CPU灯口与GPU表面/体积共享坐标基；
8bit峰值归一化会量化极弱旁瓣。Host配对不可变profile与存活atlas，Renderer检查句柄后复制灯包。
有效立体角以128点余弦空间求积，水平角均值由原始采样分段线性积分；极窄IES尖峰需提高采样质量。

.lshow写schema5，旧schema1..4补Legacy与空iesProfilePath，旧版本夹带新字段拒绝。
FixtureTrack.iesProfilePath是相对路径；Presenter用bindIesProfile绑定，缺少必需绑定在替换灯包前失败。
Studio提供IES文件选择、灯具单位属性与缓存；Save As检查新目录的依赖，资源包复制引用IES。


## 2026-10-19 距离焦平面与棱镜

focusDistance>0使用有限出光口/iris作为几何瞳孔，距焦面z处的弥散圆半径为
apertureRadius×iris×abs(1-z/focusDistance)，除以该处光束半径得到投影滤波足迹。
焦面处额外模糊为零，零focusDistance精确保留旧柔焦；图案使用有界五点瞳孔滤波，
不是衍射/像差/连续光学卷积。patternFocus仍是独立的作者柔化参数。

prismCount为1（禁用）或2..8，prismAngle[0,.35]rad，prismRotation为连续弧度。
每束保持原核心/外场发散，绕主轴倾斜并按束数分配强度；表面/烟雾/可见灯口共享方向与IES。
有限瞳孔的分束在出光口附近允许真实几何重叠；不人为挖空中心。
CPU裁剪取所有分束包围盒并集，体积解析各束交段的保守并集，再对实际每束求值。
阴影仍同一出光位置，投影外场扩大覆盖分束；非面积光阴影。
同灯仍一个表面和一个体积draw，shader最多8束，体积保留最多64步；稀疏宽分束可能欠采样，
不保证与单束相同GPU成本。五条固定Star图案射线展开，保留Shader1024迭代乘积安全门。

.lshow写schema6；schema1..5补focusDistance0/prismCount1/prismAngle0/prismRotation0。
焦距/倾角/连续旋转可插值，束数在关键帧处切换。所有键的outerAngle+prismAngle<=1.56，
保证跨拓扑键的中间帧仍合法。Studio属性可编排；--preset optics演示四束旋转与15单位焦面。


## 2026-10-19 Studio相机控制

工具栏加入Lens effects开关、Exposure locked/Auto exposure切换、手动曝光补偿EV[-16,6]，
以及brighten/darken适应速度[.01,20]。Apply camera先验证全部输入再应用，不写.lshow schema6。
CLI支持--lens-effects、--lens-star/--lens-ghost [0,4]、--lens-length [.001,.25]、
--lens-axes 1..4（1轴为横向光芒）、--lens-rotation弧度、--lens-dispersion [0,.02]、
--lens-dirt [0,1]、--auto-exposure、--exposure-ev、--adapt-bright/--adapt-dark；越界退出2。
Lens默认关闭、曝光默认锁定0EV，兼容旧预览。参数是会话相机设置，不烘焙到灯具关键帧。


## 2026-10-19 灯体与遮光几何

FixtureHousings::append显式追加不透明灯壳、镜片边框、筒形遮光罩与可选四片铰接遮光叶。
bodyLength[.5,10]、hoodLength[0,4]为出光口半径倍数，leafAngle[-.5,.5]rad为铰接角。
单位网格/材质在Renderer中共享，变更设置只改矩阵；不销毁保留场景仍引用的旧网格。
最多256灯/1536部件，灯灭保留实体，Point/零出光口跳过；非法输入保留准备输出/统计。
普通opaque深度、GBuffer与刚体批处理生效，背壳/罩/叶片可遮挡相机所见灯口。
灯壳金属为共享PBR材质，接受既有灯光/阴影；半径与方向跟随演出帧，外壳不随iris收缩。
这些是可见几何，不会重写光度/IES/Gobo或把叶片角自动转换为光束通光率；
光传播遮挡仍服从现有阴影预算/近裁剪精度，近灯口的机械挡光并非完整镜片追迹。

Presenter.setFixtureHousings默认关闭；Studio默认开启，可切换Housings/ Shade leaves。
--no-housings、--shade-leaves、--body-length、--hood-length、--leaf-angle控制会话预览，
非法维度退出2；不改.lshow schema6。动画/关键帧仍拥有灯具姿态与出光口尺寸。


## 2026-10-19 有限出光口半影（PCSS）

PerformanceLight.areaShadow为可选模式，仍需castShadow与atlas槽，默认关闭保留旧硬影/2x2 PCF。
瞳孔半径=apertureRadius×iris；先搜索更近遮挡深度，线性化ShadowCaster存储的
.5+.5*(clipZ/clipW)，再估算世界半影R×(zReceiver-zBlocker)/zBlocker并过滤可见性。
SpotShadowProjection.h是阴影生产矩阵与采样侧near/far/FOV的一致入口；源投影按有限瞳孔/棱镜扩大，
覆盖近处光束口径，最大仍遵循175°FOV。接触处几何半影为零，远处/大瞳孔更柔。
areaShadowBias[0,.1]为独立世界单位偏移（默认.003），旧light.shadowBias仍只用于legacy NDC比较。
表面16、体积8个确定性分层样本，额外中心遮挡采样；搜索和过滤半径最多32个atlas像素，
夹回本灯子图，既有shadowSoftness作为过滤下限。0半径回退旧路径，采样不使用历史或随机帧噪声。
该PCSS是平行平面/平均遮挡深度近似，稀疏采样、atlas分辨率、近裁剪及半径预算会限制质量，
并非多光源面积蒙卡阴影。不会增添新的shadow atlas槽或改变旧八灯Shader ABI。
算法参考：https://download.nvidia.com/developer/SDK/Individual_Samples/MEDIA/docPix/docs/PCSS.pdf

.lshow写schema7，schema1..6默认areaShadow=false和areaShadowBias=.003，旧版夹带新字段拒绝。
areaShadow按键切换，偏移可插值；Presenter同帧转发。Studio的Area shadows切换会在开启时
同时请求普通阴影，关闭只退回旧硬影/PCF；--area-shadows预览时为所有灯请求该模式，仍受预算。


## 2026-10-19 透明舞台与反射预览

Studio --scene glass 加入两层标准透明PBR接收面；--reflections 启用既有Deferred SSR，
并把舞台地面设为低粗糙度金属。SSR现在不要求外部IBL；只反射本帧可见不透明表面的已照明颜色。
--transparent-draws [0,1024]、--transparent-lights [0,32] 控制透明直接光额度，默认128/8。
性能JSON提供transparentLightDraws/transparentLightBudgetSkipped/transparentUnsupportedMaterials。
这三个选项为预览会话设置，schema7未变；不宣称玻璃折射、透射阴影或完整间接照明。


## 2026-10-19 间接光预览

Studio --indirect-lighting 启用单次屏幕漫反射，独立于 --reflections。
--indirect-strength [0,1]、--indirect-distance [.1,100]、--indirect-rays [1,4]、
--indirect-steps [4,32] 为有界预览控制；默认.5/10/4/24，schema7不变。
只从屏幕内已照亮的不透明接收面采色，不包含离屏GI、玻璃折射或灯口/体积的反弹。


## 2026-10-19 体积积分分辨率

Studio --fog-resolution 1|2|4 选择原始、每轴半分辨率、每轴四分之一分辨率散射积分。
默认1保持原表现。重建按深度拒绝跨面散射，消光/场景颜色/alpha维持完整视口；schema7不变。
无历史积累或froxel；离散小光束可能欠采样。固定--frames使用1/60演出步长，便于相同帧序列性能对照。


## 2026-10-19 性能采样

--profile-gpu显式开启逐视图查询并隐藏文字HUD，默认关闭，查询本身有成本。
性能JSON增加gpuProfiling、fogResolution、meanVolumeGpuMs/meanLightingGpuMs/meanShadowGpuMs，
以及rigidInstancedDraws/rigidInstancedObjects；未启用/不支持分项为0。
后者是本帧所有相关Pass的实例提交数（包含阴影重复），不是去重灯具数。
灯壳按共享部件连续提交，保留原矩阵并减少GBuffer/Shadow逐对象提交。


## 2026-10-19 froxel 预览

Studio --froxel启用Renderer有界网格，--froxel-tile 4|8|16、--froxel-slices 16|32|64、
--froxel-budget 65536..4194304控制质量/内存。默认仍原ray路径，schema7不变。
性能JSON的froxel/froxelWidth/Height/Slices/Cells来自实际本帧统计，包括设备回退与预算收缩。
--fog-resolution只影响ray路径；网格是单散射中心采样，暂无独立历史，不能保证细光束无闪烁。


## 2026-10-20 体积历史预览

--froxel-history同时启用网格与独立系数历史；volumeHistoryUsed报告实际帧级资格。
时间拖动调用resetVolumetricHistory，默认仍关闭，无.lshow格式变化。


## 2026-10-20 Presenter CPU 分段计时

LightShowPresenter::setProfilingEnabled默认关闭。frameStats()按值报告最近append的enabled/completed与
灯准备、点转换、聚合、灯包上传、灯壳、点批次、灯口和总CPU毫秒。失败为部分结果，关闭即清空；
不包含Core求值/Renderer绘制或GPU，不改变提交内容。Studio --profile-presenter（或--profile-gpu）开启，
使用既有30帧热身窗口平均，presenterStageSamples区分未采样与零耗时。


连续写入阶段验收：Renderer Contracts169/2211、Sequence108/3920通过；新增可选渲染桥成功/失败/关闭计时用例。
点实例/CPU四边形真实GPU对照D3D11、D3D12各1项24断言通过；Renderer清单1035项/10分区，Sequence108项/3分区无遗漏重复。
相同90帧热身后样本，CPU Presenter7.6742→3.3616ms、点批次6.6615→2.3455ms，整帧CPU20.4229→16.5421ms。
保持10000个可见实例；GPU均值10.0923/8.3168ms仅记录同机样本，不将其单独归因于CPU写入修改或承诺帧率。
默认未开启计时时45帧预览分段样本0/各段0，退出0。schema7未变，无图片查看。


## 2026-10-20 物理雾轨和密度画板（schema8）

LightShow.fog为空时保留宿主数值控制，schema1..7升级为空轨；写schema8。最多4096物理雾key，
可选局部uint8网格各轴1..64（最多262144cells），计入1M样本预算；compiled帧共享只读网格，不逐帧拷贝体素。
Presenter保留宿主steps/downsample/froxel/history/debug选项，只应用编排物理量；按指针和完整内容比较缓存atlas，
不使用hash代替数据相等。空轨不改宿主数值、释放Presenter自有场；宿主负责恢复自己的物理参数。
新增fogMs/meanPrepareFogMs诊断，完整CPU准备现在包括雾阶段。

Studio Fog authoring面板提供物理key、16/32/64网格、Z层、值0..255、半径1..16、强度0..1、2D/3D软刷及填充。
拖动只复制有界网格预览，释放鼠标后编译一次并留一个undo快照；快照变化会取消旧笔画。
主时间轴多一条Fog标记，支持Previous/Next/Move/Copy/模式；最后一个雾key不可删除，可显式Clear track。
原.lshow保存和bundle带入内嵌网格；无需额外外部密度格式。--fog-editor-smoke隐藏原生消息验证画板完整流程。
Core/AssetIO无Renderer依赖；RendererIntegration测试仅在有渲染桥时启用。AYEditor嵌入三维/可视多轨仍在后续清单。


2026-10-20 schema8雾编排最终验收：Contracts171/2242、PostProcess252/1363、SceneMaterial79/10042、
UI114/377、FrameGraph41/159、Sequence114/4019、Shader12/213（73生产变体/生成打包/无编译器加载）通过。
整包循环重载测试补齐bgfx延迟释放帧边界，八轮重载/旧token失效仍验证；临时诊断快捷路径已移除。
D3D11/D3D12各density atlas1/702、三种积分路径/双灯/resize/零场1/541通过；
既有局部/全局×动态/静态×1/2/4重建1/1298、legacy散射/局部盒/阴影1/737、旧光学9/395在两后端均通过。
隐藏原生Fog画板创建16³、拖动/一次Undo/Redo、物理key与编解码两后端通过；D3D12同时froxel/history=true。
独立Core/EditorCore构建与fog/key/brush合约通过，无Renderer/音频/AssetIO。
清单Renderer1038/10分区、Sequence114/3分区无遗漏重复；SDK129能力/53模块/95检索/561符号，0错误0警告。
最后一次相同90样本64灯/1万点/1920x828默认无密度图配置：Presenter3.1807ms、点批次2.2276ms、
整帧CPU15.8719/GPU9.1274ms、10000可见实例。是本机Debug样本，未据此承诺普遍帧率。
资源格式登记与SDK写schema8、旧1..7空雾迁移同步；没有查看图片或连接演出硬件。

### Strobe shutter

`CompiledLightShow::evaluateShutter(time, seconds, frame)` analytically averages the
procedural square-wave gate over a centered0..1s exposure. Zero keeps exact instant
sampling. Authored intensity/color, pose, points and fog sample the center; this
is not motion blur. Once clips/renormalizes the window; Loop restarts the gate
clock at show boundaries, accounting for complete periods without iteration.
Invalid inputs preserve the output. Studio `--shutter seconds` or Shutter button
opts into exposure integration (instant/8.33ms/16.67ms); default instant, schema8 unchanged.
The same fixture intensity feeds surface, volume and source aperture emission.

Shutter validation:5 cases/122 assertions; full Sequence119/4141 and119-case inventory passed. D3D11/D3D12 hidden45-frame Studio passed; NaN shutter rejected with exit2.

### 连续点阵聚合与有界排名过渡

EmissivePointLightingSettings.smoothCells默认false；true把RGB按三线性权重分给8个固定世界网格节点。
预算/钳制前保持RGB和位置一阶矩，±100000世界边界节点截断除外；不再跨硬桶突变，不能保证预算裁剪后能量守恒。
范围扩展sqrt(3)*cellSize并受10000总上限约束；最多524288候选节点，最多64灯，仍独立于可见点数预算。
EmissivePointLightingHistory::update接收time和transitionSeconds[0,1]，只用于smoothCells。
最多maxLights个槽，旧节点先淡出释放，再准入新节点淡入；可能出现短暂能量降低，保持原RGB不改变色相。
同时间不推进，逆时/超过.25秒缺帧/配置变化重置；全黑立即清空。显式seek/编辑调用reset；默认无历史。
Presenter setPointLighting(settings,transitionSeconds=0)保留原路径，可显式resetPointLightingHistory；Studio跳播会重置。
Studio --point-smooth、--point-transition秒，点照明按钮按off/hard/smooth循环；不改变schema8。

Point PerformanceLight可选pointContactDistance[0,10]、pointContactThickness[.001,5]、pointContactBias[0,.5]、pointContactStrength[0,1]。
距离0默认关闭；EmissivePointLightingSettings的contact*复制到聚合灯。共享不透明/标准透明受光绑定执行最多8次采样。
使用当前GBuffer世界位置/覆盖率沿向光方向短距追踪，比较相机空间前向深度以判断可见遮挡；厚度和偏移是世界单位。
只检测当前视图中的不透明遮挡，不能处理离屏、背面、多层透明或完整点光阴影；不占cube/Spot shadow槽。
Studio --point-occlusion距离显式开启。现有Spot必须保持pointContactDistance=0。

2026-10-20 continuous-point validation: Contracts175/2321, Sequence120/4157,
SceneMaterial79/10042, PostProcess252/1363, Shader12/213 (73 production variants) passed.
D3D11/D3D12 coherent-perspective contact probes each1/376; legacy point surface each1/139.
D3D12 combined Studio45 frames:1000 visible points,16 lights,froxel/history,16.67ms shutter.
Inventory: Renderer1043/10 partitions, Sequence120/3. SDK129/53/97 search/565 symbols,0 errors/warnings.
Debug/D3D11 1920x828,64 fixtures/10k points,16 aggregate lights,80 surface/12 volume/4 shadow,
24 steps,90 samples: initial smooth aggregation25.8929ms; cached smooth5.3723ms.
Final hard/smooth/contact aggregation4.1573/5.3723/7.3837ms, GPU11.4194/12.6907/15.6380ms.
All retain10k visible instances/16 lights. Process timing varies; no universal FPS claim.

### GPU点阵筛选、压缩与间接绘制（显式会话选项）

EmissivePointSettings.preferGpuCulling默认false；Windows D3D11/12具有compute/instancing/indirect及RGBA32F image-write时可用。
仅当source.size()<=maxPoints才走GPU；较小显示预算整批回CPU稳定ID排名，preferInstancing=false也保留CPU四边形。
源数据仍在CPU求值/验证，32-byte输入上传；mark、256点分块计数、前缀和、按原顺序压缩4vec4/64-byte实例，最后一个索引间接四边形绘制。
GPU与CPU共享XY/w裁剪、最小像素半径及面积能量公式，不额外加Z裁剪；可见筛选不影响独立的CPU点照明聚合。
Renderer每设备最多8个存活GPU cloud state；缓冲按256..65536的二次幂扩容，不占瞬态实例预算。
4个compute及raw draw vertex/fragment共6阶段在构建时烘焙为s_5_0 DXBC并嵌入，无运行时shaderc路径；73个标准生产变体仍单独覆盖。

cloud.stats()是append刷新的值快照。GPU模式countsExact=false，visible/culled为未知0，不能称为实际0可见；source仍是当前输入数。
gpuDiagnostics默认false，正常GPU绘制不读回、不等待。开启后，gpuSampleValid/source/visible/serial标记较早提交的异步样本，可能跨多个帧。
计算、blit、readTexture请求分开提交，越过返回完成帧后才消费；一个state最多一个在途读回。销毁state会取消尚未排队的请求，
已排队CPU目标由Renderer context保活到完成/设备关闭后，避免销毁cloud时写入已释放内存。
GPU流在OwnedCpu RenderScene中仍借用Cloud状态，必须保持owner到render结束，不能跨下一次append保留GPU帧；设备重建后重建Cloud/Presenter。

固定view50/51/52/53为mark/count/prefix/compact，54为诊断blit，排在主渲染之前；UI离屏单视图上限因此从142降为137。
RenderFrameStats.pointGpuTimeMs/pointGpuReadbackTimeMs分别统计四个计算视图和blit，GPU帧号/0含义沿用原诊断；后者不含CPU映射或排队延迟。
Studio --point-gpu显式启用，--point-gpu-readback再启用带序号样本；--point-cpu保留CPU四边形。JSON增加gpuPointCulling、pointCountsExact、
gpuPointFallback、gpuPointSampleValid/Serial/Source/Visible及meanPointGpuMs/meanPointGpuReadbackMs。
CameraRenderView即使没有不透明几何也执行请求的颜色/深度清屏，避免纯点阵或空帧保留上一张输出。

2026-10-20 GPU point acceptance: D3D11/D3D12 each2 cases/231 assertions,
all full-image CPU/GPU comparisons max difference0, including dense65536 last output,
partial257 tiles, oblique camera, subpixel energy, black frames, budgets, path switch,
readback-in-flight owner destruction and device shutdown. Six baked stages built.
Contracts177/2343, SceneMaterial79/10401, UI114/377, FrameGraph41/159,
PostProcess252/1373, Sequence120/4157, Shader12/213(73 standard variants) passed.
Inventory1046/10 and120/3; SDK129 capabilities/53 modules/99 searches/566 symbols,0 errors/warnings.
Same Debug/D3D11 90 samples: point CPU1.9881->.9917ms; GPU compute.7098ms.
Frame CPU23.5177->22.0014ms, GPU12.0987->12.6955ms, a measured tradeoff.
Async sample10000/10000; D3D12 combined sample1000/1000 with froxel history/contact/shutter passed.
