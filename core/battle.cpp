#include "battle.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sgz {

// ---------------- 属性与克制 ----------------

UnitStats computeUnitStats(const Hero& h, TroopType troop, bool isMain, bool national, int level) {
    double am = aptitudeMult(h.aptitudeOf(troop));
    double lv = level > 1 ? (level - 1) : 0;
    UnitStats s;
    s.force = (h.fBase + h.fGrow * lv) * am;
    s.intellect = (h.iBase + h.iGrow * lv) * am;
    s.command = (h.cBase + h.cGrow * lv) * am;
    s.speed = (h.sBase + h.sGrow * lv) * am;
    if (isMain) { // 主将基础四维 +10
        s.force += 10; s.intellect += 10; s.command += 10; s.speed += 10;
    }
    if (national) { // 国家队协力 +10%
        s.force *= 1.10; s.intellect *= 1.10; s.command *= 1.10; s.speed *= 1.10;
    }
    return s;
}

double counterFactor(TroopType atk, TroopType def) {
    // 克制循环（逻辑文档）：枪→骑→盾→弓→枪（枪克骑、骑克盾、盾克弓、弓克枪，±15%）
    static bool beats[T_COUNT][T_COUNT] = {
        /*cavalry*/ {false, true,  false, false, false}, // 骑克盾
        /*shield*/  {false, false, true,  false, false}, // 盾克弓
        /*bow*/     {false, false, false, true,  false}, // 弓克枪
        /*spear*/   {true,  false, false, false, false}, // 枪克骑
        /*siege*/   {false, false, false, false, false},
    };
    if (atk == T_SIEGE || def == T_SIEGE) return 1.0;
    if (beats[atk][def]) return 1.15;
    if (beats[def][atk]) return 0.85;
    return 1.0;
}

// ---------------- 战斗单位 ----------------

namespace {

struct Dot { St st; double rate; bool intScaling; double casterInt; double casterTroops; int casterSide; int rounds; std::string source; };

struct Unit {
    const Hero* hero = nullptr;
    int side = 0;     // 0=我方(评估方)，1=敌方(参考)
    int idx = 0;
    bool isMain = false;
    int tacticLevel = 10;
    int redStars = 0;
    bool alive = true;
    double force = 0, intellect = 0, command = 0, speed = 0;
    int maxTroops = 10000, troops = 10000, wounded = 0, dead = 0;
    double damageReceived = 0;

    double atkAmp = 0;        // A类增伤(加法, %)
    double dmgReduction = 0;  // B类减伤(加法, %)，上限 90%
    double vuln = 0;          // B类易伤(加法, %)
    int stDur[16] = {0};      // 状态剩余回合
    int guard = 0;            // 免伤次数
    double counter = 0;       // 反击概率
    int charge = 0;           // >0：蓄力中
    std::vector<const Tactic*> charged; // 蓄力待发战法
    double activeBoost = 0;   // 主动发动率提升(百分点)
    double assaultBoost = 0;  // 突击发动率提升(百分点)
    std::vector<Dot> dots;
    const Tactic* activeTactic = nullptr;

    struct Modifier {
        enum Kind { ATK, DEF, VULN, ACTIVE, ASSAULT, STAT } kind;
        double delta = 0;
        int stat = -1;
        int remaining = 0; // 0 means permanent
    };
    std::vector<Modifier> modifiers;

    const Tactic* tactics[4] = {nullptr, nullptr, nullptr, nullptr}; // 自带 + 配装(≤2)
    int nTactics = 0;

    bool has(St s) const { return stDur[(int)s] > 0; }
    void setSt(St s, int dur) { stDur[(int)s] = std::max(stDur[(int)s], dur); }
    void clearSt(St s) { stDur[(int)s] = 0; }
};

struct SchedEntry {
    int ownerSide = 0;
    int ownerIndex = 0;
    const Tactic* sourceTactic = nullptr;
    int firstNRounds = 0;
    std::vector<int> atRounds;
    std::vector<Effect> effects;
    bool ratesAreMaxLevel = false;
};

// 战法效果缓存（按战法名）
const TacticEffects& fx(const Tactic* t) {
    static std::unordered_map<std::string, TacticEffects> cache;
    static const TacticEffects empty;
    if (!t) return empty;
    auto it = cache.find(t->name);
    if (it != cache.end()) return it->second;
    TacticEffects e = parseTacticEffects(*t);
    return cache.emplace(t->name, std::move(e)).first->second;
}

struct Battle {
    Unit u[2][3];
    TeamConfig A, B;
    std::mt19937 rng;
    int rounds = 0;
    int winner = -1; // -1 未定, 0=我方胜, 1=敌方胜
    std::unordered_map<std::string, BattleResult::TacticStat> tacticStats;

