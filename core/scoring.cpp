#include "scoring.hpp"
#include <cstdio>

namespace sgz {

namespace {
bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
}

HeroRole classifyHeroRole(const Hero& h) {
    HeroRole r;
    r.innateType = h.innate.type;
    r.innateDesc = h.innate.description;
    const std::string& d = h.innate.description;
    const std::string& t = h.innate.type;
    double ig = h.iGrow, fg = h.fGrow;

    bool hasHeal = has(d, "治疗") || has(d, "恢复") || has(d, "急救");
    bool hasPhysDmg = has(d, "兵刃") || has(d, "猛攻") || has(d, "猛击");
    bool hasMagicDmg = has(d, "谋略") || has(d, "灼烧") || has(d, "水攻") || has(d, "中毒") || has(d, "溃逃");
    bool hasControl = has(d, "缴械") || has(d, "計穷") || has(d, "计穷") || has(d, "震慑") ||
                      has(d, "混乱") || has(d, "虚弱") || has(d, "禁疗");
    bool hasTank = has(d, "承担") || has(d, "分担") || has(d, "警戒") || has(d, "援护") ||
                   has(d, "嘲讽") || has(d, "减伤") || has(d, "受伤害");

    bool intCast = has(d, "受智力") || has(d, "谋略") || has(d, "治疗率");

    if (hasHeal && ig >= 1.8) {
        r.role = "治疗";
        r.advice = "全智力（治疗量受智力加成）";
    } else if (hasMagicDmg && ig >= 2.0) {
        r.role = "谋略输出";
        r.advice = "全智力（谋略伤害吃智力差）";
    } else if (hasPhysDmg && fg >= 1.5) {
        r.role = "兵刃输出";
        r.advice = "全武力（兵刃伤害吃武力差）";
    } else if (hasControl && (fg >= 1.0 || ig >= 1.5)) {
        r.role = "控制";
        r.advice = "主属性 + 补速度（先手控制）";
    } else if (hasTank && h.cGrow >= 1.5) {
        r.role = "坦克";
        r.advice = "全统率（提升免伤与自愈）";
    } else if (hasMagicDmg || ig >= 2.0) {
        r.role = "谋略输出";
        r.advice = "全智力";
    } else if (hasPhysDmg || fg >= 1.5) {
        r.role = "兵刃输出";
        r.advice = "全武力";
    } else if (hasTank) {
        r.role = "坦克";
        r.advice = "全统率";
    } else if (hasHeal) {
        r.role = "治疗";
        r.advice = intCast ? "全智力" : "视战法加成加属性";
    } else {
        r.role = "辅助";
        r.advice = intCast ? "全智力" : "主属性 + 补速";
    }
    return r;
}

TroopType bestTroopType(const Hero* const h[3]) {
    double best = -1;
    TroopType bt = T_SPEAR;
    for (int t = 0; t < T_SIEGE; t++) { // 骑/盾/弓/枪
        double sum = 0;
        for (int i = 0; i < 3; i++) sum += aptitudeMult(h[i]->aptitudeOf((TroopType)t));
        if (sum > best) { best = sum; bt = (TroopType)t; }
    }
    return bt;
}

RuleScore ruleScore(const TeamConfig& tc) {
    RuleScore rs;
    int costSum = 0;
    for (int i = 0; i < 3; i++) costSum += tc.hero[i]->cost;
    rs.costSum = costSum;
    if (costSum > 20) {
        rs.costOverflow = true;
        rs.cost = 0;
    } else {
        rs.cost = 100;
    }

    // 适性：最佳兵种平均适性比例（S=1.2）
    TroopType bt = bestTroopType(tc.hero);
    double sum = 0;
    for (int i = 0; i < 3; i++) sum += aptitudeMult(tc.hero[i]->aptitudeOf(bt));
    double avgRatio = sum / 3.0 / 1.2; // 1.0 = 全员 S
    if (avgRatio > 1) avgRatio = 1;
    rs.aptitude = avgRatio * 100;
    // 核心输出适性检查：主将或最高武力/智力者的适性
    // （简化：队伍平均已覆盖）

    // 阵营：国家队 10%
    if (tc.hero[0]->kingdom == tc.hero[1]->kingdom &&
        tc.hero[1]->kingdom == tc.hero[2]->kingdom) {
        rs.kingdom = 100;
    } else if (tc.hero[0]->kingdom == tc.hero[1]->kingdom ||
               tc.hero[1]->kingdom == tc.hero[2]->kingdom ||
               tc.hero[0]->kingdom == tc.hero[2]->kingdom) {
        rs.kingdom = 50;
    }

    // 角色覆盖：输出 + 保护 + 续航
    bool hasDps = false, hasTank = false, hasHeal = false;
    for (int i = 0; i < 3; i++) {
        HeroRole r = classifyHeroRole(*tc.hero[i]);
        if (r.role.find("输出") != std::string::npos) hasDps = true;
        if (r.role == "坦克") hasTank = true;
        if (r.role == "治疗") hasHeal = true;
    }
    int covered = (hasDps ? 1 : 0) + (hasTank ? 1 : 0) + (hasHeal ? 1 : 0);
    rs.role = (double)covered * 100.0 / 3.0;

    rs.total = 0.35 * rs.aptitude + 0.25 * rs.kingdom + 0.25 * rs.role + 0.15 * rs.cost;
    if (rs.costOverflow) rs.total *= 0.2; // 超统御重罚
    return rs;
}

std::vector<std::string> detectSynergies(const TeamConfig& tc) {
    std::vector<std::string> out;
    std::vector<std::string> names;
    for (int i = 0; i < 3; i++) {
        names.push_back(tc.hero[i]->innate.name);
        for (const Tactic* t : tc.slots[i]) if (t) names.push_back(t->name);
    }
    auto hasT = [&](const char* n) {
        for (auto& x : names) if (x == n) return true;
        return false;
    };

    if (hasT("盛气凌敌") && hasT("横扫千军"))
        out.push_back("盛气凌敌 → 横扫千军：前2回合缴械，横扫千军对缴械/计穷目标追加震慑");
    if (hasT("盛气凌敌") && hasT("燕人咆哮"))
        out.push_back("盛气凌敌 → 燕人咆哮：缴械后燕人咆哮对缴械目标降统率25%（破防）");
    if (hasT("火烧连营") && hasT("风助火势"))
        out.push_back("火烧连营 → 风助火势：灼烧点燃后风助火势对灼烧目标追加谋略伤害");
    // 通用灼烧体系：队伍中有灼烧施加者 + 火伤承手
    {
        bool hasBurn = false;
        for (auto& n : names) {
            const Tactic* t = store().tacticByName(n);
            if (t && has(t->description, "灼烧")) hasBurn = true;
        }
        if (hasBurn && hasT("风助火势"))
            out.push_back("灼烧体系：队伍战法可施加灼烧，配合风助火势引爆火伤");
    }
    if (hasT("暂避其锋") && hasT("刮骨疗毒"))
        out.push_back("暂避其锋 → 刮骨疗毒：减伤 + 高额治疗 + 净化，续航保障");
    if (hasT("锋矢阵") && hasT("一骑当千"))
        out.push_back("锋矢阵 → 一骑当千：主将增伤配合突击爆发");
    return out;
}

std::vector<std::string> buildAdvice(const TeamConfig& tc, const BattleStats& st, const RuleScore& rs) {
    std::vector<std::string> adv;
    std::string nat = tc.sameKingdom() ? "是" : "否";
    adv.push_back("国家（协力 +10%）：" + nat);
    adv.push_back("最佳兵种：" + std::string(troopNameCN(tc.troop)));
    if (rs.costOverflow) {
        adv.push_back("⚠ 统御超限：" + std::to_string(rs.costSum) + "/20，无法上场");
    } else {
        adv.push_back("统御：" + std::to_string(rs.costSum) + "/20（" + (rs.costSum <= 20 ? "合规" : "超限") + "）");
    }
    // 角色覆盖
    bool hasDps = false, hasTank = false, hasHeal = false;
    for (int i = 0; i < 3; i++) {
        HeroRole r = classifyHeroRole(*tc.hero[i]);
        if (r.role.find("输出") != std::string::npos) hasDps = true;
        if (r.role == "坦克") hasTank = true;
        if (r.role == "治疗") hasHeal = true;
    }
    if (!hasDps) adv.push_back("⚠ 缺少明确输出核心");
    if (!hasTank) adv.push_back("⚠ 缺少承伤/坦克位");
    if (!hasHeal) adv.push_back("⚠ 缺少治疗/续航");
    if (st.sims > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "战斗模拟（%d场，多参考队）：胜率 %.1f%%，战损比 %.2f，战损评分 %.1f",
                 st.sims, st.winRate * 100, st.avgCasualtyRatio, st.casualtyScore);
        adv.push_back(buf);
    }
    return adv;
}

double finalScore(double winRate, const RuleScore& rs) {
    double battle = winRate * 100.0;
    double s = 0.70 * battle + 0.30 * rs.total;
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    return s;
}

double finalScore(const BattleStats& battle, const RuleScore& rs) {
    // 战损比是实战主指标：同样获胜时，消耗更少兵力的队伍得分更高。
    // 规则分仅用于统御、适性等无法由短回合战斗稳定体现的约束。
    double s = 0.80 * battle.casualtyScore + 0.20 * rs.total;
    if (s < 0) s = 0;
    if (s > 100) s = 100;
    return s;
}

} // namespace sgz
