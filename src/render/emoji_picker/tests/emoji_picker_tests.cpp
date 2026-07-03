#include "emoji_picker.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void test_initial_state()
{
  EmojiPicker picker;
  expect_eq_str(picker.last_selected, "", "initial last_selected is empty");
}

void test_reset_search_does_not_throw()
{
  EmojiPicker picker;
  picker.reset_search();
  expect_eq_str(picker.last_selected, "", "reset_search keeps last_selected empty");
}

void test_last_selected_assignable()
{
  EmojiPicker picker;
  picker.last_selected = "😀";
  expect_eq_str(picker.last_selected, "😀", "last_selected assignment works");
  picker.last_selected.clear();
  expect_eq_str(picker.last_selected, "", "last_selected can be cleared");
}
} // namespace

int main()
{
  test_initial_state();
  test_reset_search_does_not_throw();
  test_last_selected_assignable();
  if(failures != 0)
  {
    std::cerr << failures << " emoji_picker test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "emoji_picker tests passed\n";
  return EXIT_SUCCESS;
}
