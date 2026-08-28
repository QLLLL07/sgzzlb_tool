// api.cpp - extern "C" 导出接口，供 Python ctypes 调用。
// 约定：返回的字符串全部 malloc 分配，由 free_string() 释放。
//       hero id = store().heroes 数组下标（0-based），与 get_heroes() 返回顺序一致。
#include "data.hpp"
#include "api.hpp"
#include "battle.hpp"
#include "scoring.hpp"
#include "tactic_assign.hpp"
#include "recommend.hpp"
#include "account.hpp"
#include "json.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
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
    j.set("tacticLevel", (double)tc.tacticLevel);
    j.set("evidence", "满级战法 + 8回合蒙特卡洛实战胜率");
    return j;
}

jq::Json evaluateTeamStarsImpl(int id1, int id2, int id3, const int stars[3],
                               const int freeAttrs[3][4] = nullptr) {
    auto& st = store();
    const Hero* src[3] = {st.heroByIndex(id1), st.heroByIndex(id2), st.heroByIndex(id3)};
    jq::Json err = jq::Json::object(); err.set("ok", false);
    if (!src[0] || !src[1] || !src[2]) { err.set("error", "武将下标越界"); return err; }
    TeamConfig tc;
    for (int i = 0; i < 3; ++i) {
        tc.hero[i] = src[i];
        tc.redStars[i] = std::max(0, std::min(5, stars[i]));
        if (freeAttrs) for (int k = 0; k < 4; ++k)
            tc.freeAttributes[i][k] = std::max(0, freeAttrs[i][k]);
    }
    tc.mainIdx = 0; tc.troop = bestTroopType(tc.hero); tc.tacticLevel = 10; assignTactics(tc);
    RuleScore rs = ruleScore(tc); BattleStats bst; TeamConfig ref;
    if (buildReferenceTeam(ref)) bst = simulateBattle(tc, ref, 200, 12345); else bst.sims = 0;
    jq::Json out = evaluateTeamImpl(id1, id2, id3, (int)tc.troop);
    jq::Json battle = jq::Json::object();
    battle.set("sims", (double)bst.sims); battle.set("winRate", bst.winRate);
    battle.set("avgRounds", bst.avgRounds); battle.set("avgDmgDealt", bst.avgDmgDealt);
    battle.set("avgDmgTaken", bst.avgDmgTaken); out.set("battle", battle);
    out.set("total", finalScore(bst.winRate, rs));
    jq::Json arr = jq::Json::array(); for (int i = 0; i < 3; ++i) arr.push_back((double)std::max(0,std::min(5,stars[i])));
    out.set("redStars", arr); out.set("tacticLevel", 10.0);
    jq::Json multipliers = jq::Json::array();
    // 红度不修改基础属性/成长值；保留该字段表示属性倍率恒为 1.0，
    // 出伤和减伤倍率分别由下面两个字段返回。
    for (int i = 0; i < 3; ++i) multipliers.push_back(1.0);
    out.set("redStarAttributeMultiplier", multipliers);
    jq::Json attrs = jq::Json::array();
    for (int i = 0; i < 3; ++i) {
        jq::Json row = jq::Json::array();
        for (int k = 0; k < 4; ++k) row.push_back((double)tc.freeAttributes[i][k]);
        attrs.push_back(row);
    }
    out.set("freeAttributes", attrs);
    jq::Json redDamage = jq::Json::array();
    jq::Json redReduction = jq::Json::array();
    for (int i = 0; i < 3; ++i) {
        redDamage.push_back(3.0 * tc.redStars[i]);
        redReduction.push_back(3.0 * tc.redStars[i]);
    }
    out.set("redDamageBonusPercent", redDamage);
    out.set("redDamageReductionPercent", redReduction);
    out.set("battleWinRateWithRedStars", bst.winRate);
    out.set("evidence", "红度增伤/减伤 + 自由属性点 + 8回合蒙特卡洛");
    return out;
}

jq::Json evaluateTeamBuildJson(const char* text) {
    jq::Json error = jq::Json::object(); error.set("ok", false);
    try {
        jq::Json root = jq::Json::parse(text ? text : "");
        const jq::Json& hs = root.get("heroes");
        if (!hs.isArray() || hs.size() != 3) { error.set("error", "heroes 必须是长度为 3 的数组"); return error; }
        int ids[3] = {-1, -1, -1}, stars[3] = {0, 0, 0}, points[3][4] = {};
        static const char* keys[4] = {"force", "intellect", "command", "speed"};
        for (int i = 0; i < 3; ++i) {
            ids[i] = hs[(size_t)i].get("id").asInt(-1);
            stars[i] = hs[(size_t)i].get("stars").asInt(0);
            const jq::Json& p = hs[(size_t)i].get("freeAttributes");
            for (int k = 0; k < 4; ++k) points[i][k] = p.get(keys[k]).asInt(0);
            int sum = 0; for (int k = 0; k < 4; ++k) sum += std::max(0, points[i][k]);
            if (sum > 10) { error.set("error", "每名武将 freeAttributes 总和不能超过 10"); return error; }
        }
        return evaluateTeamStarsImpl(ids[0], ids[1], ids[2], stars, points);
    } catch (const std::exception& e) {
        error.set("error", std::string("构建 JSON 解析失败: ") + e.what()); return error;
    }
}

} // namespace
} // namespace sgz