    Battle(const TeamConfig& a, const TeamConfig& b, unsigned seed) : A(a), B(b), rng(seed) {
        bool natA = a.sameKingdom();
        bool natB = b.sameKingdom();
        for (int i = 0; i < 3; i++) {
            initUnit(a, u[0][i], 0, i, natA);
            initUnit(b, u[1][i], 1, i, natB);
            for (int k = 0; k < u[0][i].nTactics; ++k)
                if (u[0][i].tactics[k]) tacticStats.emplace(u[0][i].tactics[k]->name, BattleResult::TacticStat{});
        }
        preparePhase();
    }

    double rnd() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }

    void initUnit(const TeamConfig& tc, Unit& un, int side, int i, bool nat) {
        un.hero = tc.hero[i];
        un.side = side; // 0=我方(A) 1=敌方(B)，显式传入避免地址比较
        un.idx = i;
        un.isMain = (i == tc.mainIdx);
        un.tacticLevel = std::max(1, std::min(10, tc.tacticLevel));
        UnitStats s = computeUnitStats(*tc.hero[i], tc.troop, un.isMain, nat, 50);
        un.force = s.force; un.intellect = s.intellect; un.command = s.command; un.speed = s.speed;
        int stars = std::max(0, std::min(5, tc.redStars[i]));
        un.redStars = stars;
        // 十点自由属性点由调用方分配；负数和超额点数在这里归零/截断。
        double* attrs[4] = {&un.force, &un.intellect, &un.command, &un.speed};
        int used = 0;
        for (int k = 0; k < 4; ++k) {
            int points = std::max(0, tc.freeAttributes[i][k]);
            int remaining = std::max(0, 10 - used);
            points = std::min(points, remaining);
            *attrs[k] += points;
            used += points;
        }
        un.troops = un.maxTroops = 10000;
        un.nTactics = 0;
        // 自带战法（从战法表按名查，保证指针类型一致）
        const Tactic* innate = store().tacticByName(tc.hero[i]->innate.name);
        if (innate) un.tactics[un.nTactics++] = innate;
        for (const Tactic* t : tc.slots[i]) {
            if (t && un.nTactics < 4) un.tactics[un.nTactics++] = t;
        }
    }

    // 准备回合：被动→阵法→兵种→指挥（按速度），施放永久/计划效果
    void preparePhase() {
        std::vector<Unit*> order;
        for (int side = 0; side < 2; side++)
            for (int i = 0; i < 3; i++) order.push_back(&u[side][i]);
        std::sort(order.begin(), order.end(), [](Unit* a, Unit* b) {
            return a->speed > b->speed;
        });
        for (Unit* un : order) {
            for (int k = 0; k < un->nTactics; k++) {
                const TacticEffects& e = fx(un->tactics[k]);
                if (!e.parseOk) continue;
                // 仅准备回合型
                if (un->tactics[k]->type == "主动" || un->tactics[k]->type == "突击") continue;
                for (auto& pe : e.permanent) applyEffect(*un, *un, pe, true, e.ratesAreMaxLevel, true);
                for (auto& sc : e.scheduled) {
                    if (!sc.effects.empty()) {
                        // 计划效果保留拥有者，回合开始仅由拥有者结算一次。
                        (un->side == 0 ? schedA : schedB).push_back(
                            {un->side, un->idx, un->tactics[k], sc.firstNRounds, sc.atRounds,
                             sc.effects, e.ratesAreMaxLevel});
                    }
                }
            }
        }
    }

    std::vector<SchedEntry> schedA, schedB;

    // ---- 目标选择 ----
    std::vector<Unit*> liveEnemies(Unit& atk) {
        std::vector<Unit*> v;
        for (int i = 0; i < 3; i++) {
            Unit& e = u[1 - atk.side][i];
            if (e.alive) v.push_back(&e);
        }
        return v;
    }
    std::vector<Unit*> liveAllies(Unit& atk) {
        std::vector<Unit*> v;
        for (int i = 0; i < 3; i++) {
            Unit& e = u[atk.side][i];
            if (e.alive && &e != &atk) v.push_back(&e);
        }
        return v;
    }

