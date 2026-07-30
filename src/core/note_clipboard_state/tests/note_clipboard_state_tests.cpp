#include "note_clipboard_state.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

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

void test_full_batch_round_trip()
{
  const std::vector<Json> items = {
      Json{{"title", "one"}, {"content", "first"}, {"font_path", "notes/fonts/a.ttf"}},
      Json{{"title", "two"}, {"content", "second"}}};
  const Json document = notepp::note_clipboard_state::make_document(items);
  const auto loaded = notepp::note_clipboard_state::read_items(document);
  expect(loaded.size() == 2, "the complete copied-note batch survives restart");
  expect(loaded[0]["font_path"] == "notes/fonts/a.ttf",
         "portable font metadata survives serialization");
}

void test_legacy_and_validation()
{
  const Json legacy = {{"has_note", true}, {"title", "old"}, {"content", "note"}};
  expect(notepp::note_clipboard_state::read_items(legacy).size() == 1,
         "legacy single-note clipboard migrates into a batch");
  expect(notepp::note_clipboard_state::validate_document(
             Json{{"schemaVersion", 3}, {"items", Json::array()}}, true) ==
             notepp::note_clipboard_state::DocumentState::future_schema,
         "future clipboard schemas are not rewritten");
  expect(notepp::note_clipboard_state::validate_document(
             Json{{"items", "bad"}}, true) ==
             notepp::note_clipboard_state::DocumentState::malformed,
         "malformed batches are rejected");
}
} // namespace

int main()
{
  test_full_batch_round_trip();
  test_legacy_and_validation();
  if(failures != 0) return EXIT_FAILURE;
  std::cout << "note_clipboard_state tests passed\n";
  return EXIT_SUCCESS;
}
