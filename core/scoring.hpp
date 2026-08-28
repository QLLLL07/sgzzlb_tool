// scoring.hpp - 规则评分、角色分类、加点建议、战法联动检测。
#pragma once
#include <string>
#include <vector>
#include "data.hpp"
#include "battle.hpp"

namespace sgz {

// 武将定位
struct HeroRole {
    std::string role;    // 兵刃输出/谋略输出/坦克/治疗/控制/辅助
    std::string advice;  // 加点建议
    std::string innateType; // 自带战法类型
    std::string innateDesc; // 自带战法描述（截断）
};

HeroRole classifyHeroRole(const Hero& h);

// 为三武将挑选最佳兵种（在骑/盾/弓/枪中取适性平均最高的）
TroopType bestTroopType(const Hero* const h[3]);

// 规则评分分解
struct RuleScore {
    double aptitude = 0;   // 兵种适性 0-100
    double kingdom = 0;    // 国家队加成 0-100
    double role = 0;       // 角色覆盖 0-100
    double cost = 0;       // 统御 0-100
    double total = 0;      // 加权总分 0-100
    bool costOverflow = false;
    int costSum = 0;
    // 多参考队实战结果（战损比越高分越高）
    double casualtyRatio = 0;
    double casualtyScore = 0;
};
RuleScore ruleScore(const TeamConfig& tc);

// 战法联动检测（返回可读描述）
std::vector<std::string> detectSynergies(const TeamConfig& tc);

// 队伍建议
std::vector<std::string> buildAdvice(const TeamConfig& tc, const BattleStats& st, const RuleScore& rs);

// 最终综合评分（0-100）：战斗胜率 70% + 规则分 30%
double finalScore(double winRate, const RuleScore& rs);
double finalScore(const BattleStats& battle, const RuleScore& rs);

} // namespace sgz