    // 选择 count 个目标；requiresStatus 非空则过滤
    std::vector<Unit*> pickTargets(Unit& atk, const Effect& e, const std::vector<Unit*>& pool) {
        std::vector<Unit*> cand = pool;
        std::vector<Unit*> out;
        int want = e.count; // 0=全体
        if (want <= 0 || want > (int)cand.size()) want = (int)cand.size();
        if (e.targetMode == Effect::MOST_WOUNDED) {
            std::sort(cand.begin(), cand.end(), [](Unit* a, Unit* b) {
                return a->wounded > b->wounded;
            });
        } else if (e.targetMode == Effect::HIGHEST_FORCE) {
            std::sort(cand.begin(), cand.end(), [](Unit* a, Unit* b) { return a->force > b->force; });
        } else if (e.targetMode == Effect::HIGHEST_INTELLECT) {
            std::sort(cand.begin(), cand.end(), [](Unit* a, Unit* b) { return a->intellect > b->intellect; });
        } else if (e.targetMode == Effect::MAIN) {
            std::stable_sort(cand.begin(), cand.end(), [](Unit* a, Unit* b) { return a->isMain > b->isMain; });
        } else {
            // 未指定目标规则的效果保持随机取目标。
            std::shuffle(cand.begin(), cand.end(), rng);
        }
        for (Unit* c : cand) {
            if ((int)out.size() >= want) break;
            if (!e.requiresStatus.empty()) {
                bool ok = false;
                for (St s : e.requiresStatus) if (c->has(s)) { ok = true; break; }
                if (!ok) continue;
            }
            out.push_back(c);
        }
        return out;
    }

