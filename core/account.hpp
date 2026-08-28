// account.hpp - lightweight local account ownership persistence.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "json.hpp"

namespace sgz {

struct LocalAccount {
    std::string id;
    std::string name;
    // hero index -> red-star level (0..5). Presence means owned.
    std::unordered_map<int, int> heroes;
};

std::string createAccount(const std::string& name);
bool setAccountHero(const std::string& accountId, int heroId, int stars, bool owned);
bool getAccount(const std::string& accountId, LocalAccount& out);
std::vector<LocalAccount> listAccounts();
void clearAccounts();
bool saveAccounts(const char* path, std::string& error);
bool loadAccounts(const char* path, std::string& error);
jq::Json accountToJson(const LocalAccount& a);
jq::Json accountsToJson();

} // namespace sgz
