# -*- mode: python ; coding: utf-8 -*-
# PyInstaller 打包脚本 —— 生成 Windows 独立 exe。
#
# 用法（在 Windows 上，于本目录运行）：
#   python -m PyInstaller --clean --noconfirm sgzzlb_windows.spec
# 或直接双击 build_windows.bat。
#
# 注意：PyInstaller 必须在 Windows 上运行（不支持跨平台编译）。
#       本目录的 libsgzzlb.dll 由 Linux 侧 scripts/build_win_dll.sh 交叉编译好，
#       已一并打入 exe，目标机器无需预装 Python。

a = Analysis(
    ["../app/main.py"],          # 相对本 spec 文件目录（PyInstaller 会 chdir 到 spec 所在目录）
    pathex=["../app"],           # 让 Analysis 能找到 core_bridge.py
    binaries=[("libsgzzlb.dll", ".")],     # 核心库 → 解包根目录（与 core_bridge.find_library 约定一致）
    datas=[("../data/data.json", "data")],  # 数据文件 → _MEIPASS/data/data.json
    hiddenimports=[],
    hookspath=[],
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
)

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="sgzzlb",               # ASCII 名规避 bat 编码问题；生成后可在资源管理器里自行改名
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,                   # 不依赖 UPX，避免多一份外部工具
    console=False,               # windowed：不弹黑色控制台
    disable_windowed_traceback=False,
    icon=None,                   # 可替换为自定义 .ico：icon="icon.ico"
)