    std::vector<Unit*> effectTargets(Unit& atk, const Effect& e) {
        if (e.onEnemy && atk.has(St::HUN_LUAN)) {
            std::vector<Unit*> all;
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i < 3; ++i)
                    if (u[side][i].alive) all.push_back(&u[side][i]);
            return pickTargets(atk, e, all);
        }
        if (!e.onEnemy && e.targetMode == Effect::MOST_WOUNDED) {
            std::vector<Unit*> allies;
            for (int i = 0; i < 3; ++i) if (u[atk.side][i].alive) allies.push_back(&u[atk.side][i]);
            return pickTargets(atk, e, allies);
        }
        if (!e.onEnemy) {
            std::vector<Unit*> allies;
            for (int i = 0; i < 3; ++i) if (u[atk.side][i].alive) allies.push_back(&u[atk.side][i]);
            return pickTargets(atk, e, allies);
        }
        return pickTargets(atk, e, liveEnemies(atk));
    }

    // ---- 效果施放 ----
    void addModifier(Unit& target, Unit::Modifier::Kind kind, double delta, int stat, int duration) {
        double applied = delta;
        if (kind == Unit::Modifier::ATK) target.atkAmp += applied;
        else if (kind == Unit::Modifier::DEF) {
            applied = std::min(delta, 90.0 - target.dmgReduction);
            target.dmgReduction += applied;
        }
        else if (kind == Unit::Modifier::VULN) {
            applied = std::min(delta, 90.0 - target.vuln);
            target.vuln += applied;
        }
        else if (kind == Unit::Modifier::ACTIVE) target.activeBoost += applied;
        else if (kind == Unit::Modifier::ASSAULT) target.assaultBoost += applied;
        target.modifiers.push_back({kind, applied, stat, duration});
    }

    void applyEffect(Unit& caster, Unit& target, Effect e, bool isPrepare,
                     bool ratesAreMaxLevel = false, bool forcePermanent = false) {
        // 数据文件记录的是 1 级数值。满级按 1.0 -> 2.0 线性外推；
        // 状态概率与持续回合不随等级放大，数值型效果随施法者战法等级放大。
        double levelScale = 1.0 + (caster.tacticLevel - 1) / 9.0;
        // 精选战法的效果表已采用 Lv10 实测数值；若以后开放低等级模拟，
        // 按 Lv1≈Lv10 的一半反推，其余战法仍从原始 1 级文本线性外推。
        if (ratesAreMaxLevel)
            levelScale = 0.5 + 0.5 * (caster.tacticLevel - 1) / 9.0;
        switch (e.kind) {
            case Effect::E_DMG_PHYS: case Effect::E_DMG_MAGIC: case Effect::E_TRUE_DMG:
            case Effect::E_HEAL: case Effect::E_ATK_UP: case Effect::E_DEF_UP:
            case Effect::E_VULN: case Effect::E_STAT_MOD: case Effect::E_TRIGGER_BOOST:
                e.rate *= levelScale;
                break;
            default: break;
        }
        switch (e.kind) {
            case Effect::E_ATK_UP:
                if (targetRoleOk(caster, e)) addModifier(target, Unit::Modifier::ATK, e.rate, -1,
                                                         forcePermanent ? 0 : (e.duration > 0 ? e.duration : 1));
                break;
            case Effect::E_DEF_UP: {
                if (!targetRoleOk(caster, e)) break;
                double v = e.rate;
                if (e.intScaling) v *= intScale(caster);
                addModifier(target, Unit::Modifier::DEF, v, -1, forcePermanent ? 0 : (e.duration > 0 ? e.duration : 1));
                break;
            }
            case Effect::E_VULN: {
                if (!targetRoleOk(caster, e)) break;
                double v = e.rate;
                if (e.intScaling) v *= intScale(caster);
                if (e.spdScaling) v *= spdScale(caster);
                addModifier(target, Unit::Modifier::VULN, v, -1, forcePermanent ? 0 : (e.duration > 0 ? e.duration : 1));
                break;
            }
            case Effect::E_STAT_MOD: {
                if (!targetRoleOk(caster, e)) break;
                double* st = statPtr(target, e.stat);
                if (st) {
                    double delta = e.rate;
                    if (e.intScaling) delta *= intScale(caster);
                    if (!e.flat) delta = *st * delta / 100.0;
                    *st += delta;
                    if (*st < 0) *st = 0;
                    if (!forcePermanent)
                        target.modifiers.push_back({Unit::Modifier::STAT, delta, e.stat,
                                                    e.duration > 0 ? e.duration : 1});
                }
                break;
            }
            case Effect::E_FIRST_STRIKE:
                if (targetRoleOk(caster, e)) target.setSt(St::FIRST_STRIKE, e.duration > 0 ? e.duration : 1);
                break;
            case Effect::E_LINK_ATTACK:
                if (targetRoleOk(caster, e)) target.setSt(St::LINK_ATTACK, e.duration > 0 ? e.duration : 1);
                break;
            case Effect::E_TRIGGER_BOOST:
                if (targetRoleOk(caster, e)) {
                    addModifier(target, e.boostType == 1 ? Unit::Modifier::ASSAULT : Unit::Modifier::ACTIVE,
                                e.rate, -1, forcePermanent ? 0 : (e.duration > 0 ? e.duration : 1));
                }
                break;
            case Effect::E_GUARD:
                if (targetRoleOk(caster, e)) target.guard += e.count > 0 ? e.count : 1;
                break;
            case Effect::E_COUNTER:
                if (targetRoleOk(caster, e)) target.counter = std::max(target.counter, e.rate / 100.0);
                break;
            case Effect::E_STATUS:
                applyStatusEffect(caster, target, e);
                break;
            case Effect::E_DMG_PHYS:
            case Effect::E_DMG_MAGIC:
            case Effect::E_TRUE_DMG:
                dealDamage(caster, target, e);
                break;
            case Effect::E_HEAL:
                healTarget(caster, target, e);
                break;
            case Effect::E_CLEANSE:
                if (e.selfOnly || &caster == &target) {
                    clearDebuffs(caster);
                } else if (target.side == caster.side) {
                    clearDebuffs(target);
                }
                break;
            default: break;
        }
    }

    bool targetRoleOk(const Unit& caster, const Effect& e) {
        if (e.mainOnly && !caster.isMain) return false;
        if (e.deputyOnly && caster.isMain) return false;
        return true;
    }

    void clearDebuffs(Unit& u) {
        for (int s = (int)St::JI_QIONG; s <= (int)St::JIN_LIAO; s++) {
            if (s != (int)St::FIRST_STRIKE && s != (int)St::LINK_ATTACK) {
                if (s != (int)St::BURN && s != (int)St::POISON && s != (int)St::WATER && s != (int)St::KUI_TAO) {
                    u.stDur[s] = 0;
                }
            }
        }
    }

    void applyStatusEffect(Unit& caster, Unit& target, const Effect& e) {
        for (St s : e.statuses) {
            if (rnd() > e.chance) continue;
            if (target.has(s)) {
                // 已有状态：刷新时长
                int duration = e.duration > 0 ? e.duration : 1;
                target.setSt(s, duration);
                if (isDotStatus(s)) {
                    bool found = false;
                    for (Dot& d : target.dots) if (d.st == s) {
                        d.rounds = duration;
                        d.rate = e.rate;
                        d.intScaling = e.intScaling;
                        d.casterInt = caster.intellect;
                        d.casterTroops = caster.troops;
                        d.source = caster.activeTactic ? caster.activeTactic->name : std::string();
                        d.casterSide = caster.side;
                        found = true;
                        break;
                    }
                    if (!found) target.dots.push_back({s, e.rate, e.intScaling, caster.intellect,
                                                       static_cast<double>(caster.troops), caster.side, duration,
                                                       caster.activeTactic ? caster.activeTactic->name : std::string()});
                }
                continue;
            }
            target.setSt(s, e.duration > 0 ? e.duration : 1);
            if (isDotStatus(s)) {
                Dot d;
                d.st = s;
                d.rate = e.rate;
                d.intScaling = e.intScaling;
                d.casterInt = caster.intellect;
                d.casterTroops = caster.troops;
                d.casterSide = caster.side;
                d.rounds = e.duration > 0 ? e.duration : 1;
                d.source = caster.activeTactic ? caster.activeTactic->name : std::string();
                target.dots.push_back(d);
            }
        }
    }

    double intScale(const Unit& caster) {
        double v = 1.0 + (caster.intellect - 100.0) * 0.001;
        if (v < 0.5) v = 0.5;
        if (v > 2.5) v = 2.5;
        return v;
    }
    double spdScale(const Unit& caster) {
        double v = 1.0 + (caster.speed - 100.0) * 0.001;
        if (v < 0.5) v = 0.5;
        if (v > 2.5) v = 2.5;
        return v;
    }

    double* statPtr(Unit& u, int stat) {
        switch (stat) {
            case 0: return &u.force;
            case 1: return &u.intellect;
            case 2: return &u.command;
            case 3: return &u.speed;
            default: return nullptr;
        }
    }

    // ---- 伤害与治疗 ----
    double troopBaseDamage(int troops) {
        if (troops <= 0) return 0;
        if (troops <= 2000) return troops / 10.0;
        if (troops <= 5000) return 200 + (troops - 2000) * 180.0 / 3000.0;
        if (troops <= 9600) return 380 + (troops - 5000) * 160.0 / 4600.0;
        return 540.0;
    }

    double dealDamage(Unit& atk, Unit& def, const Effect& e) {
        if (atk.has(St::XU_RUO)) return 0; // 虚弱：伤害为0
        // 遇袭使下一次伤害无效；必中可绕过该状态。
        if (def.has(St::YU_XI) && !atk.has(St::BI_ZHONG)) {
            def.clearSt(St::YU_XI);
            return 0;
        }
        bool phys = (e.kind == Effect::E_DMG_PHYS);
        bool tr = (e.kind == Effect::E_TRUE_DMG);
        double attrDiff = 0;
        if (!tr) {
            if (phys) attrDiff = atk.force - (atk.has(St::PO_JUN) ? 0.0 : def.command);
            else attrDiff = atk.intellect - (atk.has(St::PO_JUN) ? 0.0 : def.intellect);
            if (attrDiff < 0) attrDiff = 0;
        }
        double troopDmg = troopBaseDamage(atk.troops);
        double raw = (troopDmg + attrDiff * 1.4375) * 1.6; // 等级系数 1.6（L50）
        double rate = e.rate;
        if (e.intScaling) rate *= intScale(atk);
        if (e.spdScaling) rate *= spdScale(atk);
        double skillBase = raw * rate / 100.0;

        double guarantee = atk.troops < 5000 ? atk.troops / 50.0 : 100.0;
        double factor = 1.0 - (def.dmgReduction + def.redStars * 3.0) / 100.0 + def.vuln / 100.0;
        if (factor < 0.10) factor = 0.10; // 减伤上限90%
        if (factor > 3.0) factor = 3.0;
        TroopType atkT = atk.side == 0 ? A.troop : B.troop;
        TroopType defT = atk.side == 0 ? B.troop : A.troop;
        double counter = counterFactor(atkT, defT);
        double fl = 0.86 + rnd() * 0.08; // 伤害浮动 86%~94%

        double amp = 1.0 + (atk.atkAmp + atk.redStars * 3.0) / 100.0;
        double dmg = guarantee * factor * counter * fl + skillBase * amp * factor * counter * fl;

        if (def.guard > 0) { dmg = 0; def.guard--; }
        applyDamage(def, dmg);
        if (atk.side == 0 && atk.activeTactic && dmg > 0) {
            auto& stat = tacticStats[atk.activeTactic->name];
            stat.damage += dmg;
        }
        return dmg;
    }

    void applyDamage(Unit& def, double dmg) {
        if (dmg <= 0) return;
        dmg = std::min(dmg, static_cast<double>(std::max(0, def.troops)));
        if (dmg <= 0) return;
        // 受伤全额扣兵力：其中 10% 永久死兵，90% 转伤兵（可治疗回补）
        // 保证 troops + wounded ≤ maxTroops（永久损失 = maxTroops - troops - wounded）
        double wound = dmg * 0.90;
        def.troops -= (int)dmg;
        def.wounded += (int)wound;
        def.dead += (int)(dmg * 0.10);
        def.damageReceived += dmg;
        if (def.troops < 0) def.troops = 0;
        if (def.troops <= 0) {
            def.alive = false;
            if (def.isMain) winner = (def.side == 1) ? 0 : 1;
        }
    }

    void healTarget(Unit& caster, Unit& target, const Effect& e) {
        if (!target.alive) return;
        if (target.has(St::JIN_LIAO)) return; // 禁疗
        if (rnd() > e.chance) return;
        double rate = e.rate;
        if (e.intScaling) rate *= intScale(caster);
        double heal = rate / 100.0 * 420.0;
        double h = heal;
        if (h > target.wounded) h = target.wounded;
        if (h > target.maxTroops - target.troops) h = target.maxTroops - target.troops;
        target.wounded -= (int)h;
        target.troops += (int)h;
    }

    // ---- 行动 ----
    void tickDots(Unit& un) {
        for (auto it = un.dots.begin(); it != un.dots.end();) {
            if (it->rounds > 0) {
                Effect e;
                e.kind = (it->st == St::BURN || it->st == St::WATER) ? Effect::E_DMG_MAGIC : Effect::E_DMG_PHYS;
                e.rate = it->rate;
                e.intScaling = it->intScaling;
                // DoT 伤害率受施法者属性影响（近似，用存储的智力/兵力/阵营）
                Unit fakeCaster = un;
                // DoT 的施法者不是受击者；不要让受击者的虚弱/遇袭状态抑制或吞掉自身 DoT。
                fakeCaster.clearSt(St::XU_RUO);
                fakeCaster.clearSt(St::YU_XI);
                fakeCaster.intellect = it->casterInt;
                fakeCaster.troops = (int)it->casterTroops;
                fakeCaster.side = it->casterSide;
                fakeCaster.activeTactic = it->source.empty() ? nullptr : store().tacticByName(it->source);
                dealDamage(fakeCaster, un, e);
                it->rounds--;
                it++;
            } else {
                it = un.dots.erase(it);
            }
        }
    }

    void unitAct(Unit& un) {
        if (!un.alive) return;
        tickDots(un);
        if (un.has(St::ZHEN_SHE)) return; // 震慑只跳过行动，DoT 已在行动前结算

        // 蓄力战法释放
        if (un.charge > 0) {
            for (const Tactic* t : un.charged) {
                if (un.side == 0) tacticStats[t->name].activations++;
                un.activeTactic = t;
                const TacticEffects& e = fx(t);
                for (auto& eff : e.cast) {
                    if (eff.selfOnly) { applyEffect(un, un, eff, false, e.ratesAreMaxLevel); continue; }
                    std::vector<Unit*> targets = effectTargets(un, eff);
                    for (Unit* tgt : targets) applyEffect(un, *tgt, eff, false, e.ratesAreMaxLevel);
                }
            }
            un.charge = 0;
            un.charged.clear();
            un.activeTactic = nullptr;
        }

        // 主动战法判定
        if (!un.has(St::JI_QIONG)) {
            for (int k = 0; k < un.nTactics; k++) {
                const Tactic* t = un.tactics[k];
                if (t->type != "主动") continue;
                const TacticEffects& e = fx(t);
                if (!e.parseOk || e.cast.empty()) continue;
                double chance = e.triggerRate + un.activeBoost / 100.0;
                if (chance < 0) chance = 0;
                if (chance > 1) chance = 1;
                if (rnd() > chance) continue;
                if (e.needsCharge) {
                    if (un.charge == 0) { un.charge = 1; un.charged.push_back(t); }
                    continue;
                }
                if (un.side == 0) tacticStats[t->name].activations++;
                un.activeTactic = t;
                for (auto& eff : e.cast) {
                    if (eff.selfOnly) { applyEffect(un, un, eff, false, e.ratesAreMaxLevel); continue; }
                    std::vector<Unit*> targets = effectTargets(un, eff);
                    for (Unit* tgt : targets) applyEffect(un, *tgt, eff, false, e.ratesAreMaxLevel);
                }
                un.activeTactic = nullptr;
            }
        }

        // 普攻
        if (un.has(St::JIE_XIE)) return; // 缴械跳过普攻与突击
        basicAttack(un, false);
        if (un.has(St::LINK_ATTACK) && un.alive) basicAttack(un, true); // 连击
    }

    void basicAttack(Unit& un, bool link) {
        if (!un.alive) return;
        std::vector<Unit*> pool = liveEnemies(un);
        if (pool.empty()) return;
        Unit* target;
        if (un.has(St::HUN_LUAN)) {
            // 混乱：随机打我或我队友
            std::vector<Unit*> all;
            for (int s = 0; s < 2; s++) for (int i = 0; i < 3; i++) if (u[s][i].alive) all.push_back(&u[s][i]);
            target = all[rng() % all.size()];
        } else {
            // 30% 集火主将（爆头倾向）
            Unit* main = nullptr;
            for (Unit* p : pool) if (p->isMain) main = p;
            if (main && rnd() < 0.30) target = main;
            else target = pool[rng() % pool.size()];
        }

        Effect e;
        e.kind = Effect::E_DMG_PHYS;
        e.rate = 100.0;
        dealDamage(un, *target, e);

        // 反击
        if (target->alive && target->counter > 0 && rnd() < target->counter) {
            Effect ce;
            ce.kind = Effect::E_DMG_PHYS;
            ce.rate = 50.0;
            dealDamage(*target, un, ce);
        }

        // 突击战法
        if (target->alive) {
            for (int k = 0; k < un.nTactics; k++) {
                const Tactic* t = un.tactics[k];
                if (t->type != "突击") continue;
                const TacticEffects& e2 = fx(t);
                if (!e2.parseOk || e2.cast.empty()) continue;
                double chance = e2.triggerRate + un.assaultBoost / 100.0;
                if (chance < 0) chance = 0;
                if (chance > 1) chance = 1;
                if (rnd() > chance) continue;
                if (un.side == 0) tacticStats[t->name].activations++;
                for (auto& eff : e2.cast) {
                    un.activeTactic = t;
                    if (eff.selfOnly) { applyEffect(un, un, eff, false, e2.ratesAreMaxLevel); continue; }
                    std::vector<Unit*> targets = effectTargets(un, eff);
                    for (Unit* tgt : targets) applyEffect(un, *tgt, eff, false, e2.ratesAreMaxLevel);
                    un.activeTactic = nullptr;
                }
            }
        }
    }

    void endRound() {
        for (int s = 0; s < 2; s++) {
            for (Unit& un : u[s]) {
                if (!un.alive) continue;
                // 回合末状态衰减
                for (int i = 0; i < 16; i++) if (un.stDur[i] > 0) un.stDur[i]--;
                expireModifiers(un);
                // 每回合结束：10% 伤兵转化为死兵
                int converted = (int)(un.wounded * 0.10);
                un.wounded -= converted;
                un.dead += converted;
                if (un.troops <= 0) {
                    un.troops = 0;
                    un.alive = false;
                    if (un.isMain) winner = (un.side == 1) ? 0 : 1;
                }
            }
        }
    }

    void expireModifiers(Unit& un) {
        for (auto it = un.modifiers.begin(); it != un.modifiers.end();) {
            if (it->remaining <= 0) { ++it; continue; }
            if (--it->remaining > 0) { ++it; continue; }
            switch (it->kind) {
                case Unit::Modifier::ATK: un.atkAmp -= it->delta; break;
                case Unit::Modifier::DEF: un.dmgReduction -= it->delta; break;
                case Unit::Modifier::VULN: un.vuln -= it->delta; break;
                case Unit::Modifier::ACTIVE: un.activeBoost -= it->delta; break;
                case Unit::Modifier::ASSAULT: un.assaultBoost -= it->delta; break;
                case Unit::Modifier::STAT: {
                    double* p = statPtr(un, it->stat);
                    if (p) *p = std::max(0.0, *p - it->delta);
                    break;
                }
            }
            it = un.modifiers.erase(it);
        }
        un.dmgReduction = std::max(0.0, un.dmgReduction);
        un.vuln = std::max(0.0, un.vuln);
    }
};

} // namespace

