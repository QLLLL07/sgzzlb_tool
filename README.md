# 《三国志·战略版》配将工具

基于 C++17 核心的配将辅助工具：Qt Widgets 原生桌面界面（首选）与现有 Python/tkinter 界面（兼容保留）都可评估任意三名武将组成的队伍，并给出全局推荐阵容。

## 架构

```
libsgzzlb.so（C++ 核心）  ←──  直接链接  ──→  sgzzlb_qt（Qt Widgets）
                    └─────  ctypes  ──→  core_bridge.py  ──→  main.py（tkinter，兼容）
```

- **核心**（`core/`）：数据加载（含内置回退）、满级战法模型、红度属性副本、8 回合蒙特卡洛战斗引擎、规则评分、战法配装、两阶段推荐搜索和本地账号存档。
- **通信**：仅通过 `extern "C"` 导出函数 + JSON 字符串，**不使用命令行**。

## 构建

依赖：`g++`（C++17）、`make`（或 `cmake`）。原生界面另需 Qt 5/6 Widgets 开发包。

```bash
make            # 生成 libsgzzlb.so
# 或
cmake -B build && cmake --build build
```

检测到 Qt 后，上述 CMake 构建还会生成 `sgzzlb_qt`。例如 Debian/Ubuntu：

```bash
sudo apt install qt6-base-dev
cmake -B build && cmake --build build
./build/sgzzlb_qt
```

## 运行

```bash
./build/sgzzlb_qt          # 启动 Qt 原生 GUI（通过 CMake 且已安装 Qt）
python3 app/main.py        # 启动旧版 tkinter GUI（兼容保留）
```

Qt 程序会自动从当前目录、可执行文件目录及其上级目录查找 `data/data.json`。Linux 的 CMake 构建已设置运行时路径，可执行文件会在同一构建目录加载 `libsgzzlb.so`。

数据默认读取 `data/data.json`（138 武将 + 212 战法）；文件缺失时自动回退内置 12 名将 + 25 战法示例数据，可随时在界面"重载数据"。

## Windows 发布

在 Windows 上安装 Qt 5/6、CMake 和对应编译器后，在项目根目录执行：

```bat
cmake -S . -B build
cmake --build build --config Release
```

使用 Qt MinGW 工具链时，在生成的可执行文件目录运行 Qt 自带的 `windeployqt sgzzlb_qt.exe`，并将 `data\data.json` 放在 exe 同级的 `data` 目录。旧版 Python/tkinter 兼容入口仍可通过 `windows\build_windows.bat` 使用 PyInstaller 打包。

## 测试

```bash
python3 tests/test_api.py  # ctypes 契约测试（加载/评估/满级/红度/账号/推荐/回退）
```

## C++ 导出接口（api.cpp）

所有字符串由库 `malloc`，Python 侧经 `free_string` 释放；`hero id` = `get_heroes()` 返回数组下标（0-based）。

| 函数 | 说明 |
|------|------|
| `load_data(path)` / `reload_data(path)` | 加载数据文件；失败回退内置数据。返回 JSON `{ok, source, heroes, tactics, error}` |
| `evaluate_team(id1, id2, id3)` | 评估队伍（兵种自动选最佳适性）。返回完整 JSON 报告 |
| `evaluate_team_troop(id1, id2, id3, troop)` | 指定兵种评估（`-1` 自动，`0~3` 骑/盾/弓/枪） |
| `recommend_teams(top_n)` | 两阶段推荐 Top-N，返回 JSON 数组 |
| `recommend_tactics(hero_id, teammate1_id, teammate2_id, top_n, sims)` | 固定队友逐个测试传承战法，按实战胜率排序 |
| `evaluate_team_stars(id1, id2, id3, s1, s2, s3)` | 按 0~5 红度进行满级战法实战评估 |
| `get_tactic_max_level(name)` / `get_tactics_max_level()` | 返回满级战法视图（当前 1 级文本按数值倍率 2.0 建模） |
| `create_local_account(name)` | 创建本地账号，返回账号快照和 `id` |
| `set_local_account_hero(account_id, hero_id, stars, owned)` | 添加/更新/移除账号武将及红度 |
| `get_local_account(account_id)` / `list_local_accounts()` | 查询账号快照 |
| `save_local_accounts(path)` / `load_local_accounts(path)` | 导出/导入本地账号 JSON |
| `recommend_account_teams(account_id, top_n)` | 仅使用账号已拥有武将，并将红度带入实战推荐 |
| `get_heroes()` / `get_tactics()` | 英雄/战法紧凑列表（GUI 用） |
| `get_version()` | 版本号 |
| `free_string(s)` | 释放返回的字符串 |

## 评分模型

综合评分 = **0.70 × 战斗胜率** + **0.30 × 规则分**（0~100）。

- **规则分** = 0.35×兵种适性 + 0.25×国家队 + 0.25×角色覆盖 + 0.15×统御（超 20 重罚）。
- **战斗胜率**：对固定参考队（桃园：刘备/关羽/张飞 + 经典战法）做 200 场、8 回合蒙特卡洛模拟（多线程并行）；推荐结果的 `evidence` 字段记录该依据。
- **战法配装**：传承战法池（74 个）贪心分配，每将 2 个，同一战法不跨将重复，按角色契合 + 战法联动（如 盛气凌敌→横扫千军）。
- **推荐搜索**：阶段一枚举 C(138,3)（统御≤20）规则分预筛取 Top 200；阶段二配装 + 并行蒙特卡洛，按综合分取 Top-N。账号推荐会先限制在账号拥有集合，并使用每名武将的红度副本。

## 已知近似 / 简化

- 战法数据文件保留 1 级原始文本；战斗默认按 10 级运行，数值型效果使用 1.0→2.0 线性倍率，概率与持续回合不放大。真实满级数据可替换该模型。
- 红度 0~5 每级使武力、智力、统率、速度基础值和成长值提高 2%，计算使用副本，不污染全局数据。
- 战斗公式（治疗基数 420、等级系数 1.6、属性差系数 1.4375、伤害浮动 86~94%）为玩家实测近似。
- 兵书/装备/科技/士气/规避/警戒细分等未建模；主将阵亡即判负；普攻 30% 概率集火主将（爆头倾向）。
- 兵种克制按文档：枪→骑→盾→弓→枪（±15%）。
- 自动兵种取"适性均值最高"，未必是最优实战兵种（如桃园适性平手时自动选盾；GUI 支持手选）。
- 参考对手固定为桃园；精选战法 17 个有精确定义，其余走关键词近似抽取。
