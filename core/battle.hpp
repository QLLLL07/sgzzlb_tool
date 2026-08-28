// battle.hpp - 8 回合战斗模拟引擎（按逻辑文档公式实现）。
#pragma once
#include "data.hpp"
#include "effects.hpp"
#include <random>
#include <unordered_map>
#include <vector>

namespace sgz {

// 队伍配置
struct TeamConfig {
    const Hero* hero[3] = {nullptr, nullptr, nullptr};
    int mainIdx = 0;                 // 0..2 主将位置
    TroopType troop = T_CAVALRY;
    int tacticLevel = 10;            // 统一按满级战法模拟，合法范围 1..10
    // 红度和自由属性点是队伍构建参数，不写回全局 Hero 数据。
    int redStars[3] = {0, 0, 0};
    int freeAttributes[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
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
    bool draw = false;
    int rounds = 0;
    double dmgDealt = 0;
    double dmgTaken = 0;
    // 有利方向：敌方承受伤害 / 我方承受伤害。值越高越好。
    double casualtyRatio = 0;
    struct TacticStat {
        double activations = 0;
        double damage = 0;
    };
    std::unordered_map<std::string, TacticStat> tacticStats;
};

// 蒙特卡洛汇总
struct BattleStats {
    double winRate = 0;
    double drawRate = 0;
    // Bernoulli standard error for win rate; draws count as non-wins.
    double winRateStdError = 0;
    double avgRounds = 0;
    double avgDmgDealt = 0;
    double avgDmgTaken = 0;
    double avgCasualtyRatio = 0;
    // 将战损比压缩到 0..100，便于跨队伍比较。
    double casualtyScore = 0;
    std::unordered_map<std::string, BattleResult::TacticStat> tacticStats;
    int sims = 0;
    // 用于复现该批次模拟的起始种子；每场使用 seed + i * 100003。
    unsigned seed = 0;
};

// 可选参考队及其标签。参考队只用于相对比较，不代表赛季环境全量对手。
struct ReferenceTeam {
    std::string name;
    TeamConfig team;
};

// 单位 L50 属性计算（适性 + 主将 +10 + 国家队 10%）
struct UnitStats {
    double force = 0, intellect = 0, command = 0, speed = 0;
};
UnitStats computeUnitStats(const Hero& h, TroopType troop, bool isMain, bool national, int level = 50);

// 构建参考对手（默认桃园：刘备/关羽/张飞 + 经典战法）
bool buildReferenceTeam(TeamConfig& ref);
// 构建当前数据集中可用的多套固定参考队；缺少任一武将的队伍会跳过。
std::vector<ReferenceTeam> buildReferenceTeams();

// 单场战斗
BattleResult runBattle(const TeamConfig& a, const TeamConfig& b, unsigned seed);

// 蒙特卡洛模拟
BattleStats simulateBattle(const TeamConfig& a, const TeamConfig& b, int sims, unsigned seed);

// 兵种克制系数：attacker 兵种 vs defender 兵种
double counterFactor(TroopType atk, TroopType def);

// 预热战法解析缓存（多线程 simulateBattle 前先单线程预热，避免并发写缓存）
void warmBattleCache();

} // namespace sgz
