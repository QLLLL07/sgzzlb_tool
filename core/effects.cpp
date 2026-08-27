#include "effects.hpp"
#include <cctype>
#include <cstdlib>
#include <regex>
#include <unordered_map>

namespace sgz {

static const char* kStNames[] = {
    "无", "计穷", "缴械", "震慑", "混乱", "虚弱", "禁疗",
    "灼烧", "中毒", "水攻", "溃逃", "先攻", "遇袭", "连击", "必中", "破阵",
};
const char* stName(St s) { return kStNames[(int)s]; }

St stFromText(const std::string& kw) {
    if (kw.find("计穷") != std::string::npos) return St::JI_QIONG;
    if (kw.find("缴械") != std::string::npos || kw.find("繳械") != std::string::npos) return St::JIE_XIE;
    if (kw.find("震慑") != std::string::npos) return St::ZHEN_SHE;
    if (kw.find("混乱") != std::string::npos) return St::HUN_LUAN;
    if (kw.find("虚弱") != std::string::npos) return St::XU_RUO;
    if (kw.find("禁疗") != std::string::npos) return St::JIN_LIAO;
    if (kw.find("灼烧") != std::string::npos) return St::BURN;
    if (kw.find("中毒") != std::string::npos) return St::POISON;
    if (kw.find("水攻") != std::string::npos) return St::WATER;
    if (kw.find("溃逃") != std::string::npos) return St::KUI_TAO;
    if (kw.find("先攻") != std::string::npos) return St::FIRST_STRIKE;
    if (kw.find("遇袭") != std::string::npos) return St::YU_XI;
    if (kw.find("连击") != std::string::npos) return St::LINK_ATTACK;
    if (kw.find("必中") != std::string::npos) return St::BI_ZHONG;
    if (kw.find("破阵") != std::string::npos) return St::PO_JUN;
    return St::NONE;
}

namespace {

bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

double rateAfter(const std::string& s, const char* key) {
    size_t p = s.find(key);
    if (p == std::string::npos) return 0;
    size_t q = s.find('%', p);
    if (q == std::string::npos) return 0;
    std::string num;
    for (size_t k = p + 2; k < q; k++)   // 跳过 "率"
        if (isdigit((unsigned char)s[k]) || s[k] == '.') num += s[k];
    return num.empty() ? 0 : std::atof(num.c_str());
}

std::vector<double> percentsIn(const std::string& s) {
    std::vector<double> out;
    std::regex re("[0-9]+(?:\\.[0-9]+)?%");
    std::smatch m;
    std::string t = s;
    while (std::regex_search(t, m, re)) {
        out.push_back(std::atof(m.str(0).c_str()));
        t = m.suffix().str();
    }
    return out;
}

int firstNumber(const std::string& s, int def = 0) {
    size_t i = 0;
    while (i < s.size() && !isdigit((unsigned char)s[i])) i++;
    int v = 0;
    while (i < s.size() && isdigit((unsigned char)s[i])) { v = v * 10 + (s[i] - '0'); i++; }
    return (v == 0 && i == 0) ? def : v;
}

// 目标解析：onEnemy / count(0=全体) / selfOnly
void parseTarget(const std::string& clause, bool& onEnemy, int& count, bool& selfOnly) {
    onEnemy = contains(clause, "敌");
    selfOnly = contains(clause, "自身") || contains(clause, "自己");
    if (contains(clause, "全体")) count = 0;
    else if (contains(clause, "群体")) {
        int n = firstNumber(clause, 2);
        count = (n == 1) ? 2 : n;
    } else count = 1;
}

bool hasIntScaling(const std::string& clause) { return contains(clause, "受智力"); }
bool hasSpdScaling(const std::string& clause) { return contains(clause, "受速度"); }

int durationOf(const std::string& clause) {
    size_t p = clause.find("持续");
    if (p == std::string::npos) return 0;
    return firstNumber(clause.substr(p), 0);
}

enum DmgCls { DMG_NONE, DMG_PHYS, DMG_MAGIC, DMG_TRUE };
DmgCls dmgClassOf(const std::string& clause) {
    if (contains(clause, "兵刃") || contains(clause, "猛攻") || contains(clause, "猛击") ||
        contains(clause, "兵刃攻击"))
        return DMG_PHYS;
    if (contains(clause, "谋略") || contains(clause, "谋略攻击"))
        return DMG_MAGIC;
    if (contains(clause, "真实伤害")) return DMG_TRUE;
    return DMG_NONE;
}

// 单句分类 → 效果列表（可同时产出伤害 + 状态）
void classifyClause(const std::string& clause, std::vector<Effect>& out, std::string& note) {
    St st = stFromText(clause);
    bool onEnemy; int count; bool selfOnly;
    parseTarget(clause, onEnemy, count, selfOnly);
    int dur = durationOf(clause);
    double rate = rateAfter(clause, "伤害率");
    DmgCls dc = dmgClassOf(clause);
    double healRate = rateAfter(clause, "治疗率");
    bool intS = hasIntScaling(clause);
    std::vector<double> pcts = percentsIn(clause);

    // 1) 伤害（若有兵刃/谋略伤害率）
    if (dc != DMG_NONE && rate > 0) {
        Effect e;
        e.kind = dc == DMG_PHYS ? Effect::E_DMG_PHYS : dc == DMG_MAGIC ? Effect::E_DMG_MAGIC : Effect::E_TRUE_DMG;
        e.rate = rate;
        e.count = count;
        e.onEnemy = true;
        e.intScaling = intS;
        // 伤害句里的状态后缀（如 "...造成兵刃伤害，并使目标缴械"）
        if (st != St::NONE) {
            Effect s;
            s.kind = Effect::E_STATUS;
            s.statuses = {st};
            s.count = count;
            s.chance = 1.0;
            s.duration = dur > 0 ? dur : 1;
            s.onEnemy = true;
            if (isDotStatus(st)) { s.rate = rate; s.intScaling = intS; }
            out.push_back(s);
        }
        out.push_back(e);
        note = "伤害";
        return;
    }

    // 2) 状态（含 DoT：灼烧/中毒/水攻/溃逃 带伤害率）
    if (st != St::NONE) {
        Effect e;
        e.kind = Effect::E_STATUS;
        e.statuses = {st};
        e.count = count;
        e.onEnemy = onEnemy;
        e.selfOnly = selfOnly;
        e.duration = dur > 0 ? dur : 1;
        e.intScaling = intS;
        if (isDotStatus(st)) {
            e.rate = rate > 0 ? rate : 40;  // 缺省持续伤害率
        }
        e.chance = 1.0;
        for (double p : pcts) {
            if (p >= 1 && p <= 100 && (contains(clause, "概率") || contains(clause, "几率"))) {
                e.chance = p / 100.0;
                break;
            }
        }
        out.push_back(e);
        note = "状态";
        return;
    }

    // 3) 治疗
    if (healRate > 0 || contains(clause, "恢复兵力") || contains(clause, "恢复一定兵力") || contains(clause, "恢复其")) {
        Effect e;
        e.kind = Effect::E_HEAL;
        e.rate = healRate > 0 ? healRate : 30;
        e.count = count;
        e.onEnemy = false;
        e.selfOnly = selfOnly || contains(clause, "自身") || contains(clause, "自己") || contains(clause, "恢复兵力最多的我军单体");
        e.intScaling = intS;
        e.chance = 1.0;
        for (double p : pcts) {
            if (p >= 1 && p <= 100 && (contains(clause, "概率") || contains(clause, "几率"))) {
                e.chance = p / 100.0;
                break;
            }
        }
        out.push_back(e);
        note = "治疗";
        return;
    }

    // 4) A类增伤 / B类减伤 / 易伤
    if (contains(clause, "造成伤害") && (contains(clause, "提升") || contains(clause, "提高") || contains(clause, "增加"))) {
        Effect e; e.kind = Effect::E_ATK_UP;
        e.rate = pcts.empty() ? 0 : pcts.back();
        e.duration = dur > 0 ? dur : 8;
        e.onEnemy = false;
        e.selfOnly = selfOnly;
        out.push_back(e); note = "增伤"; return;
    }
    if (contains(clause, "受到伤害降低") || contains(clause, "受到兵刃伤害降低") || contains(clause, "受到谋略伤害降低")) {
        Effect e; e.kind = Effect::E_DEF_UP;
        e.rate = pcts.empty() ? 0 : pcts.back();
        e.duration = dur > 0 ? dur : 8;
        e.onEnemy = false;
        e.intScaling = intS;
        out.push_back(e); note = "减伤"; return;
    }
    if (contains(clause, "受到伤害增加") || contains(clause, "受到伤害提高")) {
        Effect e; e.kind = Effect::E_VULN;
        e.rate = pcts.empty() ? 0 : pcts.back();
        e.duration = dur > 0 ? dur : 8;
        e.onEnemy = true;
        out.push_back(e); note = "易伤"; return;
    }

    // 5) 属性增减
    static const char* statNames[] = {"武力", "智力", "统率", "速度"};
    for (int i = 0; i < 4; i++) {
        if (contains(clause, statNames[i])) {
            bool up = contains(clause, "提高") || contains(clause, "提升") || contains(clause, "增加");
            bool down = contains(clause, "降低") || contains(clause, "减少");
            if (up || down) {
                Effect e; e.kind = Effect::E_STAT_MOD;
                e.stat = i;
                double v = pcts.empty() ? (double)firstNumber(clause, 0) : pcts.back();
                if (v == 0) v = firstNumber(clause, 0);
                e.flat = !contains(clause, "%");
                e.rate = down ? -v : v;
                e.duration = dur > 0 ? dur : 8;
                e.onEnemy = onEnemy;
                e.selfOnly = selfOnly;
                e.intScaling = intS;
                out.push_back(e); note = "属性"; return;
            }
        }
    }

    // 6) 发动率提升
    if ((contains(clause, "突击") || contains(clause, "主动")) && contains(clause, "发动率")) {
        Effect e; e.kind = Effect::E_TRIGGER_BOOST;
        e.boostType = contains(clause, "突击") ? 1 : 0;
        e.rate = pcts.empty() ? 5 : pcts.back();
        e.duration = dur > 0 ? dur : 8;
        e.onEnemy = false;
        out.push_back(e); note = "发动率"; return;
    }

    // 7) 先攻 / 连击
    if (contains(clause, "先攻") && !contains(clause, "遇袭")) {
        Effect e; e.kind = Effect::E_FIRST_STRIKE;
        e.duration = dur > 0 ? dur : 8;
        e.onEnemy = false;
        e.selfOnly = selfOnly;
        out.push_back(e); note = "先攻"; return;
    }
    if (contains(clause, "连击")) {
        Effect e; e.kind = Effect::E_LINK_ATTACK;
        e.duration = dur > 0 ? dur : 8;
        e.onEnemy = false;
        out.push_back(e); note = "连击"; return;
    }

    // 8) 净化
    if (contains(clause, "净化") || contains(clause, "清除负面") || contains(clause, "解除") ||
        contains(clause, "清除")) {
        Effect e; e.kind = Effect::E_CLEANSE;
        e.count = count;
        e.onEnemy = false;
        e.selfOnly = selfOnly || contains(clause, "自身") || contains(clause, "自己");
        out.push_back(e); note = "净化"; return;
    }

    // 9) 抵御/警戒/分担 → 免伤
    if (contains(clause, "抵御") || contains(clause, "警戒") || contains(clause, "分担")) {
        Effect e; e.kind = Effect::E_GUARD;
        e.rate = 100.0;
        e.count = count;
        e.onEnemy = false;
        e.selfOnly = selfOnly;
        out.push_back(e); note = "抵御"; return;
    }

    // 10) 反击
    if (contains(clause, "反击")) {
        Effect e; e.kind = Effect::E_COUNTER;
        e.rate = 50.0;
        e.onEnemy = false;
        out.push_back(e); note = "反击"; return;
    }

    note = "未识别";
}

static bool matchAt(const std::string& s, size_t i, const char* seq) {
    size_t n = 0;
    while (seq[n]) {
        if (i + n >= s.size() || (unsigned char)s[i + n] != (unsigned char)seq[n]) return false;
        n++;
    }
    return true;
}

std::vector<std::string> splitClauses(const std::string& desc) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0; // 括号深度
    size_t i = 0;
    while (i < desc.size()) {
        if (matchAt(desc, i, "（") || matchAt(desc, i, "(")) depth++;
        if (matchAt(desc, i, "）") || matchAt(desc, i, ")")) {
            depth--;
            if (depth < 0) depth = 0;
        }
        if (depth == 0 && (matchAt(desc, i, "；") || matchAt(desc, i, ";") ||
                           matchAt(desc, i, "。") || matchAt(desc, i, "，") || desc[i] == '\n')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += desc[i];
        }
        i++;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// 从句中提取计划时机；命中则填充 sched 并返回 true
bool extractSchedule(const std::string& clause, Scheduled& sched) {
    if (contains(clause, "前") && contains(clause, "回合")) {
        size_t p = clause.find("前");
        size_t q = clause.find("回合", p);
        if (q != std::string::npos) {
            std::string num;
            for (size_t k = p + 1; k < q; k++)
                if (isdigit((unsigned char)clause[k])) num += clause[k];
            if (!num.empty()) {
                sched.firstNRounds = std::atoi(num.c_str());
                return true;
            }
        }
    }
    if (contains(clause, "第") && contains(clause, "回合")) {
        size_t p = clause.find("第");
        size_t q = clause.find("回合", p);
        if (q != std::string::npos) {
            std::string seg = clause.substr(p + 1, q - p - 1);
            std::vector<int> rounds;
            std::string cur;
            for (char c : seg) {
                if (isdigit((unsigned char)c)) cur += c;
                else if (!cur.empty()) { rounds.push_back(std::atoi(cur.c_str())); cur.clear(); }
            }
            if (!cur.empty()) rounds.push_back(std::atoi(cur.c_str()));
            if (!rounds.empty()) {
                for (int r : rounds) sched.atRounds.push_back(r - 1);
                return true;
            }
        }
    }
    if (contains(clause, "每回合")) {
        sched.firstNRounds = 8;
        return true;
    }
    return false;
}

} // namespace

// ---------------- 精选精确定义 ----------------
namespace {

std::unordered_map<std::string, TacticEffects> curatedTable() {
    std::unordered_map<std::string, TacticEffects> m;
    auto status = [](St s, int count, double chance, int dur, bool intS = false) {
        Effect e; e.kind = Effect::E_STATUS; e.statuses = {s}; e.count = count;
        e.chance = chance; e.duration = dur; e.intScaling = intS; e.onEnemy = true;
        return e;
    };
    auto dmgP = [](double rate, int count, bool intS = false) {
        Effect e; e.kind = Effect::E_DMG_PHYS; e.rate = rate; e.count = count;
        e.intScaling = intS; e.onEnemy = true;
        return e;
    };
    auto dmgM = [](double rate, int count, bool intS = false) {
        Effect e; e.kind = Effect::E_DMG_MAGIC; e.rate = rate; e.count = count;
        e.intScaling = intS; e.onEnemy = true;
        return e;
    };
    auto heal = [](double rate, int count, bool intS = true) {
        Effect e; e.kind = Effect::E_HEAL; e.rate = rate; e.count = count;
        e.intScaling = intS; e.onEnemy = false;
        return e;
    };
    auto atkUp = [](double pct, int dur = 8) {
        Effect e; e.kind = Effect::E_ATK_UP; e.rate = pct; e.duration = dur; e.onEnemy = false;
        return e;
    };
    auto defUp = [](double pct, int dur = 8) {
        Effect e; e.kind = Effect::E_DEF_UP; e.rate = pct; e.duration = dur; e.onEnemy = false;
        return e;
    };
    auto statModEnemy = [](int stat, double rate, int dur) {
        Effect e; e.kind = Effect::E_STAT_MOD; e.stat = stat; e.rate = rate; e.flat = false;
        e.duration = dur; e.onEnemy = true;
        return e;
    };
    auto statModAlly = [](int stat, double rate, bool flat, int dur, bool intS = false) {
        Effect e; e.kind = Effect::E_STAT_MOD; e.stat = stat; e.rate = rate; e.flat = flat;
        e.duration = dur; e.intScaling = intS; e.onEnemy = false;
        return e;
    };
    auto trigger = [](int type, double pct, int dur) {
        Effect e; e.kind = Effect::E_TRIGGER_BOOST; e.boostType = type; e.rate = pct;
        e.duration = dur; e.onEnemy = false;
        return e;
    };
    TacticEffects t;

    // 盛气凌敌：前2回合，敌军群体2人每回合45%缴械
    t = TacticEffects();
    Scheduled s; s.firstNRounds = 2; s.effects = {status(St::JIE_XIE, 2, 0.45, 1)};
    t.scheduled.push_back(s); t.parseOk = true; t.note = "精确";
    m["盛气凌敌"] = t;

    // 横扫千军：全体兵刃50%，缴械/计穷目标20%震慑
    t = TacticEffects();
    Effect e1 = dmgP(50, 0);
    Effect e2; e2.kind = Effect::E_STATUS; e2.statuses = {St::ZHEN_SHE};
    e2.count = 0; e2.chance = 0.2; e2.duration = 1; e2.onEnemy = true;
    e2.requiresStatus = {St::JIE_XIE, St::JI_QIONG};
    t.cast = {e1, e2}; t.triggerRate = 0.45; t.parseOk = true; t.note = "精确";
    m["横扫千军"] = t;

    // 燕人咆哮：第2、4回合全体兵刃52%，缴械目标降统率25%
    t = TacticEffects();
    Effect dm = statModEnemy(2, -25, 2);
    dm.requiresStatus = {St::JIE_XIE};
    Scheduled s1; s1.atRounds = {1}; s1.effects = {dmgP(52, 0), dm};
    Scheduled s2; s2.atRounds = {3}; s2.effects = {dmgP(52, 0), dm};
    t.scheduled = {s1, s2}; t.parseOk = true; t.note = "精确";
    m["燕人咆哮"] = t;

    // 火烧连营：35%，单体灼烧41%(受智力)3回合×2；已有灼烧→焚营全体谋略31%
    t = TacticEffects();
    Effect b1 = status(St::BURN, 1, 1.0, 3, true); b1.rate = 41;
    Effect b2 = dmgM(31, 0, true); b2.requiresStatus = {St::BURN};
    t.cast = {b1, b1, b2}; t.triggerRate = 0.35; t.parseOk = true; t.note = "精确";
    m["火烧连营"] = t;

    // 风助火势：40%，单体谋略77%(受智力)，灼烧目标额外谋略99%
    t = TacticEffects();
    Effect c1 = dmgM(77, 1, true);
    Effect c2 = dmgM(99, 1, true); c2.requiresStatus = {St::BURN};
    t.cast = {c1, c2}; t.triggerRate = 0.40; t.parseOk = true; t.note = "精确";
    m["风助火势"] = t;

    // 威震华夏：35%准备1回合，全体猛攻146%，50%缴械/计穷，自身兵刃伤害+36%2回合
    t = TacticEffects();
    Effect w1 = dmgP(146, 0);
    Effect w2; w2.kind = Effect::E_STATUS; w2.statuses = {St::JIE_XIE, St::JI_QIONG};
    w2.count = 0; w2.chance = 0.5; w2.duration = 1; w2.onEnemy = true;
    Effect w3 = atkUp(36, 2); w3.selfOnly = true;
    t.cast = {w1, w2, w3}; t.triggerRate = 0.35; t.needsCharge = true; t.parseOk = true; t.note = "精确";
    m["威震华夏"] = t;

    // 一骑当千：突击45%，普攻后全体兵刃36%
    t = TacticEffects();
    t.cast = {dmgP(36, 0)}; t.triggerRate = 0.45; t.parseOk = true; t.note = "精确";
    m["一骑当千"] = t;

    // 刮骨疗毒：40%，清除负面并治疗兵力损失最多单体128%
    t = TacticEffects();
    Effect g1 = heal(128, 1, true);
    Effect g2; g2.kind = Effect::E_CLEANSE; g2.onEnemy = false;
    t.cast = {g1, g2}; t.triggerRate = 0.40; t.parseOk = true; t.note = "精确";
    m["刮骨疗毒"] = t;

    // 暂避其锋：前3回合，我方智力最高兵刃减伤20%(受智力)，武力最高谋略减伤20%
    t = TacticEffects();
    Scheduled z1; z1.firstNRounds = 3;
    Effect z1a = defUp(20, 1); z1a.selfOnly = true; z1a.intScaling = true;
    z1.effects = {z1a};
    t.scheduled.push_back(z1); t.parseOk = true; t.note = "精确(简化为全体)";
    m["暂避其锋"] = t;

    // 锋矢阵：主将增伤15%受伤害+20%；副将伤害-15%受伤害-12.5%
    t = TacticEffects();
    Effect f1 = atkUp(15, 8); f1.mainOnly = true;
    Effect f2; f2.kind = Effect::E_DEF_UP; f2.rate = -20; f2.duration = 8; f2.mainOnly = true; f2.onEnemy = false;
    Effect f3 = atkUp(-15, 8); f3.deputyOnly = true;
    Effect f4 = defUp(12.5, 8); f4.deputyOnly = true;
    t.permanent = {f1, f2, f3, f4}; t.parseOk = true; t.note = "精确";
    m["锋矢阵"] = t;

    // 八门金锁阵：前3回合敌军群体2人伤害降低15%，主将先攻
    t = TacticEffects();
    Scheduled bm1; bm1.firstNRounds = 3;
    Effect bm1a; bm1a.kind = Effect::E_VULN; bm1a.rate = -15; bm1a.duration = 1; bm1a.onEnemy = true; bm1a.intScaling = true;
    bm1.effects = {bm1a};
    Effect bm2; bm2.kind = Effect::E_FIRST_STRIKE; bm2.mainOnly = true; bm2.duration = 3; bm2.onEnemy = false;
    t.scheduled = {bm1}; t.permanent = {bm2}; t.parseOk = true; t.note = "精确";
    m["八门金锁阵"] = t;

    // 虎豹骑：全体武力+20，前3回合突击发动率+5%
    t = TacticEffects();
    Effect h1 = statModAlly(0, 20, true, 8);
    Effect h2 = trigger(1, 5, 3);
    t.permanent = {h1};
    Scheduled hs; hs.firstNRounds = 3; hs.effects = {h2};
    t.scheduled.push_back(hs); t.parseOk = true; t.note = "精确";
    m["虎豹骑"] = t;

    // 陷阵营：全体武力统率+11，前3回合受击30%概率治疗30%
    t = TacticEffects();
    Effect x1 = statModAlly(0, 11, true, 8);
    Effect x2 = statModAlly(2, 11, true, 8);
    Effect x3 = heal(30, 1, true); x3.chance = 0.3; x3.selfOnly = true;
    Scheduled xs; xs.firstNRounds = 3; xs.effects = {x3};
    t.permanent = {x1, x2}; t.scheduled.push_back(xs); t.parseOk = true; t.note = "精确";
    m["陷阵营"] = t;

    // 青囊：前4回合，我军群体2人统率+20(受智力)及急救50%治疗44%
    t = TacticEffects();
    Effect q1 = statModAlly(2, 20, true, 1, true);
    Effect q2 = heal(44, 2, true); q2.chance = 0.5;
    Scheduled qs; qs.firstNRounds = 4; qs.effects = {q1, q2};
    t.scheduled.push_back(qs); t.parseOk = true; t.note = "精确";
    m["青囊"] = t;

    // 以逸待劳：35%，治疗2人77%，受伤害降低
    t = TacticEffects();
    Effect y1 = heal(77, 2, true);
    Effect y2 = defUp(20, 1);
    t.cast = {y1, y2}; t.triggerRate = 0.35; t.parseOk = true; t.note = "精确";
    m["以逸待劳"] = t;

    // 士别三日：前3回合减伤（规避简化为减伤），第4回合智力+34并对全体谋略90%
    t = TacticEffects();
    Effect sby1 = defUp(25, 3);
    Effect sby2 = statModAlly(1, 34, true, 8);
    Effect sby3 = dmgM(90, 0, true);
    Scheduled sbys; sbys.atRounds = {3}; sbys.effects = {sby2, sby3};
    t.permanent = {sby1}; t.scheduled.push_back(sbys); t.parseOk = true; t.note = "精确(规避→减伤)";
    m["士别三日"] = t;

    // 奇计良谋：前3回合，敌武力最高兵刃减伤14%(受速度)，敌智力最高谋略减伤14%
    t = TacticEffects();
    Effect j1a; j1a.kind = Effect::E_VULN; j1a.rate = -14; j1a.duration = 1; j1a.onEnemy = true; j1a.spdScaling = true;
    Scheduled j1; j1.firstNRounds = 3; j1.effects = {j1a};
    t.scheduled.push_back(j1); t.parseOk = true; t.note = "精确(简化为全体)";
    m["奇计良谋"] = t;

    // 勇者得前：突击45%，普攻后获得1次抵御，并提升下一个主动战法伤害40%
    t = TacticEffects();
    Effect u1; u1.kind = Effect::E_GUARD; u1.rate = 100; u1.selfOnly = true; u1.onEnemy = false;
    Effect u2 = atkUp(40, 1); u2.selfOnly = true;
    t.cast = {u1, u2}; t.triggerRate = 0.45; t.parseOk = true; t.note = "精确";
    m["勇者得前"] = t;

    return m;
}

const std::unordered_map<std::string, TacticEffects>& curated() {
    static const std::unordered_map<std::string, TacticEffects> m = curatedTable();
    return m;
}

} // namespace

TacticEffects parseTacticEffects(const Tactic& t) {
    TacticEffects out;
    if (!t.isCombat()) return out;

    auto it = curated().find(t.name);
    if (it != curated().end()) return it->second;

    std::vector<std::string> clauses = splitClauses(t.description);
    if (!t.triggerRate.empty()) out.triggerRate = std::atof(t.triggerRate.c_str()) / 100.0;
    if (contains(t.description, "准备1回合") || contains(t.description, "准备 1 回合")) out.needsCharge = true;

    bool isActive = (t.type == "主动" || t.type == "突击");

    for (auto& cl : clauses) {
        Scheduled sched;
        bool hasSched = extractSchedule(cl, sched);
        std::string note;
        std::vector<Effect> effs;
        classifyClause(cl, effs, note);
        if (effs.empty()) continue;

        if (isActive) {
            for (auto& e : effs) out.cast.push_back(e);
        } else {
            if (hasSched) {
                for (auto& e : effs) sched.effects.push_back(e);
                out.scheduled.push_back(sched);
            } else {
                // 准备回合型的无时机效果：带概率/持续的状态视为全场每回合判定，其余为永久
                bool statusy = effs.front().kind == Effect::E_STATUS;
                bool dmgish = effs.front().kind == Effect::E_DMG_PHYS ||
                              effs.front().kind == Effect::E_DMG_MAGIC || effs.front().kind == Effect::E_TRUE_DMG;
                if (statusy) {
                    Scheduled s8; s8.firstNRounds = 8;
                    for (auto& e : effs) s8.effects.push_back(e);
                    out.scheduled.push_back(s8);
                } else if (dmgish) {
                    // 被动伤害（如反击、特殊触发）→ 每回合开始判定
                    Scheduled s8; s8.firstNRounds = 8;
                    for (auto& e : effs) s8.effects.push_back(e);
                    out.scheduled.push_back(s8);
                } else {
                    for (auto& e : effs) out.permanent.push_back(e);
                }
            }
        }
    }

    out.parseOk = !out.cast.empty() || !out.permanent.empty() || !out.scheduled.empty();
    if (!out.parseOk) out.note = "无效果";
    return out;
}

} // namespace sgz
