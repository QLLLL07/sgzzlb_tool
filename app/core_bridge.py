#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""core_bridge.py - Python ctypes 桥接 libsgzzlb.so。

封装约定：
- 所有返回字符串由库 malloc，本模块读取后立即用 free_string 释放；
- 所有库调用经同一把锁串行化（避免并发写全局数据仓库）；
- 提供与 C++ 导出函数一一对应的 Python 方法，返回 dict/list。
"""
import ctypes
import json
import os
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LIB = os.path.join(ROOT, "libsgzzlb.so")


class CoreBridgeError(RuntimeError):
    pass


class CoreBridge:
    def __init__(self, libpath=None):
        self._libpath = libpath or DEFAULT_LIB
        if not os.path.exists(self._libpath):
            raise CoreBridgeError(f"未找到动态库: {self._libpath}（请先在项目根目录执行 make）")
        self._lib = ctypes.CDLL(self._libpath)
        self._lock = threading.Lock()
        self._heroes = []
        self._tactics = []
        self._configure()

    # ---- ctypes 签名配置 ----
    def _configure(self):
        lib = self._lib
        for name in ("get_version", "load_data", "reload_data", "evaluate_team",
                     "evaluate_team_troop", "recommend_teams", "get_heroes", "get_tactics"):
            getattr(lib, name).restype = ctypes.c_void_p
        lib.free_string.argtypes = [ctypes.c_char_p]
        lib.load_data.argtypes = [ctypes.c_char_p]
        lib.reload_data.argtypes = [ctypes.c_char_p]
        lib.evaluate_team.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.evaluate_team_troop.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
        lib.recommend_teams.argtypes = [ctypes.c_int]

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
