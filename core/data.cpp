#include "data.hpp"
#include "builtin_fallback.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace sgz {

static DataStore g_store;
DataStore& store() { return g_store; }

Hero heroWithRedStars(const Hero& h, int redStars) {
    // 红度不再修改基础值/成长值。保留该函数作为 ABI 内部兼容入口；
    // 红度的出伤/减伤和自由点在 TeamConfig 中按战斗构建参数处理。
    (void)redStars;
    return h;
}

void loadBuiltinFallback() {
    try {
        jq::Json root = jq::Json::parse(kBuiltinDataJson);
        if (loadDataFromJson(root)) {
            g_store.loadError = "已加载内置示例数据（未找到外部数据文件）";
            return;
        }
    } catch (const std::exception&) {}
    g_store.loadError = "内置数据解析失败";
}

static Skill parseSkill(const jq::Json& j) {
    Skill s;
    if (!j.isObject()) return s;
    s.id = j.get("id").asString();
    s.name = j.get("name").asString();
    s.type = j.get("type").asString();
    s.quality = j.get("quality").asString();
    s.triggerRate = j.get("triggerRate").asString();
    s.description = j.get("description").asString();
    return s;
}

Hero parseHero(const jq::Json& j) {
    Hero h;
    h.id = j.get("id").asString();
    h.name = j.get("name").asString();
    h.kingdom = j.get("kingdom").asString();
    h.cost = j.get("cost").asInt(3);
    h.rating = j.get("rating").asInt(5);

    const jq::Json& at = j.get("attributes");
    h.fBase = at.get("force").get("base").asNumber(0);
    h.fGrow = at.get("force").get("growth").asNumber(0);
    h.iBase = at.get("intellect").get("base").asNumber(0);
    h.iGrow = at.get("intellect").get("growth").asNumber(0);
    h.cBase = at.get("command").get("base").asNumber(0);
    h.cGrow = at.get("command").get("growth").asNumber(0);
    h.sBase = at.get("speed").get("base").asNumber(0);
    h.sGrow = at.get("speed").get("growth").asNumber(0);

    const jq::Json& ta = j.get("troopAptitude");
    for (int t = 0; t < T_COUNT; t++) {
        std::string v = ta.get(troopKey((TroopType)t)).asString();
        h.apt[t] = v.empty() ? 'C' : v[0];
    }

    h.innate = parseSkill(j.get("innateSkill"));
    const jq::Json& in = j.get("inheritSkill");
    if (in.isObject() && !in.get("name").asString().empty()) {
        h.inherit = parseSkill(in);
        h.hasInherit = true;
    }
    return h;
}

Tactic parseTactic(const jq::Json& j) {
    Tactic t;
    t.id = j.get("id").asString();
    t.name = j.get("name").asString();
    t.type = j.get("type").asString();
    t.category = j.get("category").asString();
    t.quality = j.get("quality").asString();
    t.triggerRate = j.get("triggerRate").asString();
    t.sourceHero = j.get("sourceHero").asString();
    t.description = j.get("description").asString();
    const jq::Json& vt = j.get("validTroops");
    if (vt.isArray()) {
        for (size_t i = 0; i < vt.size(); i++)
            t.validTroops.push_back(vt[i].asString());
    }
    return t;
}

static std::string maxLevelLookupName(std::string name) {
    const std::string selfSuffix = "-自带";
    if (name.size() >= selfSuffix.size() &&
        name.compare(name.size() - selfSuffix.size(), selfSuffix.size(), selfSuffix) == 0)
        name.resize(name.size() - selfSuffix.size());
    return name;
}

