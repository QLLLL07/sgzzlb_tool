#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""main.py - 《三国志·战略版》配将工具 GUI（tkinter）。

功能：英雄筛选 → 3 槽位选将 → 兵种选择 → 评估（评分分解/战斗统计/战法配装/联动/建议）
      → 推荐 Top-N 表格（双击载入槽位）→ 数据重载。
C++ 计算均在后台线程执行，界面不卡顿。
"""
import os
import sys
import threading
import tkinter as tk
from tkinter import ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from core_bridge import CoreBridge, CoreBridgeError, resource_root

DEFAULT_DATA = os.path.join(resource_root(), "data", "data.json")

TROOP_OPTIONS = [("自动", -1), ("骑兵", 0), ("盾兵", 1), ("弓兵", 2), ("枪兵", 3)]
KINGDOM_OPTIONS = ["全部", "魏", "蜀", "吴", "群", "晋", "汉"]
ROLE_OPTIONS = ["全部", "兵刃输出", "谋略输出", "坦克", "治疗", "控制", "辅助"]
COST_OPTIONS = ["全部", 15, 16, 17, 18, 19, 20]


class App:
    def __init__(self, root, bridge):
        self.root = root
        self.bridge = bridge
        self.slots = [None, None, None]   # 每个槽位存 hero dict 或 None
        self.active_slot = 0
        self.filtered = []
        self.rec_cache = {}               # 推荐表 iid -> RecommendEntry
        self._busy = False

        root.title("三国志·战略版 配将工具")
        root.geometry("1180x720")

        self._build_ui()
        self.set_status("正在加载数据...")
        self._bg(self._do_load, self._on_loaded)

    # ---------------- UI 构建 ----------------
    def _build_ui(self):
        top = ttk.Frame(self.root, padding=6)
        top.pack(side=tk.TOP, fill=tk.X)
        self.data_var = tk.StringVar(value=DEFAULT_DATA)
        ttk.Entry(top, textvariable=self.data_var, width=52).pack(side=tk.LEFT, padx=(0, 6))
        ttk.Button(top, text="重载数据", command=self.reload).pack(side=tk.LEFT)

        body = ttk.Panedwindow(self.root, orient=tk.HORIZONTAL)
        body.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        # ---- 左：筛选 + 英雄列表 ----
        left = ttk.Frame(body)
        body.add(left, weight=1)

        filt = ttk.LabelFrame(left, text="筛选", padding=6)
        filt.pack(fill=tk.X)
        row1 = ttk.Frame(filt)
        row1.pack(fill=tk.X)
        self.king_var = tk.StringVar(value="全部")
        self.role_var = tk.StringVar(value="全部")
        self.cost_var = tk.StringVar(value="全部")
        self.search_var = tk.StringVar()
        ttk.Label(row1, text="阵营").pack(side=tk.LEFT)
        ttk.Combobox(row1, textvariable=self.king_var, values=KINGDOM_OPTIONS,
                     width=5, state="readonly").pack(side=tk.LEFT, padx=(0, 6))
        ttk.Label(row1, text="定位").pack(side=tk.LEFT)
        ttk.Combobox(row1, textvariable=self.role_var, values=ROLE_OPTIONS,
                     width=9, state="readonly").pack(side=tk.LEFT, padx=(0, 6))
        ttk.Label(row1, text="统御≤").pack(side=tk.LEFT)
        ttk.Combobox(row1, textvariable=self.cost_var, values=COST_OPTIONS,
                     width=5, state="readonly").pack(side=tk.LEFT)
        row2 = ttk.Frame(filt)
        row2.pack(fill=tk.X, pady=(4, 0))
        ttk.Label(row2, text="搜索").pack(side=tk.LEFT)
        ttk.Entry(row2, textvariable=self.search_var, width=20).pack(side=tk.LEFT, fill=tk.X, expand=True)

        for w in (self.king_var, self.role_var, self.cost_var, self.search_var):
            w.trace_add("write", lambda *a: self._apply_filter())

        ttk.Label(left, text="武将列表（双击选入当前槽位）").pack(anchor=tk.W, pady=(8, 2))
        listwrap = ttk.Frame(left)
        listwrap.pack(fill=tk.BOTH, expand=True)
        self.hero_list = tk.Listbox(listwrap, font=("TkDefaultFont", 10))
        self.hero_list.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        sb = ttk.Scrollbar(listwrap, orient=tk.VERTICAL, command=self.hero_list.yview)
        sb.pack(side=tk.RIGHT, fill=tk.Y)
        self.hero_list.config(yscrollcommand=sb.set)
        self.hero_list.bind("<Double-Button-1>", lambda e: self.assign_from_list())

        # ---- 右：槽位 + 结果 ----
        right = ttk.Frame(body)
        body.add(right, weight=2)

        slotrow = ttk.Frame(right)
        slotrow.pack(fill=tk.X)
        ttk.Label(slotrow, text="阵容槽位：").pack(side=tk.LEFT)
        self.slot_btns = []
        for i in range(3):
            b = ttk.Button(slotrow, text=f"槽{i + 1}: 空", width=22,
                           command=lambda k=i: self.set_active_slot(k))
            b.pack(side=tk.LEFT, padx=2)
            self.slot_btns.append(b)
        self.set_active_slot(0)

        ctl = ttk.Frame(right)
        ctl.pack(fill=tk.X, pady=6)
        ttk.Label(ctl, text="兵种").pack(side=tk.LEFT)
        self.troop_var = tk.StringVar(value="自动")
        ttk.Combobox(ctl, textvariable=self.troop_var, values=[t[0] for t in TROOP_OPTIONS],
                     width=6, state="readonly").pack(side=tk.LEFT, padx=(2, 10))
        ttk.Label(ctl, text="主将").pack(side=tk.LEFT)
        self.main_idx_var = tk.StringVar(value="槽位 1")
        ttk.Combobox(ctl, textvariable=self.main_idx_var, values=["槽位 1", "槽位 2", "槽位 3"],
                     width=6, state="readonly").pack(side=tk.LEFT, padx=(2, 10))
        ttk.Button(ctl, text="评估", command=self.evaluate).pack(side=tk.LEFT, padx=2)
        ttk.Button(ctl, text="推荐 Top10", command=self.recommend).pack(side=tk.LEFT, padx=2)
        ttk.Button(ctl, text="清空槽位", command=self.clear_slots).pack(side=tk.LEFT, padx=2)

        nb = ttk.Notebook(right)
        nb.pack(fill=tk.BOTH, expand=True)
        self.nb = nb

        # 评估结果
        rep_frame = ttk.Frame(nb)
        nb.add(rep_frame, text="评估结果")
        self.rep_text = tk.Text(rep_frame, wrap=tk.WORD, font=("TkDefaultFont", 10))
        self.rep_text.pack(fill=tk.BOTH, expand=True)
        rsb = ttk.Scrollbar(rep_frame, orient=tk.VERTICAL, command=self.rep_text.yview)
        rsb.pack(side=tk.RIGHT, fill=tk.Y)
        self.rep_text.config(yscrollcommand=rsb.set)

        # 推荐
        rec_frame = ttk.Frame(nb)
        nb.add(rec_frame, text="推荐 Top-N")
        self.rec_tree = ttk.Treeview(
            rec_frame,
            columns=("rank", "total", "win", "draw", "error", "rule", "troop", "cost", "team"),
            show="headings")
        self.rec_tree.heading("rank", text="排名")
        self.rec_tree.heading("total", text="综合分")
        self.rec_tree.heading("win", text="胜率%")
        self.rec_tree.heading("draw", text="平局%")
        self.rec_tree.heading("error", text="误差%")
        self.rec_tree.heading("rule", text="规则分")
        self.rec_tree.heading("troop", text="兵种")
        self.rec_tree.heading("cost", text="统御")
        self.rec_tree.heading("team", text="阵容")
        self.rec_tree.column("rank", width=46, anchor="center")
        self.rec_tree.column("total", width=56, anchor="center")
        self.rec_tree.column("win", width=56, anchor="center")
        self.rec_tree.column("draw", width=56, anchor="center")
        self.rec_tree.column("error", width=56, anchor="center")
        self.rec_tree.column("rule", width=56, anchor="center")
        self.rec_tree.column("troop", width=46, anchor="center")
        self.rec_tree.column("cost", width=46, anchor="center")
        self.rec_tree.column("team", width=430, anchor="w")
        self.rec_tree.pack(fill=tk.BOTH, expand=True)
        self.rec_tree.bind("<Double-1>", self.load_rec_from_tree)

        # 状态栏
        self.status_var = tk.StringVar(value="就绪")
        ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN,
                  anchor=tk.W).pack(side=tk.BOTTOM, fill=tk.X)

    # ---------------- 槽位 ----------------
    def set_active_slot(self, i):
        self.active_slot = i
        for k, b in enumerate(self.slot_btns):
            hero = self.slots[k]
            text = f"槽{k + 1}: {hero['name']}" if hero else f"槽{k + 1}: 空"
            b.config(text=text)
            if k == i:
                b.state(["pressed"])
            else:
                b.state(["!pressed"])

    def assign_from_list(self):
        sel = self.hero_list.curselection()
        if not sel:
            return
        hero = self.filtered[sel[0]]
        self.slots[self.active_slot] = hero
        self.set_active_slot(self.active_slot)

    def clear_slots(self):
        self.slots = [None, None, None]
        self.set_active_slot(0)

    # ---------------- 数据 ----------------
    def _do_load(self):
        return self.bridge.load_data(self.data_var.get() or DEFAULT_DATA)

    def _on_loaded(self, r):
        if isinstance(r, tuple) and r and r[0] == "__err__":
            self.set_status(f"加载失败: {r[1]}")
            return
        ok = r.get("ok")
        src = r.get("source")
        n = r.get("heroes")
        self._apply_filter()
        msg = f"数据已加载（{n} 武将，来源 {src}）" if ok else \
            f"外部数据加载失败，使用内置回退（{n} 武将）：{r.get('error')}"
        self.set_status(msg)

    def reload(self):
        self.set_status("正在重载数据...")
        self._bg(self._do_load, self._on_loaded)

    # ---------------- 评估 ----------------
    def evaluate(self):
        ids = [h["id"] for h in self.slots if h]
        if len(ids) < 3:
            self.set_status("请先在 3 个槽位选满武将")
            return
        troop = next(t[1] for t in TROOP_OPTIONS if t[0] == self.troop_var.get())
        main_idx = ["槽位 1", "槽位 2", "槽位 3"].index(self.main_idx_var.get())
        self.set_status("评估中...")
        self._bg(lambda: self.bridge.evaluate_team(ids[0], ids[1], ids[2], troop, main_idx),
                 self._show_report)

    def _show_report(self, rep):
        if isinstance(rep, tuple) and rep and rep[0] == "__err__":
            self.set_status(f"评估出错: {rep[1]}")
            return
        self.rep_text.delete("1.0", tk.END)
        if not rep.get("ok"):
            self.rep_text.insert(tk.END, f"评估失败：{rep.get('error')}\n")
            self.set_status("评估失败")
            return
        rs = rep["ruleScore"]
        b = rep["battle"]
        lines = []
        lines.append(f"综合评分：{rep['total']:.1f} / 100")
        lines.append(f"阵容：{' / '.join(rep['names'])}   主将：{rep.get('mainName', rep['names'][0])}   "
                     f"兵种：{rep['troop']}   统御：{rep['cost']}/20")
        lines.append("")
        lines.append("【评分分解】")
        lines.append(f"  兵种适性 {rs['aptitude']:.0f}  国家 {rs['kingdom']:.0f}  "
                     f"角色覆盖 {rs['role']:.0f}  统御 {rs['cost']:.0f}  → 规则分 {rs['total']:.1f}")
        lines.append("")
        lines.append("【武将定位】")
        for r in rep["roles"]:
            lines.append(f"  {r['name']}（{r['role']}）：{r['advice']}")
        lines.append("")
        lines.append("【战法配装】")
        for i, name in enumerate(rep["names"]):
            ts = "、".join(rep["tactics"][i]) if rep["tactics"][i] else "（无）"
            lines.append(f"  {name}：{ts}")
        if b.get("sims", 0) > 0:
            lines.append("")
            lines.append("【战斗统计】（vs 桃园）")
            lines.append(f"  模拟 {b['sims']} 场：胜率 {b['winRate'] * 100:.1f}%   "
                         f"场均输出 {b['avgDmgDealt']:.0f}  场均承伤 {b['avgDmgTaken']:.0f}")
            lines.append(f"  平局率 {b.get('drawRate', 0) * 100:.1f}%   "
                         f"胜率标准误 +/-{b.get('winRateStdError', 0) * 100:.1f}%")
            lines.append(f"  95%区间 [{b.get('winRateCi95Low', 0) * 100:.1f}%, "
                         f"{b.get('winRateCi95High', 0) * 100:.1f}%]   种子 {b.get('seed', 0)}")
            lines.append("  说明：结果仅表示当前简化规则下对固定桃园参考队的比较。")
        if rep.get("synergies"):
            lines.append("")
            lines.append("【战法联动】")
            for s in rep["synergies"]:
                lines.append(f"  • {s}")
        if rep.get("advice"):
            lines.append("")
            lines.append("【队伍建议】")
            for a in rep["advice"]:
                lines.append(f"  • {a}")
        self.rep_text.insert(tk.END, "\n".join(lines))
        self.set_status("评估完成")

    # ---------------- 推荐 ----------------
    def recommend(self):
        self.set_status("推荐计算中（约 2~3 秒）...")
        self._bg(lambda: self.bridge.recommend_teams(10), self._show_recommend)

    def _show_recommend(self, recs):
        if isinstance(recs, tuple) and recs and recs[0] == "__err__":
            self.set_status(f"推荐出错: {recs[1]}")
            return
        self.rec_tree.delete(*self.rec_tree.get_children())
        self.rec_cache.clear()
        for i, r in enumerate(recs):
            names = [self.bridge.hero_by_id(x)["name"] for x in r["heroes"]]
            if names:
                names[0] = f"主{names[0]}"
            team = " / ".join(names)
            iid = self.rec_tree.insert("", tk.END, values=(
                i + 1, f"{r['total']:.1f}", f"{r['winRate'] * 100:.1f}",
                f"{r.get('drawRate', 0) * 100:.1f}",
                f"{r.get('winRateStdError', 0) * 100:.1f}",
                f"{r['rule']:.1f}", r["troop"], r["cost"], team))
            self.rec_cache[iid] = r
        self.nb.select(1)
        self.set_status(f"推荐完成：Top {len(recs)} 队伍（双击表格行载入槽位）")

    def load_rec_from_tree(self, _evt):
        iid = self.rec_tree.focus()
        r = self.rec_cache.get(iid)
        if not r:
            return
        for i in range(3):
            self.slots[i] = self.bridge.hero_by_id(r["heroes"][i])
        self.main_idx_var.set("槽位 1")
        self.set_active_slot(0)
        self.nb.select(0)
        self.evaluate()

    # ---------------- 筛选 ----------------
    def _apply_filter(self):
        king = self.king_var.get()
        role = self.role_var.get()
        cost = self.cost_var.get()
        search = self.search_var.get().strip()
        cost_max = cost if isinstance(cost, int) else None
        self.filtered = []
        for h in self.bridge.heroes:
            if king != "全部" and h["kingdom"] != king:
                continue
            if role != "全部" and h["role"] != role:
                continue
            if cost_max is not None and h["cost"] > cost_max:
                continue
            if search and search not in h["name"]:
                continue
            self.filtered.append(h)
        self.hero_list.delete(0, tk.END)
        for h in self.filtered:
            self.hero_list.insert(tk.END,
                                  f"{h['name']}   {h['kingdom']}  {h['cost']}统  {h['role']}")

    # ---------------- 后台任务 ----------------
    def _bg(self, fn, cb):
        if self._busy:
            return
        self._busy = True

        def work():
            try:
                res = fn()
            except Exception as e:  # noqa: BLE001
                res = ("__err__", str(e))
            self.root.after(0, lambda: (cb(res), setattr(self, "_busy", False)))

        threading.Thread(target=work, daemon=True).start()

    def set_status(self, msg):
        self.status_var.set(msg)


def main():
    try:
        bridge = CoreBridge()
    except Exception as e:  # CoreBridgeError + 加载期 OSError（windowed exe 下不能静默退出）
        import tkinter.messagebox as mb
        mb.showerror("启动失败", str(e))
        return 1
    root = tk.Tk()
    App(root, bridge)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
