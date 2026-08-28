// recommend.hpp - 两阶段推荐搜索（预筛 + 并行蒙特卡洛）。
#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "data.hpp"
#include "json.hpp"

namespace sgz {

// 单条推荐结果
struct RecommendEntry {
    int heroIdx[3] = {-1, -1, -1};  // 武将下标（0-based）
    int cost = 0;
    TroopType troop = T_CAVALRY;
    std::vector<std::string> tactics[3]; // 每武将配装战法名（不含自带）
    double rule = 0;     // 规则分 0-100
    double winRate = 0;  // 战斗胜率 0-1
    double drawRate = 0;
    double casualtyRatio = 0;
    double casualtyScore = 0;
    double winRateStdError = 0;
    double total = 0;    // 综合评分 0-100
    int redStars[3] = {0, 0, 0};
    int tacticLevel = 10;
    unsigned seed = 0;
};

// 阶段一：枚举 C(n,3)（统御≤20）规则分预筛取 topM；
// 阶段二：对候选配装 + 蒙特卡洛战斗（std::thread 并行）取 topN。
std::vector<RecommendEntry> recommendTeams(int topN, int topM = 200, int sims = 200,
    const std::vector<int>* eligibleHeroIds = nullptr,
    const std::unordered_map<int, int>* redStars = nullptr,
    const std::unordered_set<std::string>* allowedTactics = nullptr);

// 序列化为 JSON 数组（供 api 导出）
jq::Json recommendToJson(const std::vector<RecommendEntry>& entries);

} // namespace sgz
