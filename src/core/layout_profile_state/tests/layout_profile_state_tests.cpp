#include "layout_profile_state.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using Json = nlohmann::json;
int failures = 0;
void expect(bool value, std::string_view message)
{
  if(value) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

void test_validation()
{
  using namespace notepp::layout_profile_state;
  expect(validate_document(Json{}, false) == DocumentState::missing, "missing profile state");
  expect(validate_document(Json::parse("{", nullptr, false), true) == DocumentState::malformed, "malformed profile state");
  expect(validate_document(Json{{"profiles", Json::array()}}, true) == DocumentState::supported,
         "legacy profile state is supported");
  expect(validate_document(Json{{"schemaVersion", 2}, {"profiles", Json::array()}}, true) ==
             DocumentState::future_schema,
         "future profile state is read-only");
  expect(validate_document(Json{{"profiles", Json::array({Json{{"id", "p"},
                                                               {"note_layouts", "bad"}}})}},
                           true) == DocumentState::malformed,
         "malformed nested profile data blocks canonical writes");

  const Json valid_profile = {
      {"schemaVersion", 1},
      {"active_profile_id", "p"},
      {"profiles", Json::array({Json{{"id", "p"},
                                     {"name", "Profile"},
                                     {"window_maximized", false},
                                     {"window_x", 10},
                                     {"window_y", 20},
                                     {"window_w", 1100},
                                     {"window_h", 700},
                                     {"note_layouts", Json::array({Json{{"note_id", "n"},
                                                                        {"x", 0},
                                                                        {"y", 0},
                                                                        {"w", 520},
                                                                        {"h", 260},
                                                                        {"dock_id", 0},
                                                                        {"hidden", false},
                                                                        {"has_layout", true}}})}}})}};
  expect(validate_document(valid_profile, true) == DocumentState::supported,
         "all known layout profile fields validate");
  Json bad_active = valid_profile;
  bad_active["active_profile_id"] = 4;
  Json bad_name = valid_profile;
  bad_name["profiles"][0]["name"] = 4;
  Json bad_width = valid_profile;
  bad_width["profiles"][0]["window_w"] = 0;
  Json bad_hidden = valid_profile;
  bad_hidden["profiles"][0]["note_layouts"][0]["hidden"] = "false";
  for(const Json &malformed : std::vector<Json>{bad_active, bad_name, bad_width, bad_hidden})
    expect(validate_document(malformed, true) == DocumentState::malformed,
           "malformed known layout profile fields make the document read-only");
}

void test_unknown_fields_follow_stable_ids()
{
  const Json source = {{"pluginRoot", true},
                       {"profiles", Json::array({Json{{"id", "p1"}, {"pluginProfile", 7}, {"note_layouts", Json::array({Json{{"note_id", "n1"}, {"pluginLayout", 8}}})}}})}};
  const Json current = {{"schemaVersion", 1},
                        {"profiles", Json::array({Json{{"id", "p1"}, {"name", "Profile"}, {"note_layouts", Json::array({Json{{"note_id", "n1"}, {"x", 3}}})}}})}};
  const Json merged = notepp::layout_profile_state::merge_unknown_fields(source, current);
  expect(merged["pluginRoot"] == true, "unknown root field preserved");
  expect(merged["profiles"][0]["pluginProfile"] == 7, "unknown profile field preserved");
  expect(merged["profiles"][0]["note_layouts"][0]["pluginLayout"] == 8,
         "unknown layout field preserved");

  Json replacement = current;
  replacement["profiles"][0]["id"] = "p2";
  replacement["profiles"][0]["note_layouts"][0]["note_id"] = "n2";
  const Json isolated = notepp::layout_profile_state::merge_unknown_fields(source, replacement);
  expect(!isolated["profiles"][0].contains("pluginProfile"),
         "unknown fields do not move to a different profile identity");
  expect(!isolated["profiles"][0]["note_layouts"][0].contains("pluginLayout"),
         "unknown fields do not move to a different note identity");
}
} // namespace

int main()
{
  test_validation();
  test_unknown_fields_follow_stable_ids();
  if(failures != 0) return EXIT_FAILURE;
  std::cout << "layout_profile_state tests passed\n";
  return EXIT_SUCCESS;
}