// ---------------- 对外接口 ----------------

// 预热：把全部战法解析结果写入缓存（此后只读，多线程安全）
void warmBattleCache() {
    for (const Tactic& t : store().tactics) fx(&t);
}

BattleResult runBattle(const TeamConfig& a, const TeamConfig& b, unsigned seed) {
    Battle bt(a, b, seed);
    BattleResult res;
    for (int r = 1; r <= 8 && bt.winner < 0; r++) {
        bt.rounds = r;
        // 回合开始：计划效果（按速度顺序施放）
        std::vector<Unit*> order;
        for (int s = 0; s < 2; s++)
            for (int i = 0; i < 3; i++) order.push_back(&bt.u[s][i]);
        std::sort(order.begin(), order.end(), [](Unit* x, Unit* y) {
            bool fx = x->has(St::FIRST_STRIKE), fy = y->has(St::FIRST_STRIKE);
            if (fx != fy) return fx;
            return x->speed > y->speed;
        });
        for (Unit* un : order) {
            if (!un->alive) continue;
            // 每个计划条目只使用记录的拥有者，不能由同阵营其他单位重复施放。
            auto& list = (un->side == 0) ? bt.schedA : bt.schedB;
            for (auto& sc : list) {
                if (sc.ownerSide != un->side || sc.ownerIndex != un->idx) continue;
                bool fire = false;
                if (sc.firstNRounds > 0 && r <= sc.firstNRounds) fire = true;
                for (int rr : sc.atRounds) if (rr == r - 1) fire = true;
                if (!fire) continue;
                if (un->side == 0) bt.tacticStats[sc.sourceTactic ? sc.sourceTactic->name : std::string()].activations++;
                for (auto& e : sc.effects) {
                    un->activeTactic = sc.sourceTactic;
                    if (e.selfOnly) { bt.applyEffect(*un, *un, e, true, sc.ratesAreMaxLevel); un->activeTactic = nullptr; continue; }
                    std::vector<Unit*> targets = bt.effectTargets(*un, e);
                    for (Unit* t : targets) bt.applyEffect(*un, *t, e, true, sc.ratesAreMaxLevel);
                    un->activeTactic = nullptr;
                }
            }
        }
        // 行动顺序
        std::sort(order.begin(), order.end(), [](Unit* x, Unit* y) {
            bool fx = x->has(St::FIRST_STRIKE), fy = y->has(St::FIRST_STRIKE);
            if (fx != fy) return fx;
            return x->speed > y->speed;
        });
        for (Unit* un : order) {
            if (!un->alive) continue;
            bt.unitAct(*un);
            if (bt.winner >= 0) break;
        }
        if (bt.winner < 0) bt.endRound();
    }
    res.draw = bt.winner < 0;
    res.win = !res.draw && bt.winner == 0;
    res.rounds = bt.rounds;
    for (int i = 0; i < 3; i++) {
        res.dmgDealt += bt.u[1][i].damageReceived;
        res.dmgTaken += bt.u[0][i].damageReceived;
    }
    res.casualtyRatio = res.dmgDealt / std::max(1.0, res.dmgTaken);
    res.tacticStats = bt.tacticStats;
    return res;
}

