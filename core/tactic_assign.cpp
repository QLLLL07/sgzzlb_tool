// tactic_assign.cpp - 战法配装实现。
// 思路：对每个候选（武将×传承战法）打契合分，贪心逐槽分配；
//       加分规则覆盖角色契合 + 战法联动（盛气凌敌→横扫千军 等）。
#include "tactic_assign.hpp"
#include "effects.hpp"
#include <algorithm>
#include <unordered_map>

namespace sgz {

namespace {

bool isControlStatus(St s) {
    return s == St::JI_QIONG || s == St::JIE_XIE || s == St::ZHEN_SHE ||
           s == St::HUN_LUAN || s == St::XU_RUO || s == St::JIN_LIAO;
}

// 战法效果摘要（用于配装打分）
struct EffStat {
    double dmg = 0;       // 最高伤害率
    double heal = 0;      // 最高治疗率
    double trigger = 0;   // 发动率 0..1
    bool charge = false;  // 需要准备 1 回合
    bool defUp = false;   // 减伤
    bool guard = false;   // 抵御/警戒
    bool atkUp = false;   // 增伤
    bool control = false; // 控制
    bool cleanse = false; // 净化
    bool counter = false; // 反击
    bool boost = false;   // 发动率提升
    bool burn = false;    // 施加灼烧
    bool intScal = false; // 受智力加成
};

EffStat analyze(const Tactic& t) {
    EffStat s;
    TacticEffects te = parseTacticEffects(t);
    s.trigger = te.triggerRate;
    s.charge = te.needsCharge;
    auto collect = [&](const Effect& e) {
        if (isDamage(e)) s.dmg = std::max(s.dmg, e.rate);
        if (e.kind == Effect::E_HEAL) s.heal = std::max(s.heal, e.rate);
        if (e.kind == Effect::E_DEF_UP) s.defUp = true;
        if (e.kind == Effect::E_GUARD) s.guard = true;
        if (e.kind == Effect::E_ATK_UP) s.atkUp = true;
        if (e.kind == Effect::E_STATUS) {
            for (St st : e.statuses) {
                if (st == St::BURN) s.burn = true;
                if (isControlStatus(st)) s.control = true;
            }
        }
        if (e.kind == Effect::E_CLEANSE) s.cleanse = true;
        if (e.kind == Effect::E_COUNTER) s.counter = true;
        if (e.kind == Effect::E_TRIGGER_BOOST) s.boost = true;
        if (e.intScaling) s.intScal = true;
    };
    for (const auto& e : te.cast) collect(e);
    for (const auto& e : te.permanent) collect(e);
    for (const auto& sch : te.scheduled)
        for (const auto& e : sch.effects) collect(e);
    return s;
}

// 解析结果按战法名缓存（recommend 大量调用时复用）
static std::unordered_map<std::string, EffStat> g_effCache;

const EffStat& analyzeCached(const Tactic& t) {
    auto it = g_effCache.find(t.name);
    if (it == g_effCache.end()) it = g_effCache.emplace(t.name, analyze(t)).first;
    return it->second;
}

// 联动加分：候选战法与「队伍已有战法（含三武将自带）」的经典组合
double synergyBonus(const Tactic* t, const std::vector<std::string> got[3], const Hero* h[3]) {
    std::string cand = t->name;
    auto teamHas = [&](const char* n) -> bool {
        for (int k = 0; k < 3; k++) {
            if (h[k] && h[k]->innate.name == n) return true;
            for (auto& g : got[k]) if (g == n) return true;
        }
        return false;
    };
    double b = 0;
    if (cand == "横扫千军" && teamHas("盛气凌敌")) b += 40;
    if (cand == "盛气凌敌" && teamHas("横扫千军")) b += 40;
    if (cand == "风助火势" && teamHas("火烧连营")) b += 35;
    if (cand == "刮骨疗毒" && teamHas("暂避其锋")) b += 30;
    if (cand == "暂避其锋" && teamHas("刮骨疗毒")) b += 30;
    if (cand == "一骑当千" && teamHas("虎豹骑")) b += 25; // 突击体系
    if (cand == "虎豹骑" && teamHas("一骑当千")) b += 25;
    return b;
}

} // namespace

// 预热：把全部战法的效果摘要写入缓存（此后只读，多线程安全）
void warmTacticCache() {
    for (const Tactic& t : store().tactics) analyzeCached(t);
}

double tacticFitScore(const Hero& h, const HeroRole& role, const Tactic& t) {
    const EffStat& s = analyzeCached(t);
    const std::string& r = role.role;
    double score = 0;
    if (r == "兵刃输出") {
        score = s.dmg;
        if (t.type == "突击") score += 12;      // 突击高频
        if (t.type == "主动") score += 6;
        if (s.atkUp) score += 8;
    } else if (r == "谋略输出") {
        score = s.dmg;
        if (s.intScal) score += 15;             // 吃智力成长
        if (t.type == "主动") score += 6;
        if (s.atkUp) score += 8;
    } else if (r == "坦克") {
        score = (s.defUp ? 45 : 0) + (s.guard ? 30 : 0) +
                (s.counter ? 25 : 0) + (s.control ? 18 : 0) + s.dmg * 0.3;
    } else if (r == "治疗") {
        score = s.heal + (s.cleanse ? 40 : 0) + (s.defUp ? 20 : 0) +
                (s.control ? 12 : 0);
    } else if (r == "控制") {
        score = (s.control ? 65 : 0) + (s.boost ? 20 : 0) + s.dmg * 0.3;
    } else { // 辅助
        score = (s.atkUp ? 30 : 0) + (s.defUp ? 30 : 0) + (s.cleanse ? 25 : 0) +
                (s.boost ? 25 : 0) + (s.control ? 20 : 0) + s.heal * 0.5 + s.dmg * 0.2;
    }
    if (t.type == "主动") score += s.trigger * 25.0; // 高发动率优先
    if (s.charge) score += 5;                        // 蓄力型伤害高
    return score;
}

void assignTactics(TeamConfig& tc) {
    auto& st = store();
    int assigned[3] = {0, 0, 0};
    std::vector<std::string> got[3];
    std::vector<const Tactic*> used;

    auto isUsed = [&](const Tactic* t) {
        for (const Tactic* u : used) if (u->name == t->name) return true;
        return false;
    };

    // 每次迭代为「当前最优（武将×战法）」填一个槽，最多 6 槽
    for (int round = 0; round < 6; round++) {
        int bestIdx = -1;
        const Tactic* bestT = nullptr;
        double bestScore = -1e9;
        for (int i = 0; i < 3; i++) {
            if (assigned[i] >= 2) continue;
            const Hero& h = *tc.hero[i];
            HeroRole role = classifyHeroRole(h);
            for (const Tactic& t : st.tactics) {
                if (!t.isInheritable() || !t.isCombat()) continue;
                if (!t.fitsTroop(tc.troop)) continue;
                if (t.name == h.innate.name) continue;              // 不用自带
                if (h.hasInherit && t.name == h.inherit.name) continue; // 不用自身传承
                if (isUsed(&t)) continue;
                double s = tacticFitScore(h, role, t) + synergyBonus(&t, got, tc.hero);
                if (s > bestScore) { bestScore = s; bestIdx = i; bestT = &t; }
            }
        }
        if (!bestT) break;
        tc.slots[bestIdx].push_back(bestT);
        used.push_back(bestT);
        got[bestIdx].push_back(bestT->name);
        assigned[bestIdx]++;
    }
}

} // namespace sgz
