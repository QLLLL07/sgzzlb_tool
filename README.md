# 《三国志·战略版》配将工具

基于 C++17 核心的配将辅助工具：Qt Widgets 原生桌面界面（首选）与现有 Python/tkinter 界面（兼容保留）都可评估任意三名武将组成的队伍，并给出全局推荐阵容。

## 架构

```
libsgzzlb.so（C++ 核心）  ←──  直接链接  ──→  sgzzlb_qt（Qt Widgets）
                    └─────  ctypes  ──→  core_bridge.py  ──→  main.py（tkinter，兼容）
```

- **核心**（`core/`）：数据加载（含内置回退）、满级战法模型、红度增伤/减伤与自由属性点、8 回合蒙特卡洛战斗引擎、规则评分、战法配装、两阶段推荐搜索和本地账号/个人战法池存档。
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

数据默认读取 `data/data.json`（138 武将 + 212 战法 + 125 条 Lv10 战法资料）；文件缺失时自动回退内置 12 名将 + 25 战法示例数据，可随时在界面"重载数据"。资料按战法名称自动挂载，当前全量战法表中可匹配 72 个基础名（含兵种战法的自带变体）。

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
| `evaluate_team_main(id1, id2, id3, troop, main_idx)` | 指定兵种和主将槽位评估（`main_idx` 为 `0~2`） |
| `evaluate_team_references(id1, id2, id3, troop, main_idx, sims)` | 对可用的桃园枪、魏法骑、蜀枪、吴骑参考队分别模拟，并返回平均/最低胜率、战损比、战损评分、95% 区间与种子 |
| `recommend_teams(top_n)` | 两阶段推荐 Top-N，枚举主将位置并返回 JSON 数组；以多参考队战损评分为主排序指标 |
| `recommend_tactics(hero_id, teammate1_id, teammate2_id, top_n, sims)` | 固定队友完整配装，逐个替换目标武将传承战法，对多套参考队实战测试并按战损评分排序 |
| `evaluate_team_stars(id1, id2, id3, s1, s2, s3)` | 按 0~5 红度进行满级战法实战评估 |
| `evaluate_team_build(build_json)` | 通过 JSON 指定红度和每名武将最多 10 点自由属性（武力/智力/统率/速度） |
| `get_tactic_max_level(name)` / `get_tactics_max_level()` | 返回原始文本、Lv10 资料摘要、可靠性/版本冲突标记与战斗数值模型 |
| `create_local_account(name)` | 创建本地账号，返回账号快照和 `id` |
| `set_local_account_hero(account_id, hero_id, stars, owned)` | 添加/更新/移除账号武将及红度 |
| `set_local_account_tactic(account_id, tactic_name, owned)` | 添加/移除账号的可配装传承战法 |
| `get_local_account(account_id)` / `list_local_accounts()` | 查询账号快照 |
| `save_local_accounts(path)` / `load_local_accounts(path)` | 导出/导入本地账号 JSON |
| `recommend_account_teams(account_id, top_n)` | 仅使用账号已拥有武将，并将红度带入实战推荐 |
| `get_heroes()` / `get_tactics()` | 英雄/战法紧凑列表（GUI 用） |
| `get_version()` | 版本号 |
| `free_string(s)` | 释放返回的字符串 |

## 评分模型

综合评分 = **0.80 × 战损评分** + **0.20 × 规则分**（0~100）。

- **规则分** = 0.35×兵种适性 + 0.25×国家队 + 0.25×角色覆盖 + 0.15×统御（超 20 重罚）。
- **实战战损比**：默认对可用的桃园枪、魏法骑、蜀枪、吴骑进行 8 回合蒙特卡洛模拟。`avgCasualtyRatio = 敌方承受伤害 / 我方承受伤害`，比值越高越好；`casualtyScore = 100 × ratio / (1 + ratio)`，用于跨参考队聚合和推荐排序。结果同时返回胜率、平局率、胜率标准误、95% 置信区间、平均伤害、起始随机种子。
- **战斗统计**：`battle.tacticStats` 按战法名称返回平均发动次数 `activations` 和平均伤害 `damage`；持续伤害会归属到施放它的战法。`evaluate_team_references` 和 `recommend_tactics` 的每个参考结果也提供同样字段。
- **战法配装**：传承战法池（74 个）贪心分配，每将 2 个，同一战法不跨将重复；阵法、兵种战法各自限制为每队最多一个，按角色契合 + 战法联动（如 盛气凌敌→横扫千军）。单战法推荐会先为队友生成完整配装，再逐项替换目标武将战法进行实战比较。
- **账号推荐**：阶段一枚举账号拥有武将的 C(n,3)（统御≤20）规则分预筛，阶段二仅从账号战法池配装并并行蒙特卡洛。红度与战法库存都会进入结果；空战法池会保留自带战法、不给传承槽补装。

## 我的账号（Qt）

Qt 主界面的“我的账号”页可新建、切换、导入和保存本地账号；在同一页面可搜索并加入武将、设置 0~5 红度、维护可配装的传承战法池，并运行“账号推荐 Top 10”。账号修改会自动保存到系统应用数据目录的 `local_accounts.json`，也可指定任意 JSON 文件手动保存或导入。旧版账号 JSON 会自动兼容导入，原有武将/红度不会丢失。

## 已知近似 / 简化

- 战法数据文件保留 1 级原始文本，并挂载 125 条可追溯的 Lv10 资料摘要。已有 14 个精选战法改为直接使用资料确认的 Lv10 可执行数值；其余战法仍使用 1.0→2.0 线性模型，API 会明确返回该状态，避免将估算误作实测值。
- 红度 0~5 不修改武将基础值和成长值；每红提供 3% 出伤增益和 3% 受伤减免。每名武将另有最多 10 点自由属性，可通过 `evaluate_team_build` 分配到武力、智力、统率、速度。
- 战斗公式（治疗基数 420、等级系数 1.6、属性差系数 1.4375、伤害浮动 86~94%）为玩家实测近似。
- 兵书/装备/科技/士气/规避/警戒细分等未建模；主将阵亡即判负；普攻 30% 概率集火主将（爆头倾向）。GUI 可在评估前选择主将，推荐会枚举主将位置。
- 兵种克制按文档：枪→骑→盾→弓→枪（±15%）。
- 自动兵种取"适性均值最高"，未必是最优实战兵种（如桃园适性平手时自动选盾；GUI 支持手选）。
- 参考对手使用当前数据集中可构建的固定队伍（桃园枪、魏法骑、蜀枪、吴骑）；缺少任一队伍武将时自动跳过。精选战法 17 个有专用执行定义，其中 14 个已按本次 Lv10 资料校正，其他战法仍走关键词近似抽取。
