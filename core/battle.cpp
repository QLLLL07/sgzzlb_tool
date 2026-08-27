#include "battle.hpp"
#include <algorithm>
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

struct Dot { St st; double rate; bool intScaling; double casterInt; double casterTroops; int casterSide; int rounds; };

struct Unit {
    const Hero* hero = nullptr;
    int side = 0;     // 0=我方(评估方)，1=敌方(参考)
    int idx = 0;
    bool isMain = false;
    bool alive = true;
    double force = 0, intellect = 0, command = 0, speed = 0;
    int maxTroops = 10000, troops = 10000, wounded = 0;

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

    const Tactic* tactics[4] = {nullptr, nullptr, nullptr, nullptr}; // 自带 + 配装(≤2)
    int nTactics = 0;

    bool has(St s) const { return stDur[(int)s] > 0; }
    void setSt(St s, int dur) { stDur[(int)s] = std::max(stDur[(int)s], dur); }
    void clearSt(St s) { stDur[(int)s] = 0; }
};

struct SchedEntry {
    int firstNRounds = 0;
    std::vector<int> atRounds;
    std::vector<Effect> effects;
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

    Battle(const TeamConfig& a, const TeamConfig& b, unsigned seed) : A(a), B(b), rng(seed) {
        bool natA = a.sameKingdom();
        bool natB = b.sameKingdom();
        for (int i = 0; i < 3; i++) {
            initUnit(a, u[0][i], 0, i, natA);
            initUnit(b, u[1][i], 1, i, natB);
        }
        preparePhase();
    }

