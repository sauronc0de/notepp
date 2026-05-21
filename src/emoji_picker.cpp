#include "emoji_picker.hpp"
#include <imgui.h>
#include <cstring>

struct EmojiEntry { const char* utf8; const char* name; };
struct EmojiCategory { const char* icon; const char* label; const EmojiEntry* entries; int count; };

static const EmojiEntry k_smileys[] = {
    {"😀","grinning"},{"😃","smiley"},{"😄","smile"},{"😁","grin"},{"😆","laughing"},
    {"😅","sweat_smile"},{"😂","joy"},{"🤣","rofl"},{"🥲","smile_tear"},{"😊","blush"},
    {"😇","innocent"},{"🙂","slightly_smiling"},{"🙃","upside_down"},{"😉","wink"},
    {"😍","heart_eyes"},{"🥰","smiling_hearts"},{"😘","kissing_heart"},{"😋","yum"},
    {"😎","sunglasses"},{"🤓","nerd"},{"🥸","disguised"},{"🤩","star_struck"},
    {"🥳","partying"},{"😏","smirk"},{"😒","unamused"},{"😞","disappointed"},
    {"😔","pensive"},{"🙁","slightly_frowning"},{"😣","persevere"},{"😫","tired"},
    {"😩","weary"},{"🥺","pleading"},{"😢","cry"},{"😭","sob"},{"😤","triumph"},
    {"😠","angry"},{"😡","rage"},{"🤬","exploding_head_mad"},{"🤯","exploding_head"},
    {"😳","flushed"},{"🥵","hot"},{"🥶","cold"},{"😱","scream"},{"😨","fearful"},
    {"😰","anxious"},{"😥","sad_relieved"},{"😓","sweat"},{"🤗","hugging"},
    {"🤔","thinking"},{"🤭","hand_over_mouth"},{"🤫","shushing"},{"🤥","lying"},
    {"😶","no_mouth"},{"😐","neutral"},{"😑","expressionless"},{"😬","grimacing"},
    {"🙄","eye_roll"},{"😯","hushed"},{"😮","open_mouth"},{"😲","astonished"},
    {"🥱","yawning"},{"😴","sleeping"},{"🤤","drooling"},{"😵","dizzy"},
    {"🤒","sick"},{"🤕","injured"},{"🤑","money_mouth"},{"🤠","cowboy"},
    {"😈","smiling_devil"},{"👻","ghost"},{"💀","skull"},{"🤖","robot"},
};

static const EmojiEntry k_people[] = {
    {"👋","wave"},{"🤚","raised_back_hand"},{"✋","hand"},{"🤙","call_me"},
    {"👍","thumbsup"},{"👎","thumbsdown"},{"✊","fist"},{"👊","punch"},
    {"👏","clap"},{"🙌","raised_hands"},{"🤝","handshake"},{"🙏","pray"},
    {"💪","muscle"},{"🦾","mechanical_arm"},{"👀","eyes"},{"👁","eye"},
    {"👅","tongue"},{"👂","ear"},{"👃","nose"},{"🫀","heart_anatomy"},
    {"👶","baby"},{"🧒","child"},{"👦","boy"},{"👧","girl"},{"🧑","person"},
    {"👨","man"},{"👩","woman"},{"👴","older_man"},{"👵","older_woman"},
    {"🧔","bearded_person"},{"👮","police_officer"},{"🧙","mage"},
    {"🧚","fairy"},{"🧛","vampire"},{"🧜","merperson"},{"🧝","elf"},
    {"🧞","genie"},{"🧟","zombie"},{"💃","dancer"},{"🕺","man_dancing"},
    {"🤺","fencer"},{"🧗","climber"},{"🏋","weight_lifter"},{"🤸","cartwheel"},
    {"🤼","wrestlers"},{"🏊","swimmer"},{"🚴","cyclist"},{"🧘","lotus"},
};

