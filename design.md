# AYSequence 设计

## 灯光/点阵演出扩展（2026-10-16）

独立LightShow值模型不修改既有Sequence格式/World绑定。Compile校验预算、有限值及编队ID集，
锥体半角要求cos(inner)>=cos(outer)+1e-6，保证Renderer可接收编译后的灯具，
并规范ID顺序；绝对时间求值支持键插值/扫动/频闪，作者采样可排除程序化调制。
有限出光口apertureRadius与两半角共同定义核心/场半径r(z)=apertureRadius+z*tan(angle)。
半角和出光口线性插值；核心外平滑衰减到场边界，不是恒定总光通或完整镜头光学模型。
播放器显式持有唯一时钟，输出向量复用容量；可从外部绝对时钟同步，暂停不推进。
连续间距诊断在每个编队键间对同一进度参数求两条线段的解析最近距离；
Smooth反解进度为节目时间，Step只检查保持位置和下一键，不把瞬时跳变当飞行路径。
候选按轨迹包围盒扫描，比较次数和报告数量由调用方限制；不提供避碰规划/飞行安全认证。
可选planFormationTransition以稳定ID的冲突图分配中途高度层，插入一个线性中间键；
每轮仍用同一连续间距诊断验证两段路径，最多尝试八档高度间隔。端点重叠、
诊断预算不足或最大高度内无法消除交叉时不替换输出。该功能只规划视觉过渡，
不计算障碍物、速度/加速度约束，也不认证真实飞行。
严格schema7/.lshow只作loose作者资源，兼容读取schema1/2/3并显式补零出光口、
清晰焦点/全开光阑或空贴图路径，不重解释旧文件的角度/功率；旧版本夹带新字段拒绝。
Texture Gobo路径属灯具轨道，不随时间插值，必须相对演出文件且不得越出其目录。
Presenter按稳定灯具ID绑定Renderer纹理，缺少贴图的Texture键在提交前失败。
schema4同时记录可插值shadowSoftness[0,4]，旧版本按零半径升级，Renderer映射到
单点阴影atlas的2x2过滤；这不是面积光源模拟。
写入通过AYIO原子替换，失败保留旧输出；JSON浮点允许正常舍入，整数/枚举不得有损转换。
Renderer桥接是可选target，Core可独立构建。Studio独立于AYEditor；启用AYAudio时
Studio按需解码本地WAV/MP3/OGG为有界PCM Clip，以Music总线播放，并用真实Voice播放头
同步LightShowPlayer。暂停、重启、跳转共用一个播放状态；清除音乐切回独立预览时钟。
Studio时间轴将关键帧时间压入1024个显示桶，音乐解码后有界抽样计算1024列峰值；
绘制成本与显示宽度相关，不随每帧的原始关键帧/PCM样本数增加。时间轴手势只定位
播放头，关键帧数值仍经现有滑条与Add / update key写入并保留撤销历史。
完整文件解码限制源文件128MiB、PCM 256MiB，不持久化音乐路径到.lshow；
宿主集成仍自行提供音频时钟与延迟策略。无AYAudio构建保留静音Studio。
手动exportLightShowBundle发布可移植文件夹，包含schema4演出、引用的图片和可选音乐；
只接受新目的文件夹，失败删除本次创建的目录。manifest仅记录相对音乐路径，读取时校验
路径边界；不改动.lshow格式或把音乐编入运行时快照。仍无DMX发送和ProjectBuild自动发布。
Studio在1080p默认24积分步、12条体积光和最多10000个可见点；D3D11/D3D12支持时
点阵走Renderer GPU实例化，--point-cpu强制回退对照。隐藏验收区分CPU墙钟与
Renderer提供的GPU帧计时，不能把某一后端/设备测值推广为跨平台保证。
局部烟雾沿用Renderer的VolumetricSettings.local AABB；Studio可切换默认盒或用
--smoke-box设定中心和半尺寸，不把全局密度场误当成具体烟机模拟。
完整契约见[项目文档](../../AYDocs/LIGHTING-SHOW-SYSTEM.md)。

Version: 0.1.0 · 2026-09-29 · Status: S1–S4 implemented; S5 planned

## 1. 定位与边界

