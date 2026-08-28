// tactic_assign.hpp - 战法配装：为三武将挑选传承战法（按角色契合 + 联动）。
#pragma once
#include <unordered_set>
#include "data.hpp"
#include "scoring.hpp"

namespace sgz {

// 战法对某武将的契合评分（按角色定位 + 效果强度）
double tacticFitScore(const Hero& h, const HeroRole& role, const Tactic& t);

// 为队伍填充每武将 2 个传承战法（写入 tc.slots[i]）。
// 约束：仅传承+战斗战法；匹配队伍兵种；不用自带/自身传承；同一战法不跨武将重复。
void assignTactics(TeamConfig& tc, const std::unordered_set<std::string>* allowedTactics = nullptr);

// 预热战法效果缓存（多线程调用 assignTactics 前先单线程调用一次，避免并发写缓存）
void warmTacticCache();

} // namespace sgz