BattleStats simulateBattle(const TeamConfig& a, const TeamConfig& b, int sims, unsigned seed) {
    BattleStats s;
    s.sims = sims;
    s.seed = seed;
    double wins = 0;
    double draws = 0;
    for (int i = 0; i < sims; i++) {
        BattleResult r = runBattle(a, b, seed + i * 100003);
        if (r.win) wins++;
        if (r.draw) draws++;
        s.avgRounds += r.rounds;
        s.avgDmgDealt += r.dmgDealt;
        s.avgDmgTaken += r.dmgTaken;
        s.avgCasualtyRatio += r.casualtyRatio;
        for (const auto& kv : r.tacticStats) {
            auto& dst = s.tacticStats[kv.first];
            dst.activations += kv.second.activations;
            dst.damage += kv.second.damage;
        }
    }
    if (sims <= 0) return s;
    s.winRate = wins / sims;
    s.drawRate = draws / sims;
    s.winRateStdError = std::sqrt(s.winRate * (1.0 - s.winRate) / sims);
    s.avgRounds /= sims;
    s.avgDmgDealt /= sims;
    s.avgDmgTaken /= sims;
    s.avgCasualtyRatio /= sims;
    // ratio=1 means even trade (50 points), ratio=2 means 66.7 points.
    s.casualtyScore = 100.0 * s.avgCasualtyRatio / (1.0 + s.avgCasualtyRatio);
    for (auto& kv : s.tacticStats) {
        kv.second.activations /= std::max(1, sims);
        kv.second.damage /= sims;
    }
    return s;
}

