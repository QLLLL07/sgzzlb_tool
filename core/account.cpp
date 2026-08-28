// account.cpp - local-only account storage. The caller controls save locations.
#include "account.hpp"
#include "data.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>

namespace sgz {
namespace {
std::vector<LocalAccount> g_accounts;
std::mutex g_accountsMutex;
unsigned long long g_nextAccountId = 1;

std::string nextId() { return "local-" + std::to_string(g_nextAccountId++); }

jq::Json accountJsonUnlocked(const LocalAccount& a) {
    jq::Json j = jq::Json::object();
    j.set("id", a.id);
    j.set("name", a.name);
    jq::Json heroes = jq::Json::array();
    std::vector<std::pair<int, int>> sortedHeroes(a.heroes.begin(), a.heroes.end());
    std::sort(sortedHeroes.begin(), sortedHeroes.end());
    for (const auto& entry : sortedHeroes) {
        jq::Json h = jq::Json::object();
        h.set("heroId", (double)entry.first);
        h.set("stars", (double)entry.second);
        const Hero* hero = store().heroByIndex(entry.first);
        if (hero) h.set("name", hero->name);
        heroes.push_back(h);
    }
    j.set("heroes", heroes);
    jq::Json tactics = jq::Json::array();
    std::vector<std::string> sortedTactics(a.tactics.begin(), a.tactics.end());
    std::sort(sortedTactics.begin(), sortedTactics.end());
    for (const std::string& name : sortedTactics) {
        jq::Json tactic = jq::Json::object();
        tactic.set("name", name);
        if (const Tactic* source = store().tacticByName(name)) {
            tactic.set("type", source->type);
            tactic.set("quality", source->quality);
            tactic.set("category", source->category);
        }
        tactics.push_back(tactic);
    }
    j.set("tactics", tactics);
    return j;
}
} // namespace

std::string createAccount(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    LocalAccount a;
    a.id = nextId();
    a.name = name.empty() ? "本地账号" : name;
    g_accounts.push_back(a);
    return a.id;
}

bool setAccountHero(const std::string& accountId, int heroId, int stars, bool owned) {
    if (heroId < 0 || !store().heroByIndex(heroId)) return false;
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    for (LocalAccount& a : g_accounts) {
        if (a.id != accountId) continue;
        if (!owned) a.heroes.erase(heroId);
        else a.heroes[heroId] = std::max(0, std::min(5, stars));
        return true;
    }
    return false;
}

bool setAccountTactic(const std::string& accountId, const std::string& tacticName, bool owned) {
    const Tactic* tactic = store().tacticByName(tacticName);
    // 账号战法池对应“可配装”的传承战法，避免将武将自带战法错误加入库存。
    if (!tactic || !tactic->isInheritable() || !tactic->isCombat()) return false;
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    for (LocalAccount& account : g_accounts) {
        if (account.id != accountId) continue;
        if (owned) account.tactics.insert(tactic->name);
        else account.tactics.erase(tactic->name);
        return true;
    }
    return false;
}

bool getAccount(const std::string& accountId, LocalAccount& out) {
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    for (const LocalAccount& a : g_accounts)
        if (a.id == accountId) { out = a; return true; }
    return false;
}

std::vector<LocalAccount> listAccounts() {
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    return g_accounts;
}

void clearAccounts() {
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    g_accounts.clear();
}

bool saveAccounts(const char* path, std::string& error) {
    if (!path || !path[0]) { error = "账号文件路径为空"; return false; }
    jq::Json root = jq::Json::object();
    root.set("version", 2.0);
    root.set("accounts", accountsToJson());
    std::string tmpPath = std::string(path) + ".tmp";
    std::ofstream f(tmpPath, std::ios::trunc);
    if (!f) { error = "无法写入账号文件"; return false; }
    f << root.dump();
    f.close();
    if (!f) { std::remove(tmpPath.c_str()); error = "写入账号文件失败"; return false; }
    if (std::rename(tmpPath.c_str(), path) != 0) {
        std::remove(tmpPath.c_str()); error = "无法替换账号文件"; return false;
    }
    return true;
}

bool loadAccounts(const char* path, std::string& error) {
    if (!path || !path[0]) { error = "账号文件路径为空"; return false; }
    std::ifstream f(path);
    if (!f) { error = "无法打开账号文件"; return false; }
    try {
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        jq::Json root = jq::Json::parse(content);
        const jq::Json& arr = root.get("accounts");
        if (!arr.isArray()) { error = "账号文件缺少 accounts 数组"; return false; }
        std::vector<LocalAccount> loaded;
        unsigned long long maxId = 0;
        for (size_t i = 0; i < arr.size(); ++i) {
            const jq::Json& j = arr[i];
            LocalAccount a;
            a.id = j.get("id").asString();
            a.name = j.get("name").asString();
            if (a.id.empty()) a.id = nextId();
            const std::string prefix = "local-";
            if (a.id.compare(0, prefix.size(), prefix) == 0) {
                try { maxId = std::max(maxId, std::stoull(a.id.substr(prefix.size()))); } catch (...) {}
            }
            if (a.name.empty()) a.name = "本地账号";
            const jq::Json& heroes = j.get("heroes");
            if (heroes.isArray()) for (size_t h = 0; h < heroes.size(); ++h) {
                int id = heroes[h].get("heroId").asInt(-1);
                if (store().heroByIndex(id))
                    a.heroes[id] = std::max(0, std::min(5, heroes[h].get("stars").asInt(0)));
            }
            const jq::Json& tactics = j.get("tactics");
            if (tactics.isArray()) for (size_t t = 0; t < tactics.size(); ++t) {
                const jq::Json& item = tactics[t];
                const std::string name = item.isString() ? item.asString() : item.get("name").asString();
                const Tactic* tactic = store().tacticByName(name);
                if (tactic && tactic->isInheritable() && tactic->isCombat()) a.tactics.insert(tactic->name);
            }
            loaded.push_back(std::move(a));
        }
        std::lock_guard<std::mutex> lock(g_accountsMutex);
        g_accounts = std::move(loaded);
        g_nextAccountId = std::max(g_nextAccountId, maxId + 1);
        return true;
    } catch (const std::exception& e) {
        error = std::string("账号 JSON 解析失败: ") + e.what();
        return false;
    }
}

jq::Json accountToJson(const LocalAccount& a) { return accountJsonUnlocked(a); }

jq::Json accountsToJson() {
    std::lock_guard<std::mutex> lock(g_accountsMutex);
    jq::Json arr = jq::Json::array();
    for (const LocalAccount& a : g_accounts) arr.push_back(accountJsonUnlocked(a));
    return arr;
}

} // namespace sgz