    double rnd() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }

    void initUnit(const TeamConfig& tc, Unit& un, int side, int i, bool nat) {
        un.hero = tc.hero[i];
        un.side = side; // 0=我方(A) 1=敌方(B)，显式传入避免地址比较
        un.idx = i;
        un.isMain = (i == tc.mainIdx);
        UnitStats s = computeUnitStats(*tc.hero[i], tc.troop, un.isMain, nat, 50);
        un.force = s.force; un.intellect = s.intellect; un.command = s.command; un.speed = s.speed;
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
                for (auto& pe : e.permanent) applyEffect(*un, *un, pe, true);
                for (auto& sc : e.scheduled) {
                    if (!sc.effects.empty()) {
                        // 属性/增益类视为持续，直接施放；状态/伤害/治疗按计划窗口
                        bool persistent = false;
                        for (auto& eff : sc.effects)
                            if (eff.kind == Effect::E_STAT_MOD || eff.kind == Effect::E_FIRST_STRIKE ||
                                eff.kind == Effect::E_LINK_ATTACK || eff.kind == Effect::E_TRIGGER_BOOST ||
                                eff.kind == Effect::E_ATK_UP || eff.kind == Effect::E_DEF_UP ||
                                eff.kind == Effect::E_VULN || eff.kind == Effect::E_GUARD)
                                persistent = true;
                        if (persistent) {
                            for (auto& eff : sc.effects) applyEffect(*un, *un, eff, true);
                        } else {
                            (un->side == 0 ? schedA : schedB).push_back({sc.firstNRounds, sc.atRounds, sc.effects});
                        }
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
        // 洗牌取前 want
        std::shuffle(cand.begin(), cand.end(), rng);
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

    // ---- 效果施放 ----
    void applyEffect(Unit& caster, Unit& target, const Effect& e, bool isPrepare) {
        switch (e.kind) {
            case Effect::E_ATK_UP:
                if (targetRoleOk(caster, e)) target.atkAmp += e.rate;
                break;
            case Effect::E_DEF_UP: {
                if (!targetRoleOk(caster, e)) break;
                double v = e.rate;
                if (e.intScaling) v *= intScale(caster);
                target.dmgReduction += v;
                if (target.dmgReduction > 90) target.dmgReduction = 90;
                break;
            }
            case Effect::E_VULN: {
                if (!targetRoleOk(caster, e)) break;
                double v = e.rate;
                if (e.intScaling) v *= intScale(caster);
                if (e.spdScaling) v *= spdScale(caster);
                target.vuln += v;
                if (target.vuln > 90) target.vuln = 90;
                break;
            }
            case Effect::E_STAT_MOD: {
                if (!targetRoleOk(caster, e)) break;
                double* st = statPtr(target, e.stat);
                if (st) {
                    if (e.flat) *st += e.rate;
                    else {
                        double v = e.rate;
                        if (e.intScaling) v *= intScale(caster);
                        *st *= (1.0 + v / 100.0);
                    }
                    if (*st < 0) *st = 0;
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
                    if (e.boostType == 1) target.assaultBoost += e.rate;
                    else target.activeBoost += e.rate;
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
            if (target.has(s)) {
                // 已有状态：刷新时长
                target.setSt(s, e.duration > 0 ? e.duration : 1);
                continue;
            }
            if (rnd() > e.chance) continue;
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
        bool phys = (e.kind == Effect::E_DMG_PHYS);
        bool tr = (e.kind == Effect::E_TRUE_DMG);
        double attrDiff = 0;
        if (!tr) {
            if (phys) attrDiff = atk.force - def.command;
            else attrDiff = atk.intellect - def.intellect;
            if (attrDiff < 0) attrDiff = 0;
        }
        double troopDmg = troopBaseDamage(atk.troops);
        double raw = (troopDmg + attrDiff * 1.4375) * 1.6; // 等级系数 1.6（L50）
        double rate = e.rate;
        if (e.intScaling) rate *= intScale(atk);
        if (e.spdScaling) rate *= spdScale(atk);
        double skillBase = raw * rate / 100.0;

        double guarantee = atk.troops < 5000 ? atk.troops / 50.0 : 100.0;
        double factor = 1.0 - def.dmgReduction / 100.0 + def.vuln / 100.0;
        if (factor < 0.10) factor = 0.10; // 减伤上限90%
        if (factor > 3.0) factor = 3.0;
        TroopType atkT = atk.side == 0 ? A.troop : B.troop;
        TroopType defT = atk.side == 0 ? B.troop : A.troop;
        double counter = counterFactor(atkT, defT);
        double fl = 0.86 + rnd() * 0.08; // 伤害浮动 86%~94%

        double amp = 1.0 + atk.atkAmp / 100.0;
        double dmg = guarantee * factor * counter * fl + skillBase * amp * factor * counter * fl;

        if (def.guard > 0) { dmg = 0; def.guard--; }
        applyDamage(def, dmg);
        return dmg;
    }

    void applyDamage(Unit& def, double dmg) {
        if (dmg <= 0) return;
        // 受伤全额扣兵力：其中 10% 永久死兵，90% 转伤兵（可治疗回补）
        // 保证 troops + wounded ≤ maxTroops（永久损失 = maxTroops - troops - wounded）
        double dead = dmg * 0.10;
        double wound = dmg * 0.90;
        def.troops -= (int)dmg;
        def.wounded += (int)wound;
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
                fakeCaster.intellect = it->casterInt;
                fakeCaster.troops = (int)it->casterTroops;
                fakeCaster.side = it->casterSide;
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
        if (un.has(St::ZHEN_SHE)) return; // 震慑跳过
        tickDots(un);

        // 蓄力战法释放
        if (un.charge > 0) {
            for (const Tactic* t : un.charged) {
                const TacticEffects& e = fx(t);
                for (auto& eff : e.cast) {
                    if (eff.selfOnly) { applyEffect(un, un, eff, false); continue; }
                    std::vector<Unit*> targets = eff.onEnemy ? pickTargets(un, eff, liveEnemies(un))
                                                             : pickTargets(un, eff, liveAllies(un));
                    for (Unit* tgt : targets) applyEffect(un, *tgt, eff, false);
                }
            }
            un.charge = 0;
            un.charged.clear();
        }

        // 主动战法判定
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
            for (auto& eff : e.cast) {
                if (eff.selfOnly) { applyEffect(un, un, eff, false); continue; }
                std::vector<Unit*> targets = eff.onEnemy ? pickTargets(un, eff, liveEnemies(un))
                                                         : pickTargets(un, eff, liveAllies(un));
                for (Unit* tgt : targets) applyEffect(un, *tgt, eff, false);
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
                for (auto& eff : e2.cast) {
                    if (eff.selfOnly) { applyEffect(un, un, eff, false); continue; }
                    std::vector<Unit*> targets = eff.onEnemy ? pickTargets(un, eff, liveEnemies(un))
                                                             : pickTargets(un, eff, liveAllies(un));
                    for (Unit* tgt : targets) applyEffect(un, *tgt, eff, false);
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
                // 每回合结束：10% 伤兵转化为死兵
                int dead = (int)(un.wounded * 0.10);
                un.wounded -= dead;
                un.troops -= dead;
                if (un.troops <= 0) {
                    un.troops = 0;
                    un.alive = false;
                    if (un.isMain) winner = (un.side == 1) ? 0 : 1;
                }
            }
        }
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
            // 施放该回合计划效果
            auto& list = (un->side == 0) ? bt.schedA : bt.schedB;
            for (auto& sc : list) {
                bool fire = false;
                if (sc.firstNRounds > 0 && r <= sc.firstNRounds) fire = true;
                for (int rr : sc.atRounds) if (rr == r - 1) fire = true;
                if (!fire) continue;
                for (auto& e : sc.effects) {
                    if (e.selfOnly) { bt.applyEffect(*un, *un, e, true); continue; }
                    std::vector<Unit*> targets = e.onEnemy ? bt.pickTargets(*un, e, bt.liveEnemies(*un))
                                                           : bt.pickTargets(*un, e, bt.liveAllies(*un));
                    for (Unit* t : targets) bt.applyEffect(*un, *t, e, true);
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
    if (bt.winner < 0) res.win = false; // 平局
    else res.win = (bt.winner == 0);
    res.rounds = bt.rounds;
    for (int i = 0; i < 3; i++) {
        res.dmgDealt += bt.u[0][i].maxTroops - bt.u[0][i].troops - bt.u[0][i].wounded;
        res.dmgTaken += bt.u[1][i].maxTroops - bt.u[1][i].troops - bt.u[1][i].wounded;
    }
    return res;
}

BattleStats simulateBattle(const TeamConfig& a, const TeamConfig& b, int sims, unsigned seed) {
    BattleStats s;
    s.sims = sims;
    double wins = 0;
    for (int i = 0; i < sims; i++) {
        BattleResult r = runBattle(a, b, seed + i * 100003);
        if (r.win) wins++;
        s.avgRounds += r.rounds;
        s.avgDmgDealt += r.dmgDealt;
        s.avgDmgTaken += r.dmgTaken;
    }
    s.winRate = wins / sims;
    s.avgRounds /= sims;
    s.avgDmgDealt /= sims;
    s.avgDmgTaken /= sims;
    return s;
}

bool buildReferenceTeam(TeamConfig& ref) {
    auto& st = store();
    int liu = st.heroIndexByName("刘备");
    int guan = st.heroIndexByName("关羽");
    int zhang = st.heroIndexByName("张飞");
    if (liu < 0 || guan < 0 || zhang < 0) return false;
    ref.hero[0] = &st.heroes[liu];
    ref.hero[1] = &st.heroes[guan];
    ref.hero[2] = &st.heroes[zhang];
    ref.mainIdx = 0; // 刘备主将
    ref.troop = T_SPEAR; // 桃园枪
    auto push = [&](int h, const char* name) {
        const Tactic* t = st.tacticByName(name);
        if (t) ref.slots[h].push_back(t);
    };
    push(0, "暂避其锋");
    push(0, "刮骨疗毒");
    push(1, "横扫千军");
    push(1, "盛气凌敌");
    push(2, "一骑当千");
    push(2, "破军威胜");
    return true;
}

} // namespace sgz