static const EmojiEntry k_animals[] = {
    {"🐶","dog"},{"🐱","cat"},{"🐭","mouse"},{"🐹","hamster"},{"🐰","rabbit"},
    {"🦊","fox"},{"🐻","bear"},{"🐼","panda"},{"🐨","koala"},{"🐯","tiger"},
    {"🦁","lion"},{"🐮","cow"},{"🐷","pig"},{"🐸","frog"},{"🐵","monkey"},
    {"🙈","see_no_evil"},{"🙉","hear_no_evil"},{"🙊","speak_no_evil"},
    {"🐔","chicken"},{"🐧","penguin"},{"🐦","bird"},{"🦆","duck"},
    {"🦅","eagle"},{"🦉","owl"},{"🦇","bat"},{"🐺","wolf"},{"🐴","horse"},
    {"🦄","unicorn"},{"🐝","bee"},{"🦋","butterfly"},{"🐌","snail"},
    {"🐞","ladybug"},{"🐢","turtle"},{"🐍","snake"},{"🦎","lizard"},
    {"🐙","octopus"},{"🦑","squid"},{"🦈","shark"},{"🐬","dolphin"},
    {"🐳","whale"},{"🐟","fish"},{"🐠","tropical_fish"},{"🐡","blowfish"},
    {"🐘","elephant"},{"🦏","rhino"},{"🦛","hippo"},{"🦒","giraffe"},
    {"🦓","zebra"},{"🦘","kangaroo"},{"🐪","camel"},{"🐫","two_hump_camel"},
    {"🐕","dog2"},{"🐈","cat2"},{"🐓","rooster"},{"🦃","turkey"},
};

static const EmojiEntry k_food[] = {
    {"🍎","apple"},{"🍊","orange"},{"🍋","lemon"},{"🍇","grapes"},
    {"🍓","strawberry"},{"🫐","blueberries"},{"🍒","cherries"},{"🍑","peach"},
    {"🍍","pineapple"},{"🥭","mango"},{"🥥","coconut"},{"🥝","kiwi"},
    {"🍅","tomato"},{"🥑","avocado"},{"🍆","eggplant"},{"🥕","carrot"},
    {"🌽","corn"},{"🌶","pepper"},{"🥒","cucumber"},{"🧄","garlic"},
    {"🧅","onion"},{"🥜","peanuts"},{"🍞","bread"},{"🥐","croissant"},
    {"🧀","cheese"},{"🥚","egg"},{"🍳","cooking"},{"🥞","pancakes"},
    {"🧇","waffle"},{"🥓","bacon"},{"🥩","meat"},{"🍗","chicken_leg"},
    {"🌭","hotdog"},{"🍔","burger"},{"🍟","fries"},{"🍕","pizza"},
    {"🌮","taco"},{"🌯","burrito"},{"🍣","sushi"},{"🍜","ramen"},
    {"🍝","spaghetti"},{"🍲","stew"},{"🍛","curry"},{"🍱","bento"},
    {"🥟","dumpling"},{"🍙","rice_ball"},{"🍚","rice"},{"🧁","cupcake"},
    {"🍰","shortcake"},{"🎂","birthday"},{"🍭","lollipop"},{"🍫","chocolate"},
    {"🍿","popcorn"},{"🍩","donut"},{"🍪","cookie"},{"☕","coffee"},
    {"🍵","tea"},{"🧃","juice"},{"🥤","cup_straw"},{"🧋","bubble_tea"},
    {"🍺","beer"},{"🍻","beers"},{"🥂","clinking_glasses"},{"🍷","wine"},
};

static const EmojiEntry k_travel[] = {
    {"✈️","airplane"},{"🚀","rocket"},{"🛸","ufo"},{"🚁","helicopter"},
    {"🚗","car"},{"🚕","taxi"},{"🚙","suv"},{"🛻","pickup"},
    {"🚌","bus"},{"🚑","ambulance"},{"🚒","fire_truck"},{"🚓","police_car"},
    {"🏍","motorcycle"},{"🚲","bike"},{"🛴","scooter"},{"🚜","tractor"},
    {"🚛","truck"},{"🚂","train"},{"🛳️","cruise_ship"},{"🚢","ship"},
    {"⛵","sailboat"},{"⚓","anchor"},{"🗺","world_map"},{"🏔","mountain"},
    {"⛰️","mountain_snow"},{"🌋","volcano"},{"🏕","camping"},{"🏖","beach"},
    {"🏙","cityscape"},{"🏛","classical_building"},{"🏠","house"},
    {"🏡","house_garden"},{"🏢","office"},{"🏥","hospital"},{"🏦","bank"},
    {"🏨","hotel"},{"🏪","convenience_store"},{"🏫","school"},{"🏬","department"},
    {"🗼","tokyo_tower"},{"🗽","statue_liberty"},{"🏰","castle"},{"⛪","church"},
    {"🕌","mosque"},{"🛕","hindu_temple"},{"⛩️","shinto_shrine"},
    {"🌁","foggy"},{"🌃","night_stars"},{"🌆","cityscape_dusk"},
    {"🌉","bridge_night"},{"🌈","rainbow"},{"🌊","ocean"},
};

