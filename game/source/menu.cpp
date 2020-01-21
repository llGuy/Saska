#include "ui.hpp"
#include "menu.hpp"


// Main menu

struct main_menu_t
{
    enum buttons_t { BROWSE_SERVER, HOST_SERVER, SETTINGS, QUIT, INVALID_MENU_BUTTON };
    
    font_t *main_menu_font;

    // Basially represents the area where the user can click
    ui_box_t main_menu_buttons[buttons_t::INVALID_MENU_BUTTON];

    // Text is to finish
};


enum menu_mode_t { NONE, MAIN_MENU, INVALID_MENU_MODE };

static menu_mode_t current_menu_mode;

static main_menu_t main_menu;


static void initialize_main_menu(void);


void initialize_menus(void)
{
    current_menu_mode = menu_mode_t::MAIN_MENU;
    
    initialize_main_menu();
}


void push_menus_to_render(gui_textured_vertex_render_list_t *textured_render_list,
                          gui_colored_vertex_render_list_t *colored_render_list,
                          element_focus_t focus)
{
    if (current_menu_mode == menu_mode_t::MAIN_MENU)
    {
        push_box_to_render(&main_menu.main_menu_buttons[main_menu_t::buttons_t::BROWSE_SERVER]);
    }
}


static void initialize_main_menu(void)
{
    main_menu.main_menu_buttons[main_menu_t::buttons_t::BROWSE_SERVER].initialize(LEFT_UP, 1.0f,
                                                                                  ui_vector2_t(0.1f, -0.1f),
                                                                                  ui_vector2_t(0.2f, 0.2f),
                                                                                  nullptr,
                                                                                  0xD6D6D636,
                                                                                  get_backbuffer_resolution());
}