AYSequence 编排场景中的对象与时间，不重新实现骨骼采样、状态机、音频混音、
相机渲染或 World 生命周期。适用于 2D/3D 场景与 Headless 求值测试，不归类为
AYAnimation 子系统。第一版不是完整影视 Sequencer，也不支持任意反射属性写入。

| 目标（未实现项标注 Planned） | 职责 | 依赖约束 |
|---|---|---|
| AYSequence / AYSequence::Core | 值对象、校验、时间映射、求值、播放器状态、事件穿越 | 标准库与必要数学基础；无 World/UI/设备依赖 |
| AYSequenceAssetIO | schema 1/2 严格 loose 读写；Resource 注册/依赖枚举另待后续 | Core、AYIO 与 JSON |
| AYSequenceEntityIntegration | 绑定解析、Transform 应用、生命周期/控制权、可选 Host 调度 | Core、AYEntityCore、AYGameLoop；不链接 AYEntity 完整 facade |
| AYSequenceAnimationIntegration | 既有 Clip 快照、时间采样、骨骼姿势 lease/恢复 | Entity integration、AYEntityAnimationIntegration、AYAnimation、AYResource；无 Renderer |
| AYSequenceEditorCore | 不可变快照、原子编辑、验证、稳定 ID | Core；无 AYEditor/AYUI/World/文件/内部历史 |
| AYEditor sequence extension | 文档/历史、内容浏览、轨道界面、隔离场景预览 | 作者核心、AssetIO、Animation integration、Scene、公共 AYUI 作者控件 |

不要在初始化阶段创建空 SubSystem 或装配默认 Host。实际调度需求在场景集成
阶段落实到 AYModule/AYGameLoop 的公开边界；依赖方向由构建检查验证。

## 2. 最小数据模型（拟议，S1 冻结）

- Sequence：schema、稳定 sequence ID、秒制时长、显示帧率、绑定表、轨道。
- Binding：sequence 内稳定 ID；运行时由宿主显式映射，不保存 Entity 裸指针。
- Track/Section/Key/Event：各自稳定 ID，不以列表位置充当持久化身份。
- Transform 轨道：平移/旋转/缩放；线性 Vector 与 Quaternion shortest-arc 采样。
  第一版不把 Quaternion 拆成四条独立标量插值，不支持 Hermite 旋转。
- Animation 片段：绑定、Clip 资源路径、序列 start/end、sourceOffset、正 speed。
  同一角色动画通道的片段不允许重叠；不提供序列片段混合/Root Motion。
- Event 轨道：稳定事件 ID、时间、声明式名称与受限 payload；不内嵌任意脚本。

同一 binding 的同一属性通道不能有重复权威轨道；全部 ID、有限数值、时间范围、
片段源范围、资源路径与规模上限须在接受数据前校验。无效修改/加载不得改变旧文档。

S1 已实现资源类型 `authoring.scene-sequence`；现行后缀 `.seq`，旧 `.ayseq` 只读兼容，后续
`runtime.scene-sequence` 仍 Planned。公开 JSON wire schema 1，顶层 type/version
显式识别；不是 `.ayanm` 的新字段，也不复用已有 `.timeline.json` 工具状态。
正式 reader/writer、依赖发现及发布准入齐备前，不注册为已支持 Runtime L1。
类型登记见根仓资源格式规范 §5；不为轨道、片段与绑定分别增设扩展名。

## 3. 时间、求值与事件契约

- 公共时间使用 double 秒，显示帧率只用于格式/吸附；记录并限制数值精度和最大时长。
- 同一快照同一时间的属性/姿势结果必须一致；seek 直接求值，不通过逐帧补跑。
- MVP 仅正向 Once、speed > 0。负速度、循环、嵌套及跳转编排明确拒绝，不能静默降级。
- 通道区间使用 [start,end)；序列终点允许显式最终采样。到达 end 的唯一规则须在
  S1 测试冻结，不得造成端点丢姿势或跨片段的双重写入。
- 正常 tick 仅触发穿越事件：(previous,current]；首次 play 后首个正 tick 包含
  起点事件。pause/resume 不重触发；stop 后再次 play 视为新一轮。
- seek/scrub、加载、预览刷新与零 tick 不派发事件。大 tick 逐个经过事件一次，
  同时刻按稳定声明顺序派发，不复制 AnimationPlayer 的循环 Notify 语义。
