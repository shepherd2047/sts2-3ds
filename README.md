# 杀戮尖塔 2 · 3DS 最小可玩版本（个人移植）

仅供个人使用：素材和文本在本机从你自己的正版游戏包里提取，请勿分发 `romfs/`、`icon.png` 或打包好的 `.3dsx`。

## 当前进度

- 铁甲战士、三幕地图与已移植的战斗和事件；卡牌、遗物、药水、商店、远古者和地图节点均已接入。
- 战斗规则按反编译的 C# 移植；无界面模拟器可检查流程、随机数和存档读回。
- 地图选择点自动存档，标题页可继续游戏；电脑预览使用上下双屏布局。
- 各幕内容仍有未移植选项和近似实现。双屏页面逐项验收、其他角色、设置、音效与真机性能检查尚未完成。具体范围和状态见 [开发计划](docs/PLAN.md)。

## 目录

```
source/core/        游戏逻辑（C++20 协程，对应原版 async/await）
source/ui/          界面、文字排版、卡牌描述格式化
source/gfx/gfx.h    平台接口
source/platform_3ds 3DS 后端（citro2d）
source/platform_sdl 电脑预览后端（SDL2，双屏上下排列，鼠标当触控笔）
tools/              素材提取：PCK 解包、Spine 骨骼离线渲染、字体和图集生成
test/sim.cpp        无界面自动对战，用来查崩溃和规则问题
```

## 构建

1. 提取素材（需要 Python 3 + Pillow + numpy；自动在 Mac/Windows/Linux 的 Steam 库里找游戏，也可用 `STS2_DIR` 指定）：

   ```bash
   python3 tools/build_assets.py
   ```

2. 电脑预览（需要 SDL2）：

   ```bash
   make -f Makefile.sdl && ./build/sts2-preview
   ```

   键位：Z=A　X=B　S=X　A=Y　Q/W=L/R　方向键=十字键　鼠标=触控。

3. 3DS（需要 devkitPro 的 `3ds-dev`）：

   ```bash
   make
   ```

   把 `sts2-3ds.3dsx` 复制到 SD 卡的 `/3ds/`，用 Homebrew Launcher 启动。素材在 romfs 里，已打包进 `.3dsx`。

## 3DS 操作

- 触摸：点手牌放大查看；按住往上拖过手牌区出牌（自动锁定最近的敌人，左右滑换目标），拖回手牌区或按 B 取消
- ←→ 选牌 / 选目标，A 确认，B 取消，L/R 切换手牌，X 结束回合，Y 查看牌组
- START 暂停菜单（继续、地图、牌组、设置、放弃、保存并退出）
- START + SELECT 退出
