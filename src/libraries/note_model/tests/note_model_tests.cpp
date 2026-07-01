#include "folder_meta.hpp"
#include "layout_profile.hpp"
#include "note_meta.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace nm = notepp::note_model;

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

void test_note_meta_defaults()
{
  nm::NoteMeta meta;
  expect_true(meta.id.empty(), "id default empty");
  expect_true(meta.title.empty(), "title default empty");
  expect_eq_str(meta.path, "", "path default empty");
  expect_true(meta.font_size == 0.0f, "font_size default 0");
  expect_true(!meta.use_custom_color, "use_custom_color default false");
  expect_true(meta.width == 520.0f, "width default 520");
  expect_true(meta.height == 260.0f, "height default 260");
  expect_true(!meta.hidden, "hidden default false");
  expect_true(!meta.always_on_top, "always_on_top default false");
  expect_true(meta.dock_id == 0, "dock_id default 0");
}

void test_folder_meta_defaults()
{
  nm::FolderMeta folder;
  expect_true(folder.name.empty(), "name default empty");
  expect_true(folder.notes.empty(), "notes default empty");
  expect_true(folder.images.empty(), "images default empty");
  expect_true(!folder.layout_locked, "layout_locked default false");
  expect_true(folder.drawings_visible, "drawings_visible default true");
  expect_true(!folder.grid_visible, "grid_visible default false");
}

void test_layout_profile_defaults()
{
  nm::LayoutProfile profile;
  expect_true(profile.id.empty(), "id default empty");
  expect_true(profile.name.empty(), "name default empty");
  expect_true(profile.window_maximized, "window_maximized default true");
  expect_true(profile.window_x == 100, "window_x default 100");
  expect_true(profile.window_y == 100, "window_y default 100");
  expect_true(profile.window_w == 1100, "window_w default 1100");
  expect_true(profile.window_h == 700, "window_h default 700");
  expect_true(!profile.pending_delete, "pending_delete default false");
  expect_true(profile.note_layouts.empty(), "note_layouts default empty");
}

void test_note_layout_data_defaults()
{
  nm::NoteLayoutData layout;
  expect_true(layout.pos_x == 0.0f, "pos_x default 0");
  expect_true(layout.pos_y == 0.0f, "pos_y default 0");
  expect_true(layout.width == 520.0f, "width default 520");
  expect_true(layout.height == 260.0f, "height default 260");
  expect_true(!layout.hidden, "hidden default false");
  expect_true(!layout.has_layout, "has_layout default false");
  expect_true(layout.dock_id == 0, "dock_id default 0");
}

void test_assignment_works()
{
  nm::NoteMeta a;
  a.title = "Hello";
  a.path = "/tmp/note.md";
  a.width = 300.0f;

  nm::NoteMeta b = a;
  expect_eq_str(b.title, "Hello", "assigned title preserved");
  expect_eq_str(b.path, "/tmp/note.md", "assigned path preserved");
  expect_true(b.width == 300.0f, "assigned width preserved");
}
} // namespace

int main()
{
  test_note_meta_defaults();
  test_folder_meta_defaults();
  test_layout_profile_defaults();
  test_note_layout_data_defaults();
  test_assignment_works();
  if(failures != 0)
  {
    std::cerr << failures << " note_model test expectation(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "note_model tests passed\n";
  return EXIT_SUCCESS;
}