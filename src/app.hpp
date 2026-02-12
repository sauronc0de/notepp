#pragma once
#include <string>

struct SDL_Window;

class App
{
public:
  int run();

private:
  void init_sdl_gl();
  void init_imgui();
  void shutdown();

  void frame_begin();
  void frame_ui();
  void frame_end();

  SDL_Window *window_ = nullptr;
  void *gl_context_ = nullptr;

  bool running_ = true;

  std::string markdown_text_ =
      "# Notes (Markdown preview)\n"
      "\n"
      "This is a **minimal** ImGui app using `enkisoftware/imgui_markdown`.\n"
      "\n"
      "- Lists\n"
      "- `inline code`\n"
      "- [Links](https://github.com/enkisoftware/imgui_markdown)\n"
      "\n"
      "```cpp\n"
      "int main(){ return 0; }\n"
      "```\n";
};
