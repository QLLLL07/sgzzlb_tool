// api.hpp - C++ GUI and external callers share this stable JSON C API.
#pragma once

extern "C" {

const char* get_version();
const char* load_data(const char* path);
const char* reload_data(const char* path);
const char* evaluate_team(int id1, int id2, int id3);
const char* evaluate_team_troop(int id1, int id2, int id3, int troop);
const char* recommend_teams(int top_n);
const char* get_heroes();
const char* get_tactics();
void free_string(const char* s);

}
