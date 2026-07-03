#include "markdown_code_highlight.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace mch = MarkdownCodeHighlight;

namespace
{
int failures = 0;

void expect_true(bool cond, std::string_view msg)
{
  if(cond) return;
  ++failures;
  std::cerr << "FAIL: " << msg << '\n';
}

void expect_eq_str(std::string_view a, std::string_view b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got \"" << a << "\", expected \"" << b << "\")\n";
}

void test_find_known_languages()
{
  expect_true(mch::find_language("c") != nullptr, "language 'c' is registered");
  expect_true(mch::find_language("cpp") != nullptr, "language 'cpp' is registered");
  expect_true(mch::find_language("python") != nullptr, "language 'python' is registered");
  expect_true(mch::find_language("bash") != nullptr, "language 'bash' is registered");
}

void test_find_aliases()
{
  // Aliases of cpp.
  const mch::LanguageDefinition *a = mch::find_language("c++");
  expect_true(a != nullptr, "alias 'c++' is recognized");
  expect_eq_str(a ? a->name : "", "cpp", "alias 'c++' resolves to cpp");

  const mch::LanguageDefinition *b = mch::find_language("py");
  expect_true(b != nullptr, "alias 'py' is recognized");
  expect_eq_str(b ? b->name : "", "python", "alias 'py' resolves to python");

  const mch::LanguageDefinition *c = mch::find_language("shell");
  expect_true(c != nullptr, "alias 'shell' is recognized");
  expect_eq_str(c ? c->name : "", "bash", "alias 'shell' resolves to bash");
}

void test_find_unknown_language()
{
  expect_true(mch::find_language("not-a-language") == nullptr, "unknown language returns null");
  expect_true(mch::find_language("") == nullptr, "empty fence info returns null");
}

void test_highlight_unrecognised_returns_empty()
{
  mch::HighlightedCodeBlock block = mch::highlight_code_block("not-a-language", "int x = 1;");
  expect_eq_str(block.requested_language, "not-a-language", "requested language recorded");
  expect_true(!block.recognized_language, "unrecognised language stays unrecognised");
  expect_true(block.spans.empty(), "unrecognised language has no spans");
}
} // namespace

int main()
{
  test_find_known_languages();
  test_find_aliases();
  test_find_unknown_language();
  test_highlight_unrecognised_returns_empty();
  if(failures != 0)
  {
    std::cerr << failures << " markdown_code_highlight test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "markdown_code_highlight tests passed\n";
  return EXIT_SUCCESS;
}
