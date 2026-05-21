#pragma once
#include <string>

class EmojiPicker {
public:
    std::string last_selected;
    bool render_content();
    void reset_search();
private:
    char search_buf_[128] = {};
    int active_category_ = 0;
};
