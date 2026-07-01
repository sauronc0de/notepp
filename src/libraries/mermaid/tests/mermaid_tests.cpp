#include "mermaid_diagrams.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace md = MermaidDiagrams;

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

void expect_eq_size(std::size_t a, std::size_t b, std::string_view msg)
{
  if(a == b) return;
  ++failures;
  std::cerr << "FAIL: " << msg << " (got " << a << ", expected " << b << ")\n";
}

void test_sequence_basic_valid()
{
  const std::string src =
      "sequenceDiagram\n"
      "  participant Alice\n"
      "  participant Bob\n"
      "  Alice->>Bob: Hello\n"
      "  Bob-->>Alice: Hi back\n";
  md::SequenceDiagram d;
  expect_true(md::parse_sequence(src, d), "valid sequence parses");
  expect_eq_size(d.participants.size(), 2, "two participants");
  expect_eq_str(d.participants[0].id, "Alice", "first id");
  expect_eq_size(d.messages.size(), 2, "two messages");
  expect_eq_str(d.messages[0].from, "Alice", "first from");
  expect_eq_str(d.messages[0].to, "Bob", "first to");
  expect_eq_str(d.messages[0].text, "Hello", "first text");
}

void test_sequence_missing_header()
{
  const std::string src =
      "participant Alice\n"
      "participant Bob\n";
  md::SequenceDiagram d;
  expect_true(!md::parse_sequence(src, d), "no header fails");
}

void test_sequence_no_participants()
{
  const std::string src = "sequenceDiagram\n";
  md::SequenceDiagram d;
  expect_true(!md::parse_sequence(src, d), "no participants fails");
}

void test_sequence_with_notes_and_groups()
{
  const std::string src =
      "sequenceDiagram\n"
      "  participant A\n"
      "  participant B\n"
      "  Note over A,B: hello\n"
      "  loop daily\n"
      "    A->>B: ping\n"
      "  end\n";
  md::SequenceDiagram d;
  expect_true(md::parse_sequence(src, d), "valid with notes and groups");
  expect_eq_size(d.notes.size(), 1, "one note");
  expect_true(!d.events.empty(), "events populated");
}

void test_class_basic_valid()
{
  const std::string src =
      "classDiagram\n"
      "  class Animal {\n"
      "    +String name\n"
      "    +makeSound() void\n"
      "  }\n"
      "  class Dog {\n"
      "    +fetch() void\n"
      "  }\n"
      "  Animal <|-- Dog\n";
  md::ClassDiagram d;
  expect_true(md::parse_class(src, d), "valid class parses");
  expect_eq_size(d.classes.size(), 2, "two classes");
  expect_eq_str(d.classes[0].name, "Animal", "first class name");
  expect_true(!d.classes[0].members.empty(), "Animal has members");
  expect_eq_size(d.relations.size(), 1, "one inheritance relation");
}

void test_class_missing_header()
{
  md::ClassDiagram d;
  expect_true(!md::parse_class("class Foo", d), "missing header rejected");
}

void test_class_empty()
{
  md::ClassDiagram d;
  expect_true(!md::parse_class("classDiagram\n", d), "empty class fails");
}

void test_state_basic_valid()
{
  const std::string src =
      "stateDiagram-v2\n"
      "  [*] --> Still\n"
      "  Still --> Moving\n"
      "  Moving --> Still\n"
      "  Moving --> [*]\n";
  md::StateDiagram d;
  expect_true(md::parse_state(src, d), "valid state parses");
  expect_true(!d.states.empty(), "states populated");
  expect_true(!d.transitions.empty(), "transitions populated");
}

void test_state_missing_header()
{
  md::StateDiagram d;
  expect_true(!md::parse_state("Still --> Moving", d), "missing header rejected");
}

void test_state_with_label()
{
  const std::string src =
      "stateDiagram-v2\n"
      "  state Active as \"Active State\"\n"
      "  [*] --> Active : start\n";
  md::StateDiagram d;
  expect_true(md::parse_state(src, d), "valid state with label parses");
  expect_true(!d.states.empty(), "states populated");
}

void test_parse_result_helpers()
{
  // make_parsed produces an ok() result with the value and an empty error.
  auto ok = md::make_parsed(md::StateDiagram{});
  expect_true(ok.ok(), "make_parsed yields ok");
  expect_eq_size(ok.error.size(), 0, "ok result has no error");

  // make_parse_error produces a failed result with the message.
  auto err = md::make_parse_error<md::StateDiagram>("bad input");
  expect_true(!err.ok(), "make_parse_error yields not ok");
  expect_eq_str(err.error, "bad input", "error message preserved");

  // Default-constructed result is not ok.
  md::ParseResult<md::StateDiagram> def;
  expect_true(!def.ok(), "default result is not ok");
}

void test_er_basic_valid()
{
  const std::string src =
      "erDiagram\n"
      "  CUSTOMER ||--o{ ORDER : places\n"
      "  ORDER ||--|{ LINE-ITEM : contains\n";
  md::ERDiagram d;
  expect_true(md::parse_er(src, d), "valid ER parses");
  expect_eq_size(d.entities.size(), 3, "three entities");
  expect_eq_size(d.relations.size(), 2, "two relations");
}

void test_er_missing_header()
{
  const std::string src = "CUSTOMER ||--o{ ORDER : places\n";
  md::ERDiagram d;
  expect_true(!md::parse_er(src, d), "missing header rejected");
}

void test_er_with_attributes()
{
  const std::string src =
      "erDiagram\n"
      "  CUSTOMER {\n"
      "    string name PK\n"
      "    string email\n"
      "  }\n";
  md::ERDiagram d;
  expect_true(md::parse_er(src, d), "valid ER with attrs parses");
  expect_eq_size(d.entities.size(), 1, "one entity");
  expect_eq_size(d.entities[0].attrs.size(), 2, "two attributes");
  expect_true(d.entities[0].attrs[0].pk, "first attr is PK");
}
} // namespace

int main()
{
  test_sequence_basic_valid();
  test_sequence_missing_header();
  test_sequence_no_participants();
  test_sequence_with_notes_and_groups();
  test_class_basic_valid();
  test_class_missing_header();
  test_class_empty();
  test_state_basic_valid();
  test_state_missing_header();
  test_state_with_label();
  test_parse_result_helpers();
  test_er_basic_valid();
  test_er_missing_header();
  test_er_with_attributes();
  if(failures != 0)
  {
    std::cerr << failures << " mermaid test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "mermaid tests passed\n";
  return EXIT_SUCCESS;
}