- 事件先形成 owned 队列；宿主派发可重入时必须检查播放/World generation，停止、
  销毁或换场景后不继续应用旧输出。未支持的方向/策略必须返回诊断。
- 一帧只允许一个 tick owner；时间轴窗口只展示，不再次推进 Host/预览播放器。

## 4. 场景绑定、控制权与恢复

- MVP 单场景；sequence 资产不硬编码 World 指针或可跨 World 复用的运行时实体 ID。
  外部绑定表由宿主注入；S2 使用 World-local Entity ID/handle，不改 Entity 核心。
  S4 使用 Assets 相对 Scene path + 精确唯一实体 name；不是 GUID，改名须重新绑定。
- 运行时解析到可校验 EntityHandle 并带 World generation；对象删除/场景替换使绑定
  失效，禁止通过旧指针恢复数据。必需绑定缺失时预检拒绝启动并报告诊断。
- 仅驱动明确登记的通道。第一版对受控对象采用独占控制，不隐式覆盖 physics 或
  确定性 SimTransform 权威（包括独立的 binary32 `DetSimTransformComponent`）；
  存在冲突时拒绝/要求宿主显式让出控制，播放中新增权威同样撤销 session。
- 播放开始前取得通道 ownership token 并捕获基态。默认 stop/完成/取消恢复基态，
  pause 保持当前值；释放后交还原控制器。不在每次 seek 重拍基态。
- 恢复只作用于同 generation 且仍持有的通道，不能覆盖对象销毁或所有权转移后的
  新值。多播放器抢占同通道在 MVP 拒绝，不做未定义优先级混合。
- 动画采样不要求编辑源 Clip，不修改角色的长期状态机资产；恢复原播放状态的可行
  边界在 S3 定义，不承诺当前 AnimationPlayer 已有序列专用 ownership API。

## 5. 作者与预览

作者核心独立，首次可用版本就通过 AYEditor 薄适配接入。预览在隔离 World 中，
不持续改写 authoring Scene；序列修改进入文档脏状态，seek/play 不使文档变脏。
Undo/Redo 复用现有 Editor history；原子领域编辑不在公共控件建立第二套历史。

复用 CurveCanvas、DopeSheet、PlaybackControls、选择、数值/资源字段与时间视口；
为片段 block 拖放/裁剪补专用视图，不能把逐键 DopeSheet 误称为已支持片段编排。
内容快照按 revision 缓存；播放头刷新不重建轨道、绑定、资源或全量采样缓存。

## 6. 连续实现与交付

执行 [根仓最小实现计划](../../AYDocs/SEQUENCE-IMPLEMENTATION-PLAN.md) 的 S1–S5。
每阶段列出已支持/拒绝项与 AYTest 用例表，通过后提交对应模块与根仓变化；不推送。
S1 已实现 Core 与独立 `AYSequenceAssetIO`（不是 Resource 集成）；S2 已实现
`SceneSequenceSession` 与可选 `SequenceRuntimeModule`；S4 已接入独立作者核心及 AYEditor。
Core 的 Value 数组是序列通道 wire 值，不替代引擎 AYMath 几何类型；场景适配
负责转换。Scale 范围 [1e-6,1e6]，单位 Quaternion norm² 容差 1e-6；ID 全局唯一。
schema 1 严格拒绝未知字段/重复 key/不支持版本；JSON 16 MiB、32 层。

S2 主线程私有 registry 原子预检所有目标后占用整个 Transform，不只占用有轨道的
分量；保存组件身份、字段与 revision。外部 writer 冲突停止，恢复仅写仍未变化的
目标。pause 不写/不重拍基态；完成输出终点样本后恢复。该检测不是线程锁。
借用 World 的寿命由既有 WorldLifecycle before-shutdown 撤销；清场/实体删除在
下一操作预检失败；跨 World 不使用全局 handle pool 作为成员证明。

Host 模块显式安装且 requires AYEntity.Runtime；Gameplay phase priority 1000、
Game clock/Scaled，初始化依赖 Entity。此阶段在真实 GameLoop 中先于 Entity.World；
同 World phase 的负 priority 不保证越过依赖排序，不能用于此契约。
最多 64 sessions；frameCount 防止双 tick，hosted session 拒绝手动推进。
当帧 owned 输出经 takeFrames 消费；seek/stop/rebind/新一轮播放/World 撤销令旧
generation 失效，调用者在每个事件回调前检查。S3 增加可选骨骼适配与显式事件派发。

