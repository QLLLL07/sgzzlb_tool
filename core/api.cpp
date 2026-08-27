// api.cpp - extern "C" 导出接口，供 Python ctypes 调用。
// 约定：返回的字符串全部 malloc 分配，由 free_string() 释放。
//       hero id = store().heroes 数组下标（0-based），与 get_heroes() 返回顺序一致。
#include "data.hpp"
#include "battle.hpp"
#include "scoring.hpp"
#include "tactic_assign.hpp"
#include "recommend.hpp"
#include "json.hpp"
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#define API_EXPORT extern "C" __declspec(dllexport)
#else
#define API_EXPORT extern "C"
#endif

namespace sgz {
namespace {

// 复制为 malloc 字符串（调用方用 free_string 释放）
char* dupStr(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p) return nullptr;
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}
char* dupJson(const jq::Json& j) { return dupStr(j.dump()); }

// 确保数据已加载（未加载则用内置回退）并预热战法缓存
void ensureLoaded() {
    if (store().empty()) loadBuiltinFallback();
    warmTacticCache();
    warmBattleCache();
}

int heroIdx(const char* name) { return store().heroIndexByName(name ? name : ""); }

// 从三个下标构造队伍并评估，输出完整 JSON 报告。
// troop: -1 自动选最佳兵种，否则 0=骑 1=盾 2=弓 3=枪
jq::Json evaluateTeamImpl(int id1, int id2, int id3, int troop = -1) {
    jq::Json err = jq::Json::object();
    err.set("ok", false);
    auto& st = store();
    const Hero* h0 = st.heroByIndex(id1);
    const Hero* h1 = st.heroByIndex(id2);
    const Hero* h2 = st.heroByIndex(id3);
    if (!h0 || !h1 || !h2) {
        err.set("error", "武将下标越界");
        return err;
    }
    TeamConfig tc;
    tc.hero[0] = h0;
    tc.hero[1] = h1;
    tc.hero[2] = h2;
    tc.mainIdx = 0;
    tc.troop = (troop >= 0 && troop < T_SIEGE) ? (TroopType)troop : bestTroopType(tc.hero);
    assignTactics(tc);

    RuleScore rs = ruleScore(tc);
    BattleStats bst;
    TeamConfig ref;
    if (buildReferenceTeam(ref))
        bst = simulateBattle(tc, ref, 200, 12345);
    else
        bst.sims = 0;
    double total = finalScore(bst.winRate, rs);
    auto syn = detectSynergies(tc);
    auto adv = buildAdvice(tc, bst, rs);

    jq::Json j = jq::Json::object();
    j.set("ok", true);
    jq::Json ids = jq::Json::array();
    jq::Json names = jq::Json::array();
    for (int i = 0; i < 3; i++) {
        ids.push_back((double)heroIdx(tc.hero[i]->name.c_str()));
        names.push_back(tc.hero[i]->name);
    }
    j.set("heroes", ids);
    j.set("names", names);
    j.set("troop", std::string(troopNameCN(tc.troop)));
    j.set("cost", (double)rs.costSum);
    j.set("costOverflow", rs.costOverflow);

    jq::Json rj = jq::Json::object();
    rj.set("aptitude", rs.aptitude);
    rj.set("kingdom", rs.kingdom);
    rj.set("role", rs.role);
    rj.set("cost", rs.cost);
    rj.set("total", rs.total);
    rj.set("costOverflow", rs.costOverflow);
    rj.set("costSum", (double)rs.costSum);
    j.set("ruleScore", rj);

    jq::Json roles = jq::Json::array();
    jq::Json tactics = jq::Json::array();
    for (int i = 0; i < 3; i++) {
        HeroRole r = classifyHeroRole(*tc.hero[i]);
        jq::Json ro = jq::Json::object();
        ro.set("name", tc.hero[i]->name);
        ro.set("role", r.role);
        ro.set("advice", r.advice);
        ro.set("innate", r.innateDesc);
        roles.push_back(ro);
        jq::Json ts = jq::Json::array();
        for (const Tactic* t : tc.slots[i]) ts.push_back(t->name);
        tactics.push_back(ts);
    }
    j.set("roles", roles);
    j.set("tactics", tactics);

    jq::Json bj = jq::Json::object();
    bj.set("sims", (double)bst.sims);
    bj.set("winRate", bst.winRate);
    bj.set("avgRounds", bst.avgRounds);
    bj.set("avgDmgDealt", bst.avgDmgDealt);
    bj.set("avgDmgTaken", bst.avgDmgTaken);
    j.set("battle", bj);

    jq::Json sj = jq::Json::array();
    for (auto& s : syn) sj.push_back(s);
    j.set("synergies", sj);
    jq::Json aj = jq::Json::array();
    for (auto& a : adv) aj.push_back(a);
    j.set("advice", aj);
    j.set("total", total);
    return j;
}

} // namespace
} // namespace sgz

