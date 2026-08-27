// battle.hpp - 8 回合战斗模拟引擎（按逻辑文档公式实现）。
#pragma once
#include "data.hpp"
#include "effects.hpp"
#include <random>
#include <vector>

namespace sgz {

// 队伍配置
struct TeamConfig {
    const Hero* hero[3] = {nullptr, nullptr, nullptr};
    int mainIdx = 0;                 // 0..2 主将位置
    TroopType troop = T_CAVALRY;
    std::vector<const Tactic*> slots[3]; // 每武将传承战法（不含自带）
    bool sameKingdom() const {
        return hero[0] && hero[1] && hero[2] &&
               hero[0]->kingdom == hero[1]->kingdom &&
               hero[1]->kingdom == hero[2]->kingdom;
    }
};

// 单场战斗结果
struct BattleResult {
    bool win = false;   // 我方（side A）获胜
    int rounds = 0;
    double dmgDealt = 0;
    double dmgTaken = 0;
};

// 蒙特卡洛汇总
struct BattleStats {
    double winRate = 0;
    double avgRounds = 0;
    double avgDmgDealt = 0;
    double avgDmgTaken = 0;
    int sims = 0;
};

// 单位 L50 属性计算（适性 + 主将 +10 + 国家队 10%）
struct UnitStats {
    double force = 0, intellect = 0, command = 0, speed = 0;
};
UnitStats computeUnitStats(const Hero& h, TroopType troop, bool isMain, bool national, int level = 50);

// 构建参考对手（默认桃园：刘备/关羽/张飞 + 经典战法）
bool buildReferenceTeam(TeamConfig& ref);

// 单场战斗
BattleResult runBattle(const TeamConfig& a, const TeamConfig& b, unsigned seed);

// 蒙特卡洛模拟
BattleStats simulateBattle(const TeamConfig& a, const TeamConfig& b, int sims, unsigned seed);

// 兵种克制系数：attacker 兵种 vs defender 兵种
double counterFactor(TroopType atk, TroopType def);

// 预热战法解析缓存（多线程 simulateBattle 前先单线程预热，避免并发写缓存）
void warmBattleCache();

} // namespace sgz
