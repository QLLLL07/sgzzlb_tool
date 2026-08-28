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
    lib.evaluate_team_main.restype = ctypes.c_void_p
    lib.evaluate_team_references.restype = ctypes.c_void_p
    lib.evaluate_team_stars.restype = ctypes.c_void_p
    lib.evaluate_team_build.restype = ctypes.c_void_p
    lib.recommend_teams.restype = ctypes.c_void_p
    lib.recommend_tactics.restype = ctypes.c_void_p
    lib.recommend_account_teams.restype = ctypes.c_void_p
    lib.get_tactic_max_level.restype = ctypes.c_void_p
    lib.create_local_account.restype = ctypes.c_void_p
    lib.set_local_account_hero.restype = ctypes.c_void_p
    lib.set_local_account_tactic.restype = ctypes.c_void_p
    lib.get_local_account.restype = ctypes.c_void_p
    lib.save_local_accounts.restype = ctypes.c_void_p
    lib.load_local_accounts.restype = ctypes.c_void_p
    lib.get_heroes.restype = ctypes.c_void_p
    lib.get_tactics.restype = ctypes.c_void_p
    lib.free_string.argtypes = [ctypes.c_char_p]
    lib.load_data.argtypes = [ctypes.c_char_p]
    lib.reload_data.argtypes = [ctypes.c_char_p]
    lib.evaluate_team.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.evaluate_team_main.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.evaluate_team_references.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                             ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.evaluate_team_stars.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                        ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.evaluate_team_build.argtypes = [ctypes.c_char_p]
    lib.recommend_teams.argtypes = [ctypes.c_int]
    lib.recommend_tactics.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.recommend_account_teams.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.get_tactic_max_level.argtypes = [ctypes.c_char_p]
    lib.create_local_account.argtypes = [ctypes.c_char_p]
    lib.set_local_account_hero.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lib.set_local_account_tactic.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    lib.get_local_account.argtypes = [ctypes.c_char_p]
    lib.save_local_accounts.argtypes = [ctypes.c_char_p]
    lib.load_local_accounts.argtypes = [ctypes.c_char_p]
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
    check("已挂载 Lv10 战法资料", r.get("tacticsWithMaxLevelData", 0) >= 70, str(r))

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
    check("有模拟不确定性指标", 0 <= rep["battle"].get("drawRate", -1) <= 1 and
          0 <= rep["battle"].get("winRateStdError", -1) <= 0.5, str(rep["battle"]))
    check("胜率置信区间合法且含胜率", 0 <= rep["battle"].get("winRateCi95Low", -1) <= rep["battle"].get("winRate", -1) <=
          rep["battle"].get("winRateCi95High", -1) <= 1, str(rep["battle"]))
    check("返回模拟种子", isinstance(rep["battle"].get("seed"), (int, float)) and rep["battle"].get("seed") >= 0,
          str(rep["battle"]))
    check("每将 2 战法", all(len(t) == 2 for t in rep.get("tactics", [])), str(rep.get("tactics")))
    check("有建议", len(rep.get("advice", [])) >= 1)
    check("总分 0-100", 0 <= rep.get("total", -1) <= 100, str(rep.get("total")))
    check("默认满级战法", rep.get("tacticLevel") == 10 and "evidence" in rep, str(rep))
    main_rep = json.loads(call(lib, lib.evaluate_team_main, liu, guan, zhang, -1, 1))
    check("可指定主将", main_rep.get("ok") is True and main_rep.get("mainIdx") == 1 and
          main_rep.get("mainName") == "关羽", str(main_rep)[:180])
    refs = json.loads(call(lib, lib.evaluate_team_references, liu, guan, zhang, -1, 0, 30))
    check("多参考队评估 ok", refs.get("ok") is True and refs.get("referenceCount", 0) >= 1,
          str(refs)[:240])
    check("多参考队含均值和最低值", refs.get("minimumWinRate", -1) <= refs.get("averageWinRate", -1) <= 1,
          str(refs)[:240])
    refs_again = json.loads(call(lib, lib.evaluate_team_references, liu, guan, zhang, -1, 0, 30))
    check("多参考队种子可复现", refs.get("references") == refs_again.get("references"),
          str((refs, refs_again))[:240])

    print("== 满级战法 / 红度评估 ==")
    max_tactic = json.loads(call(lib, lib.get_tactic_max_level, "横扫千军".encode()))
    check("满级战法查询", max_tactic.get("ok") is True and max_tactic.get("level") == 10 and
          max_tactic.get("maxLevelDataAvailable") is True and
          max_tactic.get("combatValuesExact") is not False and
          max_tactic.get("numericMultiplier") == 1 and
          "100%" in max_tactic.get("maxLevel", {}).get("description", ""), str(max_tactic))
    red = json.loads(call(lib, lib.evaluate_team_stars, liu, guan, zhang, 5, 4, 3))
    check("红度评估 ok", red.get("ok") is True and red.get("redStars") == [5, 4, 3], str(red))
    check("红度进入战斗", red.get("battle", {}).get("sims") == 200 and
          red.get("redStarAttributeMultiplier") == [1, 1, 1] and
          red.get("redDamageBonusPercent") == [15, 12, 9] and
          red.get("redDamageReductionPercent") == [15, 12, 9] and
          "battleWinRateWithRedStars" in red, str(red))

    print("== 红度自由属性点 ==")
    build = {"heroes": [
        {"id": liu, "stars": 5, "freeAttributes": {"intellect": 10}},
        {"id": guan, "stars": 4, "freeAttributes": {"force": 6, "command": 4}},
        {"id": zhang, "stars": 3, "freeAttributes": {"force": 10}},
    ]}
    built = json.loads(call(lib, lib.evaluate_team_build, json.dumps(build, ensure_ascii=False).encode("utf-8")))
    check("自由属性点评估 ok", built.get("ok") is True and
          built.get("freeAttributes") == [[0, 10, 0, 0], [6, 0, 4, 0], [10, 0, 0, 0]], str(built))
    over = dict(build)
    over["heroes"] = list(build["heroes"])
    over["heroes"][0] = {"id": liu, "stars": 5, "freeAttributes": {"force": 11}}
    over_rep = json.loads(call(lib, lib.evaluate_team_build, json.dumps(over, ensure_ascii=False).encode("utf-8")))
    check("自由属性点超额拒绝", over_rep.get("ok") is False, str(over_rep))

    print("== 实战战法推荐 ==")
    tactic_recs = json.loads(call(lib, lib.recommend_tactics, guan, liu, zhang, 3, 30))
    check("战法推荐 ok", tactic_recs.get("ok") is True and
          len(tactic_recs.get("recommendations", [])) == 3, str(tactic_recs)[:240])
    if tactic_recs.get("recommendations"):
        check("战法推荐含实战证据", all("winRate" in x and "evidence" in x
                                   for x in tactic_recs["recommendations"]), str(tactic_recs))

    print("== 本地账号 / 限定推荐 ==")
    account = json.loads(call(lib, lib.create_local_account, "测试账号".encode()))
    account_id = account.get("id", "").encode()
    check("创建本地账号", account.get("ok") is True and account_id, str(account))
    for hid, stars in ((liu, 5), (guan, 3), (zhang, 1)):
        owned = json.loads(call(lib, lib.set_local_account_hero, account_id, hid, stars, 1))
        check(f"账号加入武将 {hid}", owned.get("ok") is True, str(owned))
    owned = json.loads(call(lib, lib.get_local_account, account_id))
    check("账号保留红度", {x["stars"] for x in owned.get("heroes", [])} == {1, 3, 5}, str(owned))
    for tactic in ("盛气凌敌", "横扫千军", "刮骨疗毒", "暂避其锋", "一骑当千"):
        updated = json.loads(call(lib, lib.set_local_account_tactic, account_id, tactic.encode(), 1))
        check(f"账号加入战法 {tactic}", updated.get("ok") is True, str(updated))
    owned = json.loads(call(lib, lib.get_local_account, account_id))
    check("账号保留战法池", {x["name"] for x in owned.get("tactics", [])} ==
          {"盛气凌敌", "横扫千军", "刮骨疗毒", "暂避其锋", "一骑当千"}, str(owned))
    invalid_tactic = json.loads(call(lib, lib.set_local_account_tactic, account_id, "刘备自带战法".encode(), 1))
    check("拒绝无效账号战法", invalid_tactic.get("ok") is False, str(invalid_tactic))
    own_recs = json.loads(call(lib, lib.recommend_account_teams, account_id, 3))
    check("账号限定推荐", own_recs.get("ok") is True and own_recs.get("tacticPoolSize") == 5 and
          len(own_recs.get("recommendations", [])) == 1,
          str(own_recs)[:240])
    if own_recs.get("recommendations"):
        check("推荐仅含已拥有武将", set(own_recs["recommendations"][0]["heroes"]) == {liu, guan, zhang},
              str(own_recs))
        allowed_tactics = {"盛气凌敌", "横扫千军", "刮骨疗毒", "暂避其锋", "一骑当千"}
        equipped = {name for slots in own_recs["recommendations"][0].get("tactics", []) for name in slots}
        check("推荐仅用账号战法池", equipped <= allowed_tactics, str(own_recs))
    account_file = os.path.join(ROOT, "build", "test_local_accounts.json")
    saved = json.loads(call(lib, lib.save_local_accounts, account_file.encode()))
    check("本地账号保存", saved.get("ok") is True and os.path.exists(account_file), str(saved))
    loaded_accounts = json.loads(call(lib, lib.load_local_accounts, account_file.encode()))
    check("本地账号加载", loaded_accounts.get("ok") is True and loaded_accounts.get("accounts"), str(loaded_accounts))
    reloaded = json.loads(call(lib, lib.get_local_account, account_id))
    check("账号加载后保留战法池", {x["name"] for x in reloaded.get("tactics", [])} ==
          {"盛气凌敌", "横扫千军", "刮骨疗毒", "暂避其锋", "一骑当千"}, str(reloaded))

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
    check("推荐已返回主将", r0.get("mainHeroId") == r0.get("heroes", [None])[0], str(r0)[:180])
    all_tactics = [name for slots in r0.get("tactics", []) for name in slots]
    type_by_name = {t["name"]: t.get("type") for t in tactics}
    check("推荐队伍阵法/兵种战法唯一", sum(1 for name in all_tactics if type_by_name.get(name) == "阵法") <= 1 and
          sum(1 for name in all_tactics if type_by_name.get(name) == "兵种") <= 1, str(r0.get("tactics")))

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
