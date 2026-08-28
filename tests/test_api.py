#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""冒烟测试：通过 ctypes 验证核心动态库的 extern "C" 导出接口。

运行：python3 tests/test_api.py   （需先构建 libsgzzlb.so / libsgzzlb.dll）
"""
import ctypes
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _lib_candidates():
    return ["libsgzzlb.dll", "sgzzlb.dll"] if sys.platform == "win32" else ["libsgzzlb.so"]


def _find_lib():
    for name in _lib_candidates():
        p = os.path.join(ROOT, name)
        if os.path.exists(p):
            return p
    return os.path.join(ROOT, _lib_candidates()[0])


LIB = _find_lib()
DATA = os.path.join(ROOT, "data", "data.json")

failures = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ✓ {name}")
    else:
        failures.append(name)
        print(f"  ✗ {name}  {detail}")


def load_lib():
    lib = ctypes.CDLL(LIB)
    lib.get_version.restype = ctypes.c_void_p
    lib.load_data.restype = ctypes.c_void_p
    lib.reload_data.restype = ctypes.c_void_p
    lib.evaluate_team.restype = ctypes.c_void_p
    lib.recommend_teams.restype = ctypes.c_void_p
    lib.get_heroes.restype = ctypes.c_void_p
    lib.get_tactics.restype = ctypes.c_void_p
    lib.free_string.argtypes = [ctypes.c_char_p]
    lib.load_data.argtypes = [ctypes.c_char_p]
    lib.reload_data.argtypes = [ctypes.c_char_p]
    lib.evaluate_team.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.recommend_teams.argtypes = [ctypes.c_int]
    return lib


def call(lib, fn, *args):
    """调用返回字符串的导出函数：读取 -> 释放 -> 返回 str"""
    ptr = fn(*args)
    if not ptr:
        return None
    try:
        return ctypes.string_at(ptr).decode("utf-8")
    finally:
        lib.free_string(ctypes.c_char_p(ptr))


def main():
    if not os.path.exists(LIB):
        print(f"未找到动态库 {LIB}，请先构建（Linux: make；Windows: 见 README.md）")
        return 1
    lib = load_lib()

    print("== 版本 ==")
    v = call(lib, lib.get_version)
    check("get_version", isinstance(v, str) and len(v) > 0, str(v))
    print("  version:", v)

    print("== 加载数据 ==")
    r = json.loads(call(lib, lib.load_data, DATA.encode()))
    check("load_data ok", r.get("ok") is True, str(r))
    check("load_data 138 武将", r.get("heroes") == 138, str(r))
    check("load_data 212 战法", r.get("tactics") == 212, str(r))

    print("== get_heroes / get_tactics ==")
    heroes = json.loads(call(lib, lib.get_heroes))
    check("get_heroes 数量", len(heroes) == 138, f"len={len(heroes)}")
    h0 = heroes[0]
    check("hero 字段", all(k in h0 for k in ("id", "name", "kingdom", "cost", "role", "innate")), str(h0)[:120])
    tactics = json.loads(call(lib, lib.get_tactics))
    check("get_tactics 数量", len(tactics) == 212, f"len={len(tactics)}")

    # 桃园三将下标
    idx = {h["name"]: h["id"] for h in heroes}
    liu, guan, zhang = idx["刘备"], idx["关羽"], idx["张飞"]

    print("== evaluate_team（桃园）==")
    rep = json.loads(call(lib, lib.evaluate_team, liu, guan, zhang))
    check("evaluate ok", rep.get("ok") is True, str(rep)[:120])
    check("names", rep.get("names") == ["刘备", "关羽", "张飞"], str(rep.get("names")))
    check("有 ruleScore", "ruleScore" in rep and 0 <= rep["ruleScore"]["total"] <= 100)
    check("有 battle", "battle" in rep and rep["battle"]["sims"] > 0)
    check("每将 2 战法", all(len(t) == 2 for t in rep.get("tactics", [])), str(rep.get("tactics")))
    check("有建议", len(rep.get("advice", [])) >= 1)
    check("总分 0-100", 0 <= rep.get("total", -1) <= 100, str(rep.get("total")))

    print("== evaluate_team（非法下标）==")
    bad = json.loads(call(lib, lib.evaluate_team, 999, 0, 1))
    check("非法下标返回 error", bad.get("ok") is False, str(bad)[:120])

    print("== recommend_teams ==")
    recs = json.loads(call(lib, lib.recommend_teams, 5))
    check("recommend 返回 5 条", len(recs) == 5, f"len={len(recs)}")
    totals = [r["total"] for r in recs]
    check("按总分降序", all(totals[i] >= totals[i + 1] for i in range(len(totals) - 1)), str(totals))
    r0 = recs[0]
    check("每条含 heroes/tactics", "heroes" in r0 and "tactics" in r0, str(r0)[:120])

    print("== 回退数据（指向不存在的文件）==")
    r = json.loads(call(lib, lib.load_data, b"/nonexistent/data.json"))
    check("load_data ok=false", r.get("ok") is False, str(r))
    check("回退 source", r.get("source") == "builtin-fallback", str(r))
    hb = json.loads(call(lib, lib.get_heroes))
    check("回退 ≥10 武将", len(hb) >= 10, f"len={len(hb)}")
    # 回退数据应含桃园三将，可正常评估
    bidx = {h["name"]: h["id"] for h in hb}
    if all(n in bidx for n in ("刘备", "关羽", "张飞")):
        rep = json.loads(call(lib, lib.evaluate_team, bidx["刘备"], bidx["关羽"], bidx["张飞"]))
        check("回退下评估 ok", rep.get("ok") is True)
    else:
        check("回退下评估 ok", False, "回退数据缺少桃园三将")

    print("== 重新加载正式数据 ==")
    r = json.loads(call(lib, lib.reload_data, DATA.encode()))
    check("reload_data ok", r.get("ok") is True and r.get("heroes") == 138, str(r))

    if failures:
        print(f"\n共 {len(failures)} 项失败: {failures}")
        return 1
    print("\n全部通过 ✔")
    return 0


if __name__ == "__main__":
    sys.exit(main())