// ---------------- 导出接口 ----------------

API_EXPORT const char* get_version() {
    sgz::ensureLoaded();
    return sgz::dupStr("sgzzlb 0.1.0");
}

// 加载数据文件；path 为 NULL/空时尝试默认路径，失败回退内置数据。
// 返回 JSON：{"ok":bool,"source":..,"heroes":N,"tactics":M,"error":..}
API_EXPORT const char* load_data(const char* path) {
    sgz::ensureLoaded();
    sgz::store().clear();
    bool loaded = false;
    std::string source;
    if (path && path[0]) {
        loaded = sgz::loadDataFile(path);
        source = path;
    } else {
        // 默认尝试 data/data.json（相对当前目录）
        loaded = sgz::loadDataFile("data/data.json");
        source = "data/data.json";
    }
    jq::Json j = jq::Json::object();
    if (!loaded) {
        sgz::loadBuiltinFallback();
        j.set("ok", false);
        j.set("source", std::string("builtin-fallback"));
        j.set("error", sgz::store().loadError);
    } else {
        j.set("ok", true);
        j.set("source", source);
    }
    sgz::warmTacticCache();
    sgz::warmBattleCache();
    j.set("heroes", (double)sgz::store().heroes.size());
    j.set("tactics", (double)sgz::store().tactics.size());
    return sgz::dupJson(j);
}

API_EXPORT const char* reload_data(const char* path) { return load_data(path); }

// 评估队伍（兵种自动）：返回完整 JSON 报告
API_EXPORT const char* evaluate_team(int id1, int id2, int id3) {
    sgz::ensureLoaded();
    return sgz::dupJson(sgz::evaluateTeamImpl(id1, id2, id3));
}

// 评估队伍（指定兵种 troop：-1 自动，0=骑 1=盾 2=弓 3=枪）
API_EXPORT const char* evaluate_team_troop(int id1, int id2, int id3, int troop) {
    sgz::ensureLoaded();
    return sgz::dupJson(sgz::evaluateTeamImpl(id1, id2, id3, troop));
}

// 推荐 Top-N 队伍：返回 JSON 数组
API_EXPORT const char* recommend_teams(int top_n) {
    sgz::ensureLoaded();
    if (top_n < 1) top_n = 10;
    auto entries = sgz::recommendTeams(top_n);
    return sgz::dupJson(sgz::recommendToJson(entries));
}

// 英雄列表（GUI 用）：返回紧凑 JSON 数组
API_EXPORT const char* get_heroes() {
    sgz::ensureLoaded();
    auto& st = sgz::store();
    jq::Json a = jq::Json::array();
    for (size_t i = 0; i < st.heroes.size(); i++) {
        const sgz::Hero& h = st.heroes[i];
        jq::Json o = jq::Json::object();
        o.set("id", (double)i);
        o.set("name", h.name);
        o.set("kingdom", h.kingdom);
        o.set("cost", (double)h.cost);
        o.set("rating", (double)h.rating);
        std::string apt;
        for (int t = 0; t < sgz::T_COUNT; t++) apt += h.apt[t];
        o.set("aptitude", apt);
        o.set("innate", h.innate.name);
        sgz::HeroRole r = sgz::classifyHeroRole(h);
        o.set("role", r.role);
        a.push_back(o);
    }
    return sgz::dupJson(a);
}

// 战法列表（GUI 用）：返回紧凑 JSON 数组
API_EXPORT const char* get_tactics() {
    sgz::ensureLoaded();
    auto& st = sgz::store();
    jq::Json a = jq::Json::array();
    for (size_t i = 0; i < st.tactics.size(); i++) {
        const sgz::Tactic& t = st.tactics[i];
        jq::Json o = jq::Json::object();
        o.set("id", (double)i);
        o.set("name", t.name);
        o.set("type", t.type);
        o.set("category", t.category);
        o.set("quality", t.quality);
        o.set("triggerRate", t.triggerRate);
        a.push_back(o);
    }
    return sgz::dupJson(a);
}

API_EXPORT void free_string(const char* s) {
    if (s) std::free(const_cast<char*>(s));
}