std::vector<ReferenceTeam> buildReferenceTeams() {
    auto& st = store();
    struct Spec {
        const char* name;
        const char* heroes[3];
        TroopType troop;
        int mainIdx;
        const char* tactics[3][2];
    } specs[] = {
        {"桃园枪", {"刘备", "关羽", "张飞"}, T_SPEAR, 0,
         {{"暂避其锋", "刮骨疗毒"}, {"横扫千军", "盛气凌敌"}, {"一骑当千", "破军威胜"}}},
        {"魏法骑", {"曹操", "贾诩", "程昱"}, T_CAVALRY, 0,
         {{"暂避其锋", "刮骨疗毒"}, {"夺魂挟魄", "杯蛇鬼车"}, {"太平道法", "士别三日"}}},
        {"蜀枪", {"诸葛亮", "赵云", "张飞"}, T_SPEAR, 0,
         {{"八门金锁阵", "婴城自守"}, {"夺魂挟魄", "杯蛇鬼车"}, {"横扫千军", "盛气凌敌"}}},
        {"吴骑", {"孙尚香", "太史慈", "凌统"}, T_CAVALRY, 0,
         {{"裸衣血战", "虎豹骑"}, {"折冲御侮", "当锋摧决"}, {"弯弓饮羽", "暴戾无仁"}}}
    };
    std::vector<ReferenceTeam> refs;
    for (const auto& spec : specs) {
        ReferenceTeam entry;
        entry.name = spec.name;
        bool complete = true;
        for (int i = 0; i < 3; ++i) {
            int idx = st.heroIndexByName(spec.heroes[i]);
            if (idx < 0) { complete = false; break; }
            entry.team.hero[i] = &st.heroes[idx];
        }
        if (!complete) continue;
        entry.team.mainIdx = spec.mainIdx;
        entry.team.troop = spec.troop;
        for (int i = 0; i < 3; ++i) for (int k = 0; k < 2; ++k) {
            const Tactic* t = st.tacticByName(spec.tactics[i][k]);
            if (t) entry.team.slots[i].push_back(t);
        }
        refs.push_back(std::move(entry));
    }
    return refs;
}

bool buildReferenceTeam(TeamConfig& ref) {
    auto refs = buildReferenceTeams();
    if (refs.empty()) return false;
    ref = refs.front().team;
    return true;
}

} // namespace sgz
