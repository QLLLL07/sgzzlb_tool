// effects.hpp - 战法效果模型：把战法描述文本抽取为可被战斗引擎执行的效果。
#pragma once
#include <string>
#include <vector>
#include "data.hpp"

namespace sgz {

// 战斗状态
enum class St {
    NONE,
    JI_QIONG,    // 计穷：无法发动主动战法
    JIE_XIE,     // 缴械：无法普攻（连带突击）
    ZHEN_SHE,    // 震慑：跳过本回合行动
    HUN_LUAN,    // 混乱：攻击目标随机
    XU_RUO,      // 虚弱：造成伤害为 0
    JIN_LIAO,    // 禁疗：无法被治疗
    BURN,        // 灼烧：每回合谋略持续伤害
    POISON,      // 中毒：每回合兵刃持续伤害
    WATER,       // 水攻：每回合谋略持续伤害
    KUI_TAO,     // 溃逃：每回合兵刃持续伤害
    FIRST_STRIKE,// 先攻
    YU_XI,       // 遇袭
    LINK_ATTACK, // 连击（普攻两次）
    BI_ZHONG,    // 必中
    PO_JUN,      // 破阵（无视统率/智力防御）
};
inline const char* stName(St s);
St stFromText(const std::string& kw);
// 是否为持续伤害状态（灼烧/中毒/水攻/溃逃）
inline bool isDotStatus(St s) {
    return s == St::BURN || s == St::POISON || s == St::WATER || s == St::KUI_TAO;
}

// 单条效果
struct Effect {
    enum Kind {
        E_NONE,
        E_DMG_PHYS,     // 兵刃伤害 rate
        E_DMG_MAGIC,    // 谋略伤害 rate
        E_TRUE_DMG,     // 真实伤害 rate（无视防御）
        E_HEAL,         // 治疗 rate
        E_STATUS,       // 施加状态 statuses, count, chance, duration
        E_ATK_UP,       // 我方造成伤害增加 pct（A类增伤），duration
        E_DEF_UP,       // 我方受到伤害降低 pct（B类减伤），duration
        E_VULN,         // 目标受到伤害增加 pct（B类易伤），duration
        E_STAT_MOD,     // 属性增减 stat, rate(点数或百分比), duration
        E_FIRST_STRIKE, // 获得先攻 duration
        E_LINK_ATTACK,  // 获得连击 duration
        E_TRIGGER_BOOST,// 提高主动/突击发动率 boostType(0主动 1突击)，rate为提升的百分点
        E_CLEANSE,      // 净化自身/友军负面
        E_COUNTER,      // 反击：被普攻时 rate 概率对攻击者造成兵刃伤害
        E_GUARD,        // 抵御/警戒：免疫或降低下次伤害
    };
    Kind kind = E_NONE;
    double rate = 0.0;        // 伤害率/治疗率/百分比（%数值，如 50 表示 50%）
    int count = 1;            // 目标数（1/2/3），0 表示全体
    double chance = 1.0;      // 施加概率
    int duration = 0;         // 持续回合
    bool intScaling = false;  // 受智力影响
    bool spdScaling = false;  // 受速度影响
    int stat = 0;             // 0武力 1智力 2统率 3速度（E_STAT_MOD 用）
    bool flat = false;        // E_STAT_MOD: true=固定点数, false=百分比
    bool onEnemy = true;      // 目标阵营：true=敌方, false=我方
    bool selfOnly = false;    // 目标为自身
    bool mainOnly = false;    // 仅主将
    bool deputyOnly = false;  // 仅副将
    int boostType = 0;        // E_TRIGGER_BOOST: 0=主动 1=突击
    std::vector<St> statuses;
    std::vector<St> requiresStatus; // 目标需已处于这些状态之一才生效
};

// 计划性效果（准备回合型战法用）
struct Scheduled {
    int firstNRounds = 0;   // >0：前 N 回合内每回合执行
    std::vector<int> atRounds; // 第 N 回合执行（0-based，如第2、4回合 → {1,3}）
    std::vector<Effect> effects;
};

// 一个战法解析出的完整效果
struct TacticEffects {
    bool parseOk = false;
    // 主动/突击：施放效果
    std::vector<Effect> cast;
    bool needsCharge = false;      // 需要准备 1 回合
    double triggerRate = 0.0;      // 发动概率 0..1
    // 准备回合型（指挥/被动/阵法/兵种）：永久效果 + 计划效果
    std::vector<Effect> permanent;
    std::vector<Scheduled> scheduled;
    std::string note; // 解析说明（调试用）
};

// 解析单个战法（关键词抽取 + 精选精确定义覆盖）
TacticEffects parseTacticEffects(const Tactic& t);

// 是否为伤害类效果
inline bool isDamage(const Effect& e) {
    return e.kind == Effect::E_DMG_PHYS || e.kind == Effect::E_DMG_MAGIC || e.kind == Effect::E_TRUE_DMG;
}

} // namespace sgz
