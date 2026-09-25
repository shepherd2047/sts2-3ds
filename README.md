# 杀戮尖塔 2 · 3DS 最小可玩版本（个人移植）

仅供个人使用：素材和文本在本机从你自己的正版游戏包里提取，请勿分发 `romfs/`、`icon.png` 或打包好的 `.3dsx`。

## 内容

- 角色：铁甲战士（初始牌组、燃烧之血）
- 卡牌：3 张初始牌 + 20 张普通牌（奖励池），升级效果与原版一致；状态牌黏液、伤口
- 第一幕（Overgrowth）：4 场弱战斗、4 场普通战斗、精英 Byrdonis、Boss 幻影（Vantom）
- 能力：力量、敏捷、易伤、虚弱、脆弱、缩小、滑溜、领地、临时力量
- 地图（15 层 + Boss）、卡牌奖励、休息处（休息 / 锻造）、牌组查看

战斗规则按反编译的 C# 逐段翻译：伤害流程（`CreatureCmd.Damage`）、先加后乘的修正顺序、格挡、能力层数的回合结算（包括玩家身上的负面效果第一次不减层）、怪物行动状态机（`RandomBranchState` 的不可重复规则、冷却）、随机数（xoshiro256**，与原版相同）。

暂未实现：事件、商店、宝箱、药水、其他角色、第二幕以后、存档、音效。

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

1. 提取素材（需要 Python 3 + Pillow + numpy，默认读取 Steam 安装的 `Slay the Spire 2.pck`）：

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

- 触摸：点手牌查看，再点一次打出；需要目标的牌在下屏点敌人名字
- ←→ 选牌 / 选目标，A 确认，B 取消，L/R 切换手牌，X 结束回合，Y 查看牌组
- START + SELECT 退出