static const EmojiEntry k_objects[] = {
    {"💡","bulb"},{"🔦","flashlight"},{"🕯️","candle"},{"🧯","extinguisher"},
    {"💰","money_bag"},{"💵","dollar"},{"💳","credit_card"},{"🪙","coin"},
    {"📱","phone"},{"💻","laptop"},{"🖥️","desktop"},{"⌨️","keyboard"},
    {"🖱️","mouse"},{"💾","floppy"},{"💿","cd"},{"📷","camera"},
    {"📸","camera_flash"},{"📹","video_camera"},{"📺","tv"},{"📻","radio"},
    {"⌚","watch"},{"⏰","alarm"},{"⏱️","stopwatch"},{"🧭","compass"},
    {"🔧","wrench"},{"🪛","screwdriver"},{"🔨","hammer"},{"🔩","nut_bolt"},
    {"🔑","key"},{"🗝️","old_key"},{"🔒","locked"},{"🔓","unlocked"},
    {"🔔","bell"},{"🔕","no_bell"},{"🎵","musical_note"},{"🎶","notes"},
    {"🎸","guitar"},{"🎹","piano"},{"🎺","trumpet"},{"🎻","violin"},
    {"🥁","drum"},{"🎤","microphone"},{"🎧","headphones"},{"🎬","clapper"},
    {"🎮","video_game"},{"🕹️","joystick"},{"🎯","dart"},{"🎲","dice"},
    {"🏆","trophy"},{"🥇","gold_medal"},{"🎁","gift"},{"🎉","party_popper"},
    {"✨","sparkles"},{"🎊","confetti"},{"🎀","ribbon"},{"📚","books"},
    {"📖","open_book"},{"📝","memo"},{"✏️","pencil"},{"🖊️","pen"},
    {"📌","pushpin"},{"📎","paperclip"},{"🔍","magnifier"},{"🔭","telescope"},
    {"🔬","microscope"},{"🧬","dna"},{"💊","pill"},{"💉","syringe"},
};

static const EmojiEntry k_nature[] = {
    {"🌸","cherry_blossom"},{"🌼","blossom"},{"🌻","sunflower"},{"🌺","hibiscus"},
    {"🌹","rose"},{"🥀","wilted"},{"🌷","tulip"},{"🌱","seedling"},
    {"🌿","herb"},{"☘️","shamrock"},{"🍀","four_leaf"},{"🍃","leaves"},
    {"🍂","fallen_leaf"},{"🍁","maple"},{"🌾","sheaf"},{"🌵","cactus"},
    {"🎄","christmas_tree"},{"🌴","palm"},{"🌳","tree"},{"🌲","evergreen"},
    {"🪵","wood"},{"🍄","mushroom"},{"🌰","chestnut"},{"🦔","hedgehog"},
    {"🌞","sun_face"},{"🌝","full_moon_face"},{"🌙","crescent"},{"⭐","star"},
    {"🌟","glowing_star"},{"💫","dizzy_star"},{"🌠","shooting_star"},
    {"🪐","ringed_planet"},{"☁️","cloud"},{"⛅","partly_cloudy"},
    {"🌤️","sun_small_cloud"},{"🌦️","rain"},{"🌧️","cloud_rain"},
    {"⛈️","thunder"},{"🌨️","snow_cloud"},{"❄️","snowflake"},{"⛄","snowman"},
    {"🌪️","tornado"},{"🌀","cyclone"},{"🌈","rainbow"},{"🔥","fire"},
    {"💧","droplet"},{"🌊","ocean"},{"💦","sweat_droplets"},{"⚡","lightning"},
};

