# AYSequence 设计

Version: 0.1.0 · 2026-09-29 · Status: S1 / S2 / S3 implemented; S4–S5 planned

## 1. 定位与边界

AYSequence 编排场景中的对象与时间，不重新实现骨骼采样、状态机、音频混音、
相机渲染或 World 生命周期。适用于 2D/3D 场景与 Headless 求值测试，不归类为
AYAnimation 子系统。第一版不是完整影视 Sequencer，也不支持任意反射属性写入。

| 目标（未实现项标注 Planned） | 职责 | 依赖约束 |
|---|---|---|
| AYSequence / AYSequence::Core | 值对象、校验、时间映射、求值、播放器状态、事件穿越 | 标准库与必要数学基础；无 World/UI/设备依赖 |
| AYSequenceAssetIO | schema 1 严格 loose 读写；Resource 注册/依赖枚举另待后续 | Core、AYIO 与 JSON |
| AYSequenceEntityIntegration | 绑定解析、Transform 应用、生命周期/控制权、可选 Host 调度 | Core、AYEntityCore、AYGameLoop；不链接 AYEntity 完整 facade |
| AYSequenceAnimationIntegration | 既有 Clip 快照、时间采样、骨骼姿势 lease/恢复 | Entity integration、AYEntityAnimationIntegration、AYAnimation、AYResource；无 Renderer |
| AYSequenceEditorCore (Planned) | 文档数据、原子编辑、验证、预览状态 | Core；无 AYEditor/AYUI |
| AYEditor sequence extension (Planned) | 文档/历史、内容浏览、轨道界面、隔离场景预览 | 作者核心、可选 integration、公共 AYUI 作者控件 |

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

S1 已实现资源类型 `authoring.scene-sequence`、后缀 `.ayseq`（Experimental），后续
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
  Scene 稳定 GUID/编辑器持久化映射的设计在 S4 决定，不把 uint32 ID 假装成 GUID。
- 运行时解析到可校验 EntityHandle 并带 World generation；对象删除/场景替换使绑定
  失效，禁止通过旧指针恢复数据。必需绑定缺失时预检拒绝启动并报告诊断。
- 仅驱动明确登记的通道。第一版对受控对象采用独占控制，不隐式覆盖 physics 或
  确定性 SimTransform 权威；存在冲突时拒绝/要求宿主显式让出控制。
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
`SceneSequenceSession` 与可选 `SequenceRuntimeModule`，尚无编辑器接入。
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
- 仍无 `.ayseq` Resource loader、cook/pak、相机/音频、混合/Root Motion、Editor 页面。
- 骨架兼容检查是合法目标拓扑和轨道骨名/类型，不是源骨架 GUID/语义证明；当前
  `.ayanm` 不携带源骨架拓扑。宿主须提供已针对目标骨架制作/烘焙的 Clip。
