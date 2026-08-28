// data.hpp - 武将 / 战法 / 数据仓库结构定义。
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "json.hpp"

namespace sgz {

// 兵种（与 heroes.json troopAptitude 键一致）
enum TroopType { T_CAVALRY = 0, T_SHIELD, T_BOW, T_SPEAR, T_SIEGE, T_COUNT };
inline const char* troopKey(TroopType t) {
    static const char* k[] = {"cavalry", "shield", "bow", "spear", "siege"};
    return k[(int)t];
}
inline const char* troopNameCN(TroopType t) {
    static const char* k[] = {"骑兵", "盾兵", "弓兵", "枪兵", "器械"};
    return k[(int)t];
}

struct Skill {
    std::string id, name, type, quality, triggerRate, description;
    bool empty() const { return name.empty(); }
};

struct Hero {
    std::string id, name, kingdom;
    int cost = 0, rating = 0;
    double fBase = 0, fGrow = 0; // 武力
    double iBase = 0, iGrow = 0; // 智力
    double cBase = 0, cGrow = 0; // 统率
    double sBase = 0, sGrow = 0; // 速度
    char apt[T_COUNT] = {'C','C','C','C','C'}; // S/A/B/C
    Skill innate;    // 自带战法
    Skill inherit;   // 传承战法（可能为空）
    bool hasInherit = false;

    char aptitudeOf(TroopType t) const { return apt[(int)t]; }
};

// 兼容旧内部调用。红度不修改基础/成长属性，实际红度效果在 TeamConfig 中处理。
Hero heroWithRedStars(const Hero& h, int redStars);

struct Tactic {
    std::string id, name, type, category, quality, triggerRate;
    std::vector<std::string> validTroops; // 兵种战法限定，如 ["cavalry"]
    std::string sourceHero, description;
    // 从可校验的 Lv10 资料挂载；description 始终保留原始 1 级数据文本。
    std::string maxLevelDescription, maxLevelReliability;
    bool hasMaxLevelData = false;
    bool maxLevelVersionConflict = false;
    bool fitsTroop(TroopType t) const {
        if (validTroops.empty()) return true;
        std::string k = troopKey(t);
        for (auto& v : validTroops) if (v == k) return true;
        return false;
    }
    bool isInheritable() const { return category == "传承"; }
    bool isCombat() const {
        return type == "主动" || type == "突击" || type == "指挥" || type == "被动" ||
               type == "阵法" || type == "兵种";
    }
};

// 兵种适性 → 属性发挥比例（逻辑文档：S 120% / A 100% / B 85% / C 70%）
inline double aptitudeMult(char c) {
    switch (c) {
        case 'S': return 1.20;
        case 'A': return 1.00;
        case 'B': return 0.85;
        case 'C': return 0.70;
        default:  return 0.70;
    }
}

struct DataStore {
    std::vector<Hero> heroes;
    std::vector<Tactic> tactics;
    std::unordered_map<std::string, int> heroNameIdx; // name -> index
    std::unordered_map<std::string, int> tacticNameIdx;
    std::string loadError; // 最近一次加载错误信息

    bool empty() const { return heroes.empty(); }
    void clear() { heroes.clear(); tactics.clear(); heroNameIdx.clear(); tacticNameIdx.clear(); }

    const Hero* heroByIndex(int idx) const {
        if (idx < 0 || idx >= (int)heroes.size()) return nullptr;
        return &heroes[idx];
    }
    int heroIndexByName(const std::string& name) const {
        auto it = heroNameIdx.find(name);
        return it == heroNameIdx.end() ? -1 : it->second;
    }
    int tacticIndexByName(const std::string& name) const {
        auto it = tacticNameIdx.find(name);
        return it == tacticNameIdx.end() ? -1 : it->second;
    }
    const Tactic* tacticByName(const std::string& name) const {
        int i = tacticIndexByName(name);
        return i < 0 ? nullptr : &tactics[i];
    }
};

// 全局数据仓库（库内单例）
DataStore& store();

// 解析单个武将 / 战法
Hero parseHero(const jq::Json& j);
Tactic parseTactic(const jq::Json& j);

// 加载合并数据文件 {"heroes":[...],"tactics":[...]}。成功返回 true。
bool loadDataFromJson(const jq::Json& root);
// 加载路径；失败时保留/回退内置数据。
bool loadDataFile(const char* path);

// 内置回退数据（至少 10 个示例武将），见 builtin_fallback.hpp
void loadBuiltinFallback();

// 由 store() 生成完整 heroes/tactics JSON（供 get_heroes/get_tactics 导出）
jq::Json heroesToJson();
jq::Json tacticsToJson();
jq::Json heroToJson(const Hero& h);
jq::Json tacticToJson(const Tactic& t);

} // namespace sgz