// ---------------- 导出接口 ----------------

API_EXPORT const char* get_version() {
    sgz::ensureLoaded();
    return sgz::dupStr("sgzzlb 0.2.0");
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

// 为指定武将比较单个传承战法：固定两名队友、兵种和主将位，逐个实战模拟排序。
API_EXPORT const char* recommend_tactics(int hero_id, int teammate1_id, int teammate2_id,
                                          int top_n, int sims) {
    sgz::ensureLoaded();
    auto& st = sgz::store();
    const sgz::Hero* h0 = st.heroByIndex(hero_id);
    const sgz::Hero* h1 = st.heroByIndex(teammate1_id);
    const sgz::Hero* h2 = st.heroByIndex(teammate2_id);
    jq::Json error = jq::Json::object();
    if (!h0 || !h1 || !h2) { error.set("ok", false); error.set("error", "武将下标越界"); return sgz::dupJson(error); }
    if (top_n < 1) top_n = 10;
    if (sims < 20) sims = 200;

    sgz::TeamConfig ref;
    if (!sgz::buildReferenceTeam(ref)) { error.set("ok", false); error.set("error", "参考队伍不可用"); return sgz::dupJson(error); }
    sgz::TeamConfig base;
    base.hero[0] = h0; base.hero[1] = h1; base.hero[2] = h2;
    base.mainIdx = 0; base.troop = sgz::bestTroopType(base.hero); base.tacticLevel = 10;
    std::vector<jq::Json> rows;
    for (const sgz::Tactic& tactic : st.tactics) {
        if (!tactic.isInheritable() || !tactic.isCombat() || !tactic.fitsTroop(base.troop)) continue;
        sgz::TeamConfig tc = base;
        tc.slots[0].push_back(&tactic);
        sgz::warmTacticCache();
        sgz::BattleStats battle = sgz::simulateBattle(tc, ref, sims,
            static_cast<unsigned>(hero_id * 73856093u ^ teammate1_id * 19349663u ^
                                  teammate2_id * 83492791u ^ std::hash<std::string>{}(tactic.name)));
        sgz::HeroRole role = sgz::classifyHeroRole(*h0);
        double fit = sgz::tacticFitScore(*h0, role, tactic);
        jq::Json row = jq::Json::object();
        row.set("name", tactic.name); row.set("type", tactic.type);
        row.set("winRate", battle.winRate); row.set("sims", (double)battle.sims);
        row.set("avgDmgDealt", battle.avgDmgDealt); row.set("fitScore", fit);
        row.set("evidence", "固定队友 + 8回合蒙特卡洛实战");
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), [](const jq::Json& a, const jq::Json& b) {
        return a.get("winRate").asNumber() > b.get("winRate").asNumber();
    });
    if ((int)rows.size() > top_n) rows.resize((size_t)top_n);
    jq::Json out = jq::Json::object(); out.set("ok", true); out.set("heroId", (double)hero_id);
    jq::Json teammates = jq::Json::array();
    teammates.push_back((double)teammate1_id); teammates.push_back((double)teammate2_id);
    out.set("teammates", teammates);
    out.set("troop", std::string(sgz::troopNameCN(base.troop)));
    jq::Json result = jq::Json::array(); for (const auto& row : rows) result.push_back(row);
    out.set("recommendations", result); out.set("evidence", "按实际对战胜率降序，规则契合分仅作参考");
    return sgz::dupJson(out);
}

// 按用户红度评估；stars 取 0..5，超界值会截断。
API_EXPORT const char* evaluate_team_stars(int id1, int id2, int id3, int stars1, int stars2, int stars3) {
    sgz::ensureLoaded();
    const int stars[3] = {stars1, stars2, stars3};
    return sgz::dupJson(sgz::evaluateTeamStarsImpl(id1, id2, id3, stars));
}

// 按 JSON 构建队伍并分配每名武将最多 10 点自由属性。
// 格式：{"heroes":[{"id":1,"stars":5,"freeAttributes":{"force":10}}...]}。
API_EXPORT const char* evaluate_team_build(const char* build_json) {
    sgz::ensureLoaded();
    return sgz::dupJson(sgz::evaluateTeamBuildJson(build_json));
}

