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
  const auto decoded_legacy = decode_document(valid_profile);
  expect(decoded_legacy.has_value() &&
             !decoded_legacy->profiles[0].note_layouts.at("n").always_on_top,
         "legacy note layouts default always-on-top to false");
  Json pinned_profile = valid_profile;
  pinned_profile["profiles"][0]["note_layouts"][0]["always_on_top"] = true;
  const auto decoded_pinned = decode_document(pinned_profile);
  expect(decoded_pinned.has_value() &&
             decoded_pinned->profiles[0].note_layouts.at("n").always_on_top,
         "always-on-top layout state decodes");

  Json bad_hidden = valid_profile;
  bad_hidden["profiles"][0]["note_layouts"][0]["hidden"] = "false";
  Json bad_always_on_top = valid_profile;
  bad_always_on_top["profiles"][0]["note_layouts"][0]["always_on_top"] = "false";
  for(const Json &malformed :
      std::vector<Json>{bad_active, bad_name, bad_width, bad_hidden, bad_always_on_top})
    expect(validate_document(malformed, true) == DocumentState::malformed,
           "malformed known layout profile fields make the document read-only");
}

void test_structured_decode_ignores_shadow_keys_in_extensions()
{
  const Json document = {
      {"aaaExtension", Json{{"active_profile_id", "shadow-profile"},
                            {"profiles", Json::array({Json{{"id", "shadow"}}})},
                            {"note_layouts", Json::array({Json{{"note_id", "shadow-note"}}})}}},
      {"schemaVersion", 1},
      {"active_profile_id", "canonical-profile"},
      {"maximized_profile_id", "max-profile"},
      {"reduced_profile_id", "reduced-profile"},
      {"profiles", Json::array({Json{
                       {"aaaExtension", Json{{"note_layouts", Json::array({Json{{"note_id", "nested-shadow"}}})}}},
                       {"id", "canonical-profile"},
                       {"name", "Canonical"},
                       {"window_maximized", false},
                       {"window_x", 10},
                       {"window_y", 20},
                       {"window_w", 1200},
                       {"window_h", 800},
                       {"note_layouts", Json::array({Json{{"aaaExtension", Json{{"x", 999}}},
                                                          {"note_id", "stable-note"},
                                                          {"x", 30},
                                                          {"y", 40},
                                                          {"w", 600},
                                                          {"h", 300},
                                                          {"dock_id", 7},
                                                          {"hidden", true},
                                                          {"always_on_top", true},
                                                          {"has_layout", true}}})}}})}};

  const Json merged = notepp::layout_profile_state::merge_unknown_fields(document, document);
  expect(merged["aaaExtension"]["active_profile_id"] == "shadow-profile",
         "shadowing extension data remains preserved during merge");
  const auto decoded = notepp::layout_profile_state::decode_document(merged);
  expect(decoded.has_value(), "validated merged profile state decodes into structured state");
  if(!decoded) return;
  expect(decoded->active_profile_id == "canonical-profile" &&
             decoded->maximized_profile_id == "max-profile" &&
             decoded->reduced_profile_id == "reduced-profile",
         "extension scalar keys cannot shadow canonical profile selection");
  expect(decoded->profiles.size() == 1 &&
             decoded->profiles[0].id == "canonical-profile" &&
             decoded->profiles[0].name == "Canonical",
         "extension profiles cannot shadow the canonical profiles array");
  expect(decoded->profiles[0].window_x == 10 &&
             decoded->profiles[0].window_w == 1200 &&
             !decoded->profiles[0].window_maximized,
         "canonical window state survives structured decoding");
  const auto layout = decoded->profiles[0].note_layouts.find("stable-note");
  expect(layout != decoded->profiles[0].note_layouts.end() &&
             layout->second.x == 30 && layout->second.width == 600 &&
             layout->second.dock_id == 7 && layout->second.hidden &&
             layout->second.always_on_top && layout->second.has_layout,
         "extension note layouts cannot shadow canonical layout state");
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
  test_structured_decode_ignores_shadow_keys_in_extensions();
  test_unknown_fields_follow_stable_ids();
  if(failures != 0) return EXIT_FAILURE;
  std::cout << "layout_profile_state tests passed\n";
  return EXIT_SUCCESS;
}