## 7. S3 已实现契约

- 绑定阶段通过宿主 ResourceManager + resolveAssetPath 同步取得 Ready 的标准
  Animation 资源；校验并复制不可变 Clip/Skeleton 快照，resolver 不进入逐帧路径。
  Source duration 以实际 Clip 为准；骨名必须属于目标骨架，不隐式修复/重定向。
- 目标必须先准备 loaded SkeletonComponent/skinMatrices。只接受 Override 骨名
  TRS 与 Linear/Step/CubicHermite；Float/additive/未知骨/坏形状或数值显式拒绝。
  1..1024 bones，Clip 4096 tracks/100000 keys，合计键/切线 payload 64 MiB。
- 独立 AnimationPlayer 只 setTime/evaluate，无 tick/Clip Notify。片段端点规则沿用
  S1；间隙用捕获基态，直接 seek 与连续播放一致。全部骨骼姿势预采样成功才写 World，
  不出现 Transform 已写而骨骼求值失败的部分提交。
- SkeletonComponent 的主线程 externalPoseOwner 让内建 Animation/BlendSpace/
  StateMachine 在各自更新入口跳过对象。原 player/图/资产字段不改，停止后从原时间
  继续；不承诺追赶墙钟。pause 不写，完成立即恢复，只保留 owned 最终求值输出。
- 目标组件/蒙皮数组/骨架/原 player 身份、原 player 时间/暂停状态、输出矩阵和 lease
  被外部改动则停止。恢复仅作用于仍未变化的 owned target；转移 lease 不清除新 owner。
  其他 writer 必须遵守 lease，该检测不是通用 setter 锁，也不恢复外部 writer 的状态。
- dispatchSequenceEvents 按值持有 session/frame/sink；owner/batch/generation 防跨会话、
  重放、过期、嵌套派发。每个回调前预检；stop/seek/rebind/销毁/World teardown 取消
  余下事件。异常捕获后停止，返回已进入回调数；Host tick 只排队，取出后再显式派发。
- 仍无 `.seq` Resource loader、cook/pak、相机/音频、混合/Root Motion；Editor 页面由 S4 提供。
- 骨架兼容检查是合法目标拓扑和轨道骨名/类型，不是源骨架 GUID/语义证明；当前
  `.ayanm` 不携带源骨架拓扑。宿主须提供已针对目标骨架制作/烘焙的 Clip。

## 8. S4 已实现契约

- SequenceAuthoring 只依赖 Core，编辑复制候选并验证/编译整份快照；失败、异常、
  重入保留旧数据，no-op 保留内容版本和快照身份。辅助编辑只能对候选数据使用。
- AYEditor 文档复用 EditorCommandHistory；拖动期间保存基线，结束只记录起止快照，
  取消恢复。活动手势禁止 Save/Reload；全部编辑、撤销、保存重开保持稳定 ID。
- schema 2 必需 scenePath 和 binding.entityName，仍拒绝未知字段；兼容 schema 1。
  无 locator 写 schema 1，有 locator 写 schema 2。路径相对 Assets，name 精确唯一，
  最多 256 字节且不含控制符。不是 GUID；重名/缺失/改名拒绝，不猜第一个对象。
- 页面提供绑定、三类 Transform 轨道、事件、Clip 片段、时间和数值编辑；空的
  value/animation track 不合法，删最后 item 时删轨道。被引用 binding 删除拒绝。
- DopeSheet 复用稳定 ID/选择；动画 section 独立 block 视图，中间移动、两端裁剪。
  左裁剪推进 sourceOffset，移动保持 offset。非法时间/范围/重叠原子拒绝。
- 预览读取保存的场景到独立 Scene/World，不设置 active World，不运行系统/脚本，
  不派发 Sequence event/Clip Notify。view 单一推进，关闭/停止/完成恢复。
  内容修改/Undo/Reload 撤销旧 preview，显式 seek/play 才重新准备；绑定/资源不
  在逐帧路径重建。根路径 canonical containment 含 symlink 检查，无 cwd 猜测。
- 对象位置与骨骼预览带 orbit/zoom，不是完整渲染器/蒙皮模型视口，不预览未保存
  Scene，不提供隐式重定向。无 scenePath 时是纯数值预览。运行时 ID 不持久化。

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
