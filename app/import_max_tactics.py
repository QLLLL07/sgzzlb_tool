#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从满级战法资料生成运行时可加载的结构化战法等级数据。

输入资料保持为 Markdown，便于人工校对和补充来源；输出 JSON 只承载
名称、Lv10 效果摘要、可靠性和版本冲突标记，供数据生成器和 C++ 核心使用。
"""
import json
import os
import re


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "三国志战略版_满级战法数值大全.md")
OUTPUT = os.path.join(ROOT, "data", "tactic_max_levels.json")


def cells(line: str) -> list[str]:
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def normalized_name(name: str) -> str:
    """兵种小节用“战法（兵种）”展示，运行时按基础战法名匹配。"""
    return re.sub(r"（[骑盾弓枪器械]+）$", "", name)


def main() -> int:
    rows = []
    seen = set()
    with open(SOURCE, encoding="utf-8") as source:
        for line in source:
            if not line.startswith("|"):
                continue
            row = cells(line)
            if len(row) < 3 or row[0] in {"战法", "等级", "战法点"}:
                continue
            if not row[0] or set(row[0]) <= {"-", ":", " "}:
                continue
            name = normalized_name(row[0])
            if name in seen:
                continue
            seen.add(name)
            summary = row[2]
            evidence = "".join(row[3:])
            rows.append({
                "name": name,
                "displayName": row[0],
                "level": 10,
                "description": summary,
                "reliability": "verified" if "✅" in evidence else "partial",
                "versionConflict": "版本冲突" in summary or "🔴" in summary,
            })

    with open(OUTPUT, "w", encoding="utf-8") as target:
        json.dump({
            "source": "三国志战略版_满级战法数值大全.md",
            "collectedAt": "2026-08-28",
            "entries": rows,
        }, target, ensure_ascii=False, indent=2)
        target.write("\n")
    print(f"[import_max_tactics] {OUTPUT}: {len(rows)} Lv10 entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
