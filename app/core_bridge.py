#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""core_bridge.py - Python ctypes 桥接核心动态库（libsgzzlb.so / libsgzzlb.dll）。

封装约定：
- 所有返回字符串由库 malloc，本模块读取后立即用 free_string 释放；
- 所有库调用经同一把锁串行化（避免并发写全局数据仓库）；
- 提供与 C++ 导出函数一一对应的 Python 方法，返回 dict/list。
"""
import ctypes
import json
import os
import sys
import threading


def resource_root():
    """资源根目录：PyInstaller 冻结运行时指向解包目录，否则为项目根目录。

    cwd 无关：正常源码运行 = 项目根；打成单文件 exe 后 = sys._MEIPASS。
    """
    if getattr(sys, "frozen", False):
        return getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _lib_candidates():
    if sys.platform == "win32":
        return ["libsgzzlb.dll", "sgzzlb.dll"]
    return ["libsgzzlb.so"]


def find_library():
    """在资源根目录及其 windows/ 子目录探测实际存在的动态库。

    找不到则返回首选名的期望路径（用于报错提示）。
    """
    for base in (resource_root(), os.path.join(resource_root(), "windows")):
        for name in _lib_candidates():
            p = os.path.join(base, name)
            if os.path.exists(p):
                return p
    return os.path.join(resource_root(), _lib_candidates()[0])


ROOT = resource_root()
DEFAULT_LIB = find_library()


class CoreBridgeError(RuntimeError):
    pass


class CoreBridge:
    def __init__(self, libpath=None):
        self._libpath = libpath or DEFAULT_LIB
        if not os.path.exists(self._libpath):
            hint = "请先构建 libsgzzlb.dll（见 README.md 的 Windows 发布说明）" if sys.platform == "win32" else "请先在项目根目录执行 make"
            raise CoreBridgeError(f"未找到动态库: {self._libpath}（{hint}）")
        self._lib = ctypes.CDLL(self._libpath)
        self._lock = threading.Lock()
        self._heroes = []
        self._tactics = []
        self._configure()

    # ---- ctypes 签名配置 ----
    def _configure(self):
        lib = self._lib
        for name in ("get_version", "load_data", "reload_data", "evaluate_team",
                     "evaluate_team_troop", "recommend_teams", "recommend_account_teams",
                     "recommend_tactics",
                     "evaluate_team_stars", "get_tactic_max_level", "get_tactics_max_level",
                     "evaluate_team_build",
                     "create_local_account", "set_local_account_hero", "get_local_account",
                     "list_local_accounts", "save_local_accounts", "load_local_accounts",
                     "get_heroes", "get_tactics"):
            getattr(lib, name).restype = ctypes.c_void_p
        lib.free_string.argtypes = [ctypes.c_char_p]
        lib.load_data.argtypes = [ctypes.c_char_p]
        lib.reload_data.argtypes = [ctypes.c_char_p]
        lib.evaluate_team.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.evaluate_team_troop.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.recommend_teams.argtypes = [ctypes.c_int]
        lib.recommend_tactics.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                          ctypes.c_int, ctypes.c_int]
        lib.recommend_account_teams.argtypes = [ctypes.c_char_p, ctypes.c_int]
        lib.evaluate_team_stars.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                            ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.evaluate_team_build.argtypes = [ctypes.c_char_p]
        lib.get_tactic_max_level.argtypes = [ctypes.c_char_p]
        lib.create_local_account.argtypes = [ctypes.c_char_p]
        lib.set_local_account_hero.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.get_local_account.argtypes = [ctypes.c_char_p]
        lib.save_local_accounts.argtypes = [ctypes.c_char_p]
        lib.load_local_accounts.argtypes = [ctypes.c_char_p]

    # ---- 底层调用 ----
    def _call(self, fn, *args):
        """调用返回字符串的导出函数：读取 -> 释放 -> 返回 str"""
        with self._lock:
            ptr = fn(*args)
            if not ptr:
                raise CoreBridgeError("库调用返回空指针")
            try:
                return ctypes.string_at(ptr).decode("utf-8")
            finally:
                self._lib.free_string(ctypes.c_char_p(ptr))

    # ---- 公开接口 ----
    def version(self) -> str:
        return self._call(self._lib.get_version)

    def load_data(self, path=None) -> dict:
        """加载数据；返回 {'ok','source','heroes','tactics','error'}，并刷新列表缓存。"""
        p = path if path else ""
        r = json.loads(self._call(self._lib.load_data, p.encode("utf-8")))
        self._refresh_lists()
        return r

    def reload_data(self, path=None) -> dict:
        p = path if path else ""
        r = json.loads(self._call(self._lib.reload_data, p.encode("utf-8")))
        self._refresh_lists()
        return r

    def _refresh_lists(self):
        self._heroes = json.loads(self._call(self._lib.get_heroes))
        self._tactics = json.loads(self._call(self._lib.get_tactics))

    @property
    def heroes(self) -> list:
        return self._heroes

    @property
    def tactics(self) -> list:
        return self._tactics

    def hero_by_id(self, hid) -> dict | None:
        if 0 <= hid < len(self._heroes):
            return self._heroes[hid]
        return None

    def hero_id_by_name(self, name) -> int | None:
        for h in self._heroes:
            if h["name"] == name:
                return h["id"]
        return None

    def evaluate_team(self, id1, id2, id3, troop=-1) -> dict:
        """评估队伍。troop: -1 自动，0=骑 1=盾 2=弓 3=枪。返回完整报告 dict。"""
        raw = self._call(self._lib.evaluate_team_troop, int(id1), int(id2), int(id3), int(troop))
        return json.loads(raw)

    def recommend_teams(self, top_n=10) -> list:
        raw = self._call(self._lib.recommend_teams, int(top_n))
        return json.loads(raw)

    def recommend_tactics(self, hero_id: int, teammate1_id: int, teammate2_id: int,
                          top_n=10, sims=200) -> dict:
        """在固定队友和兵种下，以实战胜率比较指定武将可用传承战法。"""
        raw = self._call(self._lib.recommend_tactics, int(hero_id), int(teammate1_id),
                         int(teammate2_id), int(top_n), int(sims))
        return json.loads(raw)

    # ---- 本地账号、红度与满级战法接口 ----
    def evaluate_team_stars(self, id1, id2, id3, stars1=0, stars2=0, stars3=0) -> dict:
        """按三名武将各自 0..5 红度进行满级战法实战评估。"""
        raw = self._call(self._lib.evaluate_team_stars, int(id1), int(id2), int(id3),
                         int(stars1), int(stars2), int(stars3))
        return json.loads(raw)

    def evaluate_team_build(self, heroes: list[dict]) -> dict:
        """评估带红度和自由属性点的队伍；每名武将 freeAttributes 合计最多 10 点。"""
        if len(heroes) != 3:
            raise ValueError("heroes 必须包含 3 名武将")
        payload = json.dumps({"heroes": heroes}, ensure_ascii=False, separators=(",", ":"))
        return json.loads(self._call(self._lib.evaluate_team_build, payload.encode("utf-8")))

    def tactic_max_level(self, name: str) -> dict:
        """返回指定战法的满级数值模型和原始资料。"""
        return json.loads(self._call(self._lib.get_tactic_max_level, name.encode("utf-8")))

    def tactics_max_level(self) -> list:
        return json.loads(self._call(self._lib.get_tactics_max_level))

    def create_local_account(self, name: str) -> dict:
        return json.loads(self._call(self._lib.create_local_account, name.encode("utf-8")))

    def set_local_account_hero(self, account_id: str, hero_id: int, stars=0, owned=True) -> dict:
        """新增、更新或移除本地账号的武将；红度在核心侧截断到 0..5。"""
        raw = self._call(self._lib.set_local_account_hero, account_id.encode("utf-8"),
                         int(hero_id), int(stars), 1 if owned else 0)
        return json.loads(raw)

    def get_local_account(self, account_id: str) -> dict:
        return json.loads(self._call(self._lib.get_local_account, account_id.encode("utf-8")))

    def list_local_accounts(self) -> list:
        return json.loads(self._call(self._lib.list_local_accounts))

    def save_local_accounts(self, path: str) -> dict:
        return json.loads(self._call(self._lib.save_local_accounts, path.encode("utf-8")))

    def load_local_accounts(self, path: str) -> dict:
        return json.loads(self._call(self._lib.load_local_accounts, path.encode("utf-8")))

    def recommend_account_teams(self, account_id: str, top_n=10) -> dict:
        """只以账号拥有的武将为候选，按账号红度模拟并返回推荐结果。"""
        raw = self._call(self._lib.recommend_account_teams, account_id.encode("utf-8"), int(top_n))
        return json.loads(raw)