bool loadDataFromJson(const jq::Json& root) {
    DataStore ds;
    const jq::Json& hs = root.get("heroes");
    if (!hs.isArray()) return false;
    for (size_t i = 0; i < hs.size(); i++) {
        Hero h = parseHero(hs[i]);
        if (h.name.empty()) continue;
        if (ds.heroNameIdx.count(h.name)) continue; // 去重
        ds.heroNameIdx[h.name] = (int)ds.heroes.size();
        ds.heroes.push_back(std::move(h));
    }
    const jq::Json& ts = root.get("tactics");
    if (ts.isArray()) {
        for (size_t i = 0; i < ts.size(); i++) {
            Tactic t = parseTactic(ts[i]);
            if (t.name.empty()) continue;
            if (ds.tacticNameIdx.count(t.name)) continue;
            ds.tacticNameIdx[t.name] = (int)ds.tactics.size();
            ds.tactics.push_back(std::move(t));
        }
    }
    // Lv10 资料独立于原始战法表维护。当前资料名使用兵种基础名，
    // 因此“虎豹骑-自带”等条目也会继承“虎豹骑”的同一份等级资料。
    const jq::Json& maxLevels = root.get("tacticMaxLevels");
    if (maxLevels.isArray()) {
        std::unordered_map<std::string, const jq::Json*> byName;
        for (size_t i = 0; i < maxLevels.size(); ++i) {
            const std::string name = maxLevels[i].get("name").asString();
            if (!name.empty() && !byName.count(name)) byName[name] = &maxLevels[i];
        }
        for (Tactic& t : ds.tactics) {
            auto it = byName.find(maxLevelLookupName(t.name));
            if (it == byName.end()) continue;
            const jq::Json& level = *it->second;
            t.maxLevelDescription = level.get("description").asString();
            t.maxLevelReliability = level.get("reliability").asString();
            t.maxLevelVersionConflict = level.get("versionConflict").asBool(false);
            t.hasMaxLevelData = !t.maxLevelDescription.empty();
        }
    }
    if (ds.heroes.empty()) return false;
    g_store = std::move(ds);
    g_store.loadError.clear();
    return true;
}

bool loadDataFile(const char* path) {
    std::ifstream f(path ? path : "");
    if (!f.is_open()) {
        g_store.loadError = std::string("无法打开文件: ") + (path ? path : "(null)");
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        jq::Json root = jq::Json::parse(ss.str());
        if (!loadDataFromJson(root)) {
            g_store.loadError = "数据格式不正确（缺少 heroes 数组）";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        g_store.loadError = std::string("JSON 解析失败: ") + e.what();
        return false;
    }
}

jq::Json heroToJson(const Hero& h) {
    jq::Json o = jq::Json::object();
    o.set("id", h.id);
    o.set("name", h.name);
    o.set("kingdom", h.kingdom);
    o.set("cost", (double)h.cost);
    o.set("rating", (double)h.rating);
    jq::Json at = jq::Json::object();
    at.set("force", (double)h.fBase);
    at.set("forceGrowth", h.fGrow);
    at.set("intellect", (double)h.iBase);
    at.set("intellectGrowth", h.iGrow);
    at.set("command", (double)h.cBase);
    at.set("commandGrowth", h.cGrow);
    at.set("speed", (double)h.sBase);
    at.set("speedGrowth", h.sGrow);
    o.set("attributes", at);
    jq::Json ap = jq::Json::object();
    for (int t = 0; t < T_COUNT; t++) ap.set(troopKey((TroopType)t), std::string(1, h.apt[t]));
    o.set("troopAptitude", ap);
    if (!h.innate.empty()) {
        jq::Json sk = jq::Json::object();
        sk.set("name", h.innate.name);
        sk.set("type", h.innate.type);
        sk.set("quality", h.innate.quality);
        sk.set("triggerRate", h.innate.triggerRate);
        sk.set("description", h.innate.description);
        o.set("innateSkill", sk);
    }
    if (h.hasInherit && !h.inherit.empty()) {
        jq::Json sk = jq::Json::object();
        sk.set("name", h.inherit.name);
        sk.set("type", h.inherit.type);
        sk.set("quality", h.inherit.quality);
        sk.set("triggerRate", h.inherit.triggerRate);
        sk.set("description", h.inherit.description);
        o.set("inheritSkill", sk);
    }
    return o;
}

jq::Json tacticToJson(const Tactic& t) {
    jq::Json o = jq::Json::object();
    o.set("id", t.id);
    o.set("name", t.name);
    o.set("type", t.type);
    o.set("category", t.category);
    o.set("quality", t.quality);
    o.set("triggerRate", t.triggerRate);
    o.set("sourceHero", t.sourceHero);
    o.set("description", t.description);
    jq::Json max = jq::Json::object();
    max.set("available", t.hasMaxLevelData);
    max.set("level", 10.0);
    max.set("description", t.maxLevelDescription);
    max.set("reliability", t.maxLevelReliability);
    max.set("versionConflict", t.maxLevelVersionConflict);
    o.set("maxLevel", max);
    jq::Json vt = jq::Json::array();
    for (auto& v : t.validTroops) vt.push_back(v);
    o.set("validTroops", vt);
    return o;
}

jq::Json heroesToJson() {
    jq::Json a = jq::Json::array();
    for (auto& h : store().heroes) a.push_back(heroToJson(h));
    return a;
}
jq::Json tacticsToJson() {
    jq::Json a = jq::Json::array();
    for (auto& t : store().tactics) a.push_back(tacticToJson(t));
    return a;
}

} // namespace sgz
