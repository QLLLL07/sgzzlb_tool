// api.hpp - C++ GUI and external callers share this stable JSON C API.
#pragma once

extern "C" {

const char* get_version();
const char* load_data(const char* path);
const char* reload_data(const char* path);
const char* evaluate_team(int id1, int id2, int id3);
const char* evaluate_team_troop(int id1, int id2, int id3, int troop);
const char* recommend_teams(int top_n);
const char* recommend_tactics(int hero_id, int teammate1_id, int teammate2_id, int top_n, int sims);
const char* recommend_account_teams(const char* account_id, int top_n);
const char* evaluate_team_stars(int id1, int id2, int id3, int stars1, int stars2, int stars3);
const char* evaluate_team_build(const char* build_json);
const char* get_tactic_max_level(const char* name);
const char* get_tactics_max_level();
const char* create_local_account(const char* name);
const char* set_local_account_hero(const char* account_id, int hero_id, int stars, int owned);
const char* get_local_account(const char* account_id);
const char* list_local_accounts();
const char* save_local_accounts(const char* path);
const char* load_local_accounts(const char* path);
const char* get_heroes();
const char* get_tactics();
void free_string(const char* s);

}