// 仅从本地账号已拥有的武将中推荐；红度会进入候选的实战模拟。
API_EXPORT const char* recommend_account_teams(const char* account_id, int top_n) {
    sgz::ensureLoaded();
    sgz::LocalAccount account;
    jq::Json err = jq::Json::object();
    if (!sgz::getAccount(account_id ? account_id : "", account)) {
        err.set("ok", false); err.set("error", "本地账号不存在"); return sgz::dupJson(err);
    }
    std::vector<int> owned;
    for (const auto& h : account.heroes) owned.push_back(h.first);
    std::sort(owned.begin(), owned.end());
    if (top_n < 1) top_n = 10;
    auto entries = sgz::recommendTeams(top_n, 200, 200, &owned, &account.heroes);
    jq::Json out = jq::Json::object(); out.set("ok", true); out.set("accountId", account.id);
    out.set("recommendations", sgz::recommendToJson(entries));
    out.set("evidence", "只枚举已拥有武将，红度增伤/减伤进入8回合蒙特卡洛");
    return sgz::dupJson(out);
}

API_EXPORT const char* get_tactic_max_level(const char* name) {
    sgz::ensureLoaded();
    jq::Json out = jq::Json::object();
    const sgz::Tactic* t = sgz::store().tacticByName(name ? name : "");
    if (!t) { out.set("ok", false); out.set("error", "战法不存在"); return sgz::dupJson(out); }
    out = sgz::tacticToJson(*t); out.set("ok", true); out.set("level", 10.0);
    out.set("sourceLevel", 1.0); out.set("numericMultiplier", 2.0);
    out.set("model", "数值型效果按 1级->10级线性倍率 1.0->2.0；概率与回合数保持原值");
    return sgz::dupJson(out);
}

API_EXPORT const char* get_tactics_max_level() {
    sgz::ensureLoaded();
    jq::Json arr = jq::Json::array();
    for (const sgz::Tactic& t : sgz::store().tactics) {
        jq::Json j = sgz::tacticToJson(t); j.set("level", 10.0); j.set("numericMultiplier", 2.0); arr.push_back(j);
    }
    return sgz::dupJson(arr);
}

API_EXPORT const char* create_local_account(const char* name) {
    sgz::ensureLoaded();
    std::string id = sgz::createAccount(name ? name : ""); sgz::LocalAccount a;
    sgz::getAccount(id, a); jq::Json out = sgz::accountToJson(a); out.set("ok", true);
    return sgz::dupJson(out);
}

API_EXPORT const char* set_local_account_hero(const char* account_id, int hero_id, int stars, int owned) {
    sgz::ensureLoaded(); jq::Json out = jq::Json::object();
    bool ok = sgz::setAccountHero(account_id ? account_id : "", hero_id, stars, owned != 0);
    out.set("ok", ok);
    if (!ok) out.set("error", "账号不存在或武将下标越界");
    else { sgz::LocalAccount a; sgz::getAccount(account_id, a); out = sgz::accountToJson(a); out.set("ok", true); }
    return sgz::dupJson(out);
}

API_EXPORT const char* get_local_account(const char* account_id) {
    sgz::ensureLoaded(); sgz::LocalAccount a; jq::Json out = jq::Json::object();
    if (!sgz::getAccount(account_id ? account_id : "", a)) { out.set("ok", false); out.set("error", "本地账号不存在"); }
    else { out = sgz::accountToJson(a); out.set("ok", true); }
    return sgz::dupJson(out);
}

API_EXPORT const char* list_local_accounts() {
    sgz::ensureLoaded(); return sgz::dupJson(sgz::accountsToJson());
}

API_EXPORT const char* save_local_accounts(const char* path) {
    std::string error; bool ok = sgz::saveAccounts(path, error); jq::Json out = jq::Json::object();
    out.set("ok", ok); if (!ok) out.set("error", error); else out.set("path", path ? path : ""); return sgz::dupJson(out);
}

API_EXPORT const char* load_local_accounts(const char* path) {
    sgz::ensureLoaded(); std::string error; bool ok = sgz::loadAccounts(path, error); jq::Json out = jq::Json::object();
    out.set("ok", ok); if (!ok) out.set("error", error); else out.set("accounts", sgz::accountsToJson()); return sgz::dupJson(out);
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
        o.set("level", 10.0);
        o.set("numericMultiplier", 2.0);
        a.push_back(o);
    }
    return sgz::dupJson(a);
}

API_EXPORT void free_string(const char* s) {
    if (s) std::free(const_cast<char*>(s));
}
