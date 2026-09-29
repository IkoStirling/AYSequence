# AYSequence

场景序列编排模块，独立于骨骼动画求值与编辑器产品层。

**当前状态：S1 已实现，2026-09-29。** 独立 owned 快照求值、Once 播放器、
Transform/动画源时间/事件轨道、严格 JSON 和原子 loose `.ayseq` 保存加载。
场景绑定、实际骨骼驱动、编辑器页面及发布仍未交付；无默认 Host 装配。

模块边界和播放契约见 [design.md](design.md)；连续实施顺序、优先级与验收见
[最小实现计划](../../AYDocs/SEQUENCE-IMPLEMENTATION-PLAN.md)。

## 本地仓库与构建

- 独立仓库位于 `AYRuntime/AYSequence`，初始分支为 `main`，未配置任何 remote。
- 暂未登记为根仓 submodule/gitlink：没有远端时不写虚假的 `.gitmodules` URL。
  根仓新克隆不会自动获得此模块；本地仓库须单独备份/搬运。
- 根工程使用 `AY_ENABLE_SEQUENCE=ON` 显式启用，默认 OFF；启用时必须先有本地
  模块目录。未来远端由用户指定后，再登记 submodule，不自动创建或推送仓库。
- `AYSequence` / `AYSequence::Core` 是独立静态核心，无其他引擎 target 依赖。
  `AYSequenceAssetIO` / `AYSequence::AssetIO` 可选，依赖 AYIO/JSON，不是 Resource loader。

可在配置好 C++ 编译器的环境中单独配置：

```powershell
cmake -S AYRuntime/AYSequence -B out/build/sequence-bootstrap -DAY_SEQUENCE_BUILD_ASSET_IO=OFF
cmake --build out/build/sequence-bootstrap
```

## 预期依赖边界

运行时核心不依赖 AYEditor、AYUI、AYScene、AYRenderer、AYAudio 或 AYAnimation。
World/动画/音频/相机桥接由独立可选 integration targets 承担；UI-free 作者核心
通过 AYEditor 薄适配接入，复用已有公共作者控件。上述 scene integration/editor
targets 尚未创建，待对应阶段实现后再公开具体 API。

## S1 使用

`Sequence` 构造值数据，`CompiledSequence::compile` 返回校验后的不可变快照。
为 `SequencePlayer` 绑定快照，再使用 play/pause/stop/seek/advance；advance 输出
owned 属性样本、动画源时间和事件，不执行场景写入或用户回调。
可选 `SequenceAssetIO.h` 提供 encode/decode/load/save；显式路径由调用者确定，
原子保存通过 AYIO，尚未注册 ResourceManager loader。具体边界见设计和测试。

## S0 验证记录（2026-09-29）

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