static const EmojiEntry k_symbols[] = {
    {"❤️","red_heart"},{"🧡","orange_heart"},{"💛","yellow_heart"},
    {"💚","green_heart"},{"💙","blue_heart"},{"💜","purple_heart"},
    {"🖤","black_heart"},{"🤍","white_heart"},{"🤎","brown_heart"},
    {"💔","broken_heart"},{"❣️","heart_exclamation"},{"💕","two_hearts"},
    {"💞","revolving_hearts"},{"💓","beating_heart"},{"💗","growing_heart"},
    {"💖","sparkling_heart"},{"💘","heart_arrow"},{"💝","gift_heart"},
    {"✅","check_mark"},{"❌","cross"},{"⭕","circle"},{"⚠️","warning"},
    {"🚫","prohibited"},{"💯","hundred"},{"❓","question"},{"❗","exclamation"},
    {"‼️","double_exclamation"},{"🔞","no_eighteen"},{"🆕","new"},
    {"🆒","cool"},{"🆓","free"},{"🆗","ok"},{"🆙","up"},{"🆚","vs"},
    {"🔴","red_circle"},{"🟠","orange_circle"},{"🟡","yellow_circle"},
    {"🟢","green_circle"},{"🔵","blue_circle"},{"🟣","purple_circle"},
    {"⚫","black_circle"},{"⚪","white_circle"},{"🟤","brown_circle"},
    {"🔶","orange_diamond"},{"🔷","blue_diamond"},{"🔸","small_orange_diamond"},
    {"🔹","small_blue_diamond"},{"⬛","black_square"},{"⬜","white_square"},
    {"▶️","play"},{"⏩","fast_forward"},{"⏸️","pause"},{"⏹️","stop"},
    {"🔀","shuffle"},{"🔁","repeat"},{"🔂","repeat_one"},
    {"🔇","muted"},{"🔈","speaker"},{"🔉","sound"},{"🔊","loud_sound"},
    {"🏳️","white_flag"},{"🏴","black_flag"},{"🚩","triangular_flag"},{"🏁","chequered_flag"},
};

#define ARRAY_COUNT(a) ((int)(sizeof(a)/sizeof(a[0])))

static const EmojiCategory k_categories[] = {
    {"😊", "Smileys",  k_smileys,  ARRAY_COUNT(k_smileys)},
    {"👋", "People",   k_people,   ARRAY_COUNT(k_people)},
    {"🐶", "Animals",  k_animals,  ARRAY_COUNT(k_animals)},
    {"🍕", "Food",     k_food,     ARRAY_COUNT(k_food)},
    {"✈️", "Travel",   k_travel,   ARRAY_COUNT(k_travel)},
    {"💡", "Objects",  k_objects,  ARRAY_COUNT(k_objects)},
    {"🌸", "Nature",   k_nature,   ARRAY_COUNT(k_nature)},
    {"❤️", "Symbols",  k_symbols,  ARRAY_COUNT(k_symbols)},
};
static const int k_category_count = ARRAY_COUNT(k_categories);

void EmojiPicker::reset_search()
{
    search_buf_[0] = '\0';
    active_category_ = 0;
}

bool EmojiPicker::render_content()
{
    bool selected = false;

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##emoji_search", search_buf_, sizeof(search_buf_));
    ImGui::Separator();

    // Category tab row
    const float tab_size = 28.0f;
    for (int i = 0; i < k_category_count; i++) {
        if (i > 0) ImGui::SameLine(0.0f, 2.0f);
        const bool active = (active_category_ == i);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        ImGui::PushID(i);
        if (ImGui::Button(k_categories[i].icon, {tab_size, tab_size}))
            active_category_ = i;
        ImGui::PopID();
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", k_categories[i].label);
    }
    ImGui::Separator();

    // Emoji grid
    const float btn = 30.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    int cols = (int)((avail + spacing) / (btn + spacing));
    if (cols < 1) cols = 1;

    ImGui::BeginChild("##emoji_grid", {0.0f, 0.0f}, false);

    const bool searching = search_buf_[0] != '\0';
    int col = 0;

    auto try_emit = [&](const EmojiEntry& e) {
        if (searching && !strstr(e.name, search_buf_)) return;
        if (col > 0 && col % cols != 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(e.utf8);
        if (ImGui::Button(e.utf8, {btn, btn})) {
            last_selected = e.utf8;
            selected = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", e.name);
        ImGui::PopID();
        col++;
    };

    if (searching) {
        for (int ci = 0; ci < k_category_count; ci++)
            for (int j = 0; j < k_categories[ci].count; j++)
                try_emit(k_categories[ci].entries[j]);
    } else {
        const EmojiCategory& cat = k_categories[active_category_];
        for (int j = 0; j < cat.count; j++)
            try_emit(cat.entries[j]);
    }

    ImGui::EndChild();
    return selected;
}
