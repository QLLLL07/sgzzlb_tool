// recommend.cpp - 两阶段推荐搜索实现。
#include "recommend.hpp"
#include "scoring.hpp"
#include "battle.hpp"
#include "tactic_assign.hpp"
#include <algorithm>
#include <atomic>
#include <thread>

namespace sgz {

namespace {

// 阶段一用的武将特征（避免每次组合重复分类）
struct HeroFeat {
    int cost = 0;
    const std::string* kingdom = nullptr;
    double apt[T_SIEGE] = {0, 0, 0, 0};
    bool dps = false, tank = false, heal = false;
};

struct Candidate {
    double rule = 0;
    int a = 0, b = 0, c = 0;
};

constexpr double W_BATTLE = 0.70; // 战斗胜率权重
constexpr double W_RULE = 0.30;   // 规则分权重

// 与 scoring::ruleScore 相同的公式，但用预计算特征，快筛用
double fastRule(const HeroFeat& f1, const HeroFeat& f2, const HeroFeat& f3) {
    double bestSum = -1;
    for (int t = 0; t < T_SIEGE; t++) {
        double s = f1.apt[t] + f2.apt[t] + f3.apt[t];
        if (s > bestSum) bestSum = s;
    }
    double avgRatio = bestSum / 3.0 / 1.2; // 1.0 = 全员 S
    if (avgRatio > 1) avgRatio = 1;
    double aptitude = avgRatio * 100;

    double kingdom = 0;
    if (*f1.kingdom == *f2.kingdom && *f2.kingdom == *f3.kingdom)
        kingdom = 100;
    else if (*f1.kingdom == *f2.kingdom || *f2.kingdom == *f3.kingdom ||
             *f1.kingdom == *f3.kingdom)
        kingdom = 50;

    bool dps = f1.dps || f2.dps || f3.dps;
    bool tank = f1.tank || f2.tank || f3.tank;
    bool heal = f1.heal || f2.heal || f3.heal;
    int covered = (dps ? 1 : 0) + (tank ? 1 : 0) + (heal ? 1 : 0);
    double role = (double)covered * 100.0 / 3.0;

    return 0.35 * aptitude + 0.25 * kingdom + 0.25 * role + 0.15 * 100.0;
}

} // namespace

std::vector<RecommendEntry> recommendTeams(int topN, int topM, int sims,
                                           const std::vector<int>* eligibleHeroIds,
                                           const std::unordered_map<int, int>* redStars) {
    auto& st = store();
    std::vector<RecommendEntry> out;
    if (st.heroes.size() < 3) return out;

    // 预热战法效果缓存 + 战斗解析缓存：此后多线程只读，无并发写
    warmTacticCache();
    warmBattleCache();

    // ---- 阶段一：全组合规则预筛 ----
    std::vector<int> ids;
    if (eligibleHeroIds) {
        for (int id : *eligibleHeroIds) if (st.heroByIndex(id)) ids.push_back(id);
    } else {
        ids.resize(st.heroes.size());
        for (int i = 0; i < (int)ids.size(); ++i) ids[i] = i;
    }
    int n = (int)ids.size();
    if (n < 3) return out;
    std::vector<HeroFeat> feat(n);
    for (int i = 0; i < n; i++) {
        const Hero& h = st.heroes[ids[i]];
        feat[i].cost = h.cost;
        feat[i].kingdom = &h.kingdom;
        for (int t = 0; t < T_SIEGE; t++)
            feat[i].apt[t] = aptitudeMult(h.aptitudeOf((TroopType)t));
        HeroRole r = classifyHeroRole(h);
        feat[i].dps = r.role.find("输出") != std::string::npos;
        feat[i].tank = (r.role == "坦克");
        feat[i].heal = (r.role == "治疗");
    }

    std::vector<Candidate> cands;
    cands.reserve(n * n * n / 6);
    for (int a = 0; a < n - 2; a++) {
        for (int b = a + 1; b < n - 1; b++) {
            for (int c = b + 1; c < n; c++) {
                int cost = feat[a].cost + feat[b].cost + feat[c].cost;
                if (cost > 20) continue; // 超统御无法上场，直接剔除
                cands.push_back({fastRule(feat[a], feat[b], feat[c]), ids[a], ids[b], ids[c]});
            }
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Candidate& x, const Candidate& y) { return x.rule > y.rule; });
    if ((int)cands.size() > topM) cands.resize(topM);
    int M = (int)cands.size();

    // ---- 阶段二：配装 + 蒙特卡洛（并行） ----
    TeamConfig ref;
    bool haveRef = buildReferenceTeam(ref);

    std::vector<RecommendEntry> entries(M);
    std::atomic<int> next{0};
    unsigned nthreads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> pool;
    pool.reserve(nthreads);
    for (unsigned w = 0; w < nthreads; w++) {
        pool.emplace_back([&]() {
            for (;;) {
                int k = next.fetch_add(1);
                if (k >= M) break;
                const Candidate& cd = cands[k];
                TeamConfig tc;
                int stars[3] = {0, 0, 0};
                const int rawIds[3] = {cd.a, cd.b, cd.c};
                for (int i = 0; i < 3; ++i) {
                    if (redStars) {
                        auto it = redStars->find(rawIds[i]);
                        if (it != redStars->end()) stars[i] = std::max(0, std::min(5, it->second));
                    }
                    tc.hero[i] = &st.heroes[rawIds[i]];
                    tc.redStars[i] = stars[i];
                }
                tc.mainIdx = 0; // 主将取第一将（简化）
                tc.troop = bestTroopType(tc.hero);
                assignTactics(tc);

                double wr = 0.5; // 无参考队伍时退化为规则排序
                if (haveRef) {
                    unsigned seed =
                        (unsigned)(cd.a * 73856093u ^ cd.b * 19349663u ^ cd.c * 83492791u);
                    BattleStats s = simulateBattle(tc, ref, sims, seed);
                    wr = s.winRate;
                }

                RecommendEntry e;
                e.heroIdx[0] = cd.a;
                e.heroIdx[1] = cd.b;
                e.heroIdx[2] = cd.c;
                e.cost = st.heroes[cd.a].cost + st.heroes[cd.b].cost + st.heroes[cd.c].cost;
                e.troop = tc.troop;
                e.rule = cd.rule;
                e.winRate = wr;
                e.total = W_BATTLE * wr * 100.0 + W_RULE * cd.rule;
                for (int i = 0; i < 3; ++i) e.redStars[i] = stars[i];
                e.tacticLevel = tc.tacticLevel;
                for (int i = 0; i < 3; i++)
                    for (const Tactic* t : tc.slots[i]) e.tactics[i].push_back(t->name);
                entries[k] = e;
            }
        });
    }
    for (auto& th : pool) th.join();

    std::sort(entries.begin(), entries.end(),
              [](const RecommendEntry& x, const RecommendEntry& y) { return x.total > y.total; });
    out.assign(entries.begin(), entries.begin() + std::min((int)entries.size(), topN));
    return out;
}

jq::Json recommendToJson(const std::vector<RecommendEntry>& entries) {
    jq::Json arr = jq::Json::array();
    for (const RecommendEntry& e : entries) {
        jq::Json j = jq::Json::object();
        jq::Json h = jq::Json::array();
        for (int i = 0; i < 3; i++) h.push_back((double)e.heroIdx[i]);
        j.set("heroes", h);
        j.set("cost", (double)e.cost);
        j.set("troop", std::string(troopNameCN(e.troop)));
        jq::Json ts = jq::Json::array();
        for (int i = 0; i < 3; i++) {
            jq::Json inner = jq::Json::array();
            for (const std::string& t : e.tactics[i]) inner.push_back(t);
            ts.push_back(inner);
        }
        j.set("tactics", ts);
        j.set("rule", e.rule);
        j.set("winRate", e.winRate);
        j.set("total", e.total);
        jq::Json rs = jq::Json::array();
        for (int i = 0; i < 3; ++i) rs.push_back((double)e.redStars[i]);
        j.set("redStars", rs);
        j.set("tacticLevel", (double)e.tacticLevel);
        j.set("evidence", "8回合蒙特卡洛实战胜率 + 规则分");
        arr.push_back(j);
    }
    return arr;
}

} // namespace sgz
