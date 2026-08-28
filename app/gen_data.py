#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成运行时数据：
1. data/data.json           —— 合并后的全量数据（含 Lv10 战法资料，C++ 库加载用）
2. core/builtin_fallback.hpp —— 内置回退数据（≥10 个示例武将 + 相关战法），
   当外部数据文件缺失时由 C++ 库加载。
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "data")
CORE = os.path.join(ROOT, "core")
MAX_LEVELS = os.path.join(DATA, "tactic_max_levels.json")

# 精选 12 个名将（覆盖四阵营 + 桃园/魏法/吴火等经典体系）
FALLBACK_HEROES = [
    "刘备", "关羽", "张飞",
    "曹操", "司马懿", "郭嘉", "贾诩",
    "陆逊", "吕蒙", "孙权",
    "吕布", "太史慈",
]

# 经典联动战法，回退库战法池（内置数据模式下战法配装可用）
FALLBACK_TACTICS = [
    "盛气凌敌", "横扫千军", "燕人咆哮", "风助火势", "火烧连营",
    "暂避其锋", "刮骨疗毒", "御敌屏障", "抚辑军民", "草船借箭",
    "锋矢阵", "八门金锁阵", "白眉", "太平道法", "一骑当千",
    "虎豹骑", "陷阵营", "青囊",
]


def cpp_string(raw: str) -> str:
    """转成合法的 C++ 字符串字面量（原始 UTF-8，双引号/反斜杠转义）。"""
    return raw.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def build_fallback(heroes, tactics):
    used_skills = set()
    for h in heroes:
        for sk in (h.get("innateSkill"), h.get("inheritSkill")):
            if sk:
                used_skills.add(sk["name"])
    chosen = [h for h in heroes if h["name"] in FALLBACK_HEROES]
    chosen_t = []
    seen = set()
    for name in FALLBACK_TACTICS:
        for t in tactics:
            if t["name"] == name and t["name"] not in seen:
                chosen_t.append(t)
                seen.add(t["name"])
    # 若被选武将的技能引用未在战法表，补充其自带技能定义
    for h in chosen:
        for sk in (h.get("innateSkill"), h.get("inheritSkill")):
            if not sk:
                continue
            if sk["name"] in seen:
                continue
            t = {"id": sk.get("id", ""), "name": sk["name"],
                 "type": sk.get("type", ""), "category": "自带",
                 "quality": sk.get("quality", "S"),
                 "triggerRate": sk.get("triggerRate", ""),
                 "validTroops": [], "sourceHero": h["name"],
                 "description": sk.get("description", "")}
            chosen_t.append(t)
            seen.add(sk["name"])
    return {"heroes": chosen, "tactics": chosen_t}


def main():
    heroes = json.load(open(os.path.join(DATA, "heroes.json"), encoding="utf-8"))
    tactics = json.load(open(os.path.join(DATA, "tactics.json"), encoding="utf-8"))
    if not os.path.exists(MAX_LEVELS):
        raise FileNotFoundError(f"缺少 Lv10 战法数据：{MAX_LEVELS}；请先运行 app/import_max_tactics.py")
    max_levels = json.load(open(MAX_LEVELS, encoding="utf-8"))

    # 1) 合并全量数据
    combined = {"heroes": heroes, "tactics": tactics,
                "tacticMaxLevels": max_levels.get("entries", [])}
    with open(os.path.join(DATA, "data.json"), "w", encoding="utf-8") as f:
        json.dump(combined, f, ensure_ascii=False, indent=1)

    # 2) 内置回退数据
    fb = build_fallback(heroes, tactics)
    js = json.dumps(fb, ensure_ascii=False, separators=(",", ":"))
    header = (
        "// builtin_fallback.hpp —— 由 app/gen_data.py 自动生成，请勿手改。\n"
        "// 内置回退数据（≥10 个示例武将 + 相关战法），外部 data.json 缺失时使用。\n"
        "#pragma once\n#include <string>\n"
        "namespace sgz {\n"
        "inline const std::string kBuiltinDataJson = \"" + cpp_string(js) + "\";\n"
        "} // namespace sgz\n"
    )
    with open(os.path.join(CORE, "builtin_fallback.hpp"), "w", encoding="utf-8") as f:
        f.write(header)

    print(f"[gen_data] data/data.json: {len(heroes)} heroes, {len(tactics)} tactics, "
          f"{len(max_levels.get('entries', []))} Lv10 tactic entries")
    print(f"[gen_data] builtin_fallback.hpp: {len(fb['heroes'])} heroes, {len(fb['tactics'])} tactics")
    return 0


if __name__ == "__main__":
    sys.exit(main())
