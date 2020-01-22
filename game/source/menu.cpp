#include "ui.hpp"
#include "menu.hpp"


// Main menu

struct widget_t
{
    image2d_t image;
    uniform_group_t uniform;
};

struct main_menu_t
{
    enum buttons_t { BROWSE_SERVER, HOST_SERVER, SETTINGS, QUIT, INVALID_MENU_BUTTON };

    const char *button_names[5] = { "BROWSE_SERVER", "HOST_SERVER", "SETTINGS", "QUIT", "INVALID" };
    
    font_t *main_menu_font;


    widget_t widgets[buttons_t::INVALID_MENU_BUTTON];
    
    
    // Basially represents the area where the user can click
    ui_box_t main_menu_buttons[buttons_t::INVALID_MENU_BUTTON];
    ui_box_t main_menu_buttons_backgrounds[buttons_t::INVALID_MENU_BUTTON];

    // Text is to finish
};


enum menu_mode_t { NONE, MAIN_MENU, INVALID_MENU_MODE };

static menu_mode_t current_menu_mode;

static main_menu_t main_menu;


static void initialize_main_menu(void);
static void push_main_menu(gui_textured_vertex_render_list_t *textured_render_list,
                           gui_colored_vertex_render_list_t *colored_render_list);

static bool detect_if_user_clicked_on_button(main_menu_t::buttons_t button, float32_t cursor_x, float32_t cursor_y);


void initialize_menus(void)
{
    current_menu_mode = menu_mode_t::MAIN_MENU;
    
    initialize_main_menu();
}


void update_menus(raw_input_t *raw_input, element_focus_t focus)
{
    if (raw_input->buttons[button_type_t::MOUSE_LEFT].state >= button_state_t::INSTANT)
    {
        // Convert screen cursor positions to ndc coordinates
        // Need to take into account the scissor

        VkExtent2D swapchain_extent = get_swapchain_extent();

        resolution_t backbuffer_res = get_backbuffer_resolution();
    
        float32_t backbuffer_asp = (float32_t)backbuffer_res.width / (float32_t)backbuffer_res.height;
        float32_t swapchain_asp = (float32_t)get_swapchain_extent().width / (float32_t)get_swapchain_extent().height;

        uint32_t rect2D_width, rect2D_height, rect2Dx, rect2Dy;
    
        if (backbuffer_asp >= swapchain_asp)
        {
            rect2D_width = swapchain_extent.width;
            rect2D_height = (uint32_t)((float32_t)swapchain_extent.width / backbuffer_asp);
            rect2Dx = 0;
            rect2Dy = (swapchain_extent.height - rect2D_height) / 2;
        }

        if (backbuffer_asp < swapchain_asp)
        {
            rect2D_width = (uint32_t)(swapchain_extent.height * backbuffer_asp);
            rect2D_height = swapchain_extent.height;
            rect2Dx = (swapchain_extent.width - rect2D_width) / 2;
            rect2Dy = 0;
        }

        // Relative to the viewport coordinate system
        float32_t cursor_x = raw_input->cursor_pos_x;
        float32_t cursor_y = raw_input->cursor_pos_y;

        cursor_x = (float32_t)(cursor_x - rect2Dx) / (float32_t)rect2D_width;
        cursor_y = 1.0f - (float32_t)(cursor_y - rect2Dy) / (float32_t)rect2D_height;

        cursor_x = 2.0f * cursor_x - 1.0f;
        cursor_y = 2.0f * cursor_y - 1.0f;
    
        main_menu_t::buttons_t pressed_button = main_menu_t::buttons_t::INVALID_MENU_BUTTON;

        for (uint32_t i = 0; i < (uint32_t)main_menu_t::buttons_t::INVALID_MENU_BUTTON; ++i)
        {
            if (detect_if_user_clicked_on_button((main_menu_t::buttons_t)i, cursor_x, cursor_y))
            {
                output_to_debug_console(main_menu.button_names[i], "\n");
                break;
            }
        }
    }
}


void push_menus_to_render(gui_textured_vertex_render_list_t *textured_render_list,
                          gui_colored_vertex_render_list_t *colored_render_list,
                          element_focus_t focus)
{
    if (current_menu_mode == menu_mode_t::MAIN_MENU)
    {
        push_main_menu(textured_render_list, colored_render_list);
    }
}


static void initialize_main_menu(void)
{
    float32_t button_size = 0.15;

    // Relative to top right
    float32_t current_button_y = -0.2f;
    
    {
        main_menu_t::buttons_t button = main_menu_t::buttons_t::BROWSE_SERVER;
    
        main_menu.main_menu_buttons_backgrounds[button].initialize(RIGHT_UP, 1.0f,
                                                                   ui_vector2_t(-0.1f, current_button_y),
                                                                   ui_vector2_t(0.15f, 0.15f),
                                                                   nullptr,
                                                                   0x1616161636,
                                                                   get_backbuffer_resolution());
    
        main_menu.main_menu_buttons[button].initialize(CENTER, 1.0f,
                                                       ui_vector2_t(0.0f, 0.0f),
                                                       ui_vector2_t(0.8f, 0.8f),
                                                       &main_menu.main_menu_buttons_backgrounds[button],
                                                       0xFFFFFF36,
                                                       get_backbuffer_resolution());

        main_menu.widgets[button].uniform = create_texture_uniform("textures/gui/play_icon.png", &main_menu.widgets[button].image);

        current_button_y -= button_size;
    }

    {
        main_menu_t::buttons_t button = main_menu_t::buttons_t::HOST_SERVER;
    
        main_menu.main_menu_buttons_backgrounds[button].initialize(RIGHT_UP, 1.0f,
                                                                   ui_vector2_t(-0.1f, current_button_y),
                                                                   ui_vector2_t(0.15f, 0.15f),
                                                                   nullptr,
                                                                   0x1616161636,
                                                                   get_backbuffer_resolution());
    
        main_menu.main_menu_buttons[button].initialize(CENTER, 1.0f,
                                                       ui_vector2_t(0.0f, 0.0f),
                                                       ui_vector2_t(0.8f, 0.8f),
                                                       &main_menu.main_menu_buttons_backgrounds[button],
                                                       0xFFFFFF36,
                                                       get_backbuffer_resolution());

        main_menu.widgets[button].uniform = create_texture_uniform("textures/gui/host_icon.png", &main_menu.widgets[button].image);

        current_button_y -= button_size;
    }
    
    {
        main_menu_t::buttons_t button = main_menu_t::buttons_t::SETTINGS;
    
        main_menu.main_menu_buttons_backgrounds[button].initialize(RIGHT_UP, 1.0f,
                                                                   ui_vector2_t(-0.1f, current_button_y),
                                                                   ui_vector2_t(0.15f, 0.15f),
                                                                   nullptr,
                                                                   0x1616161636,
                                                                   get_backbuffer_resolution());
    
        main_menu.main_menu_buttons[button].initialize(CENTER, 1.0f,
                                                       ui_vector2_t(0.0f, 0.0f),
                                                       ui_vector2_t(0.8f, 0.8f),
                                                       &main_menu.main_menu_buttons_backgrounds[button],
                                                       0xFFFFFF36,
                                                       get_backbuffer_resolution());

        main_menu.widgets[button].uniform = create_texture_uniform("textures/gui/settings_icon.png", &main_menu.widgets[button].image);

        current_button_y -= button_size;
    }

    {
        main_menu_t::buttons_t button = main_menu_t::buttons_t::QUIT;
    
        main_menu.main_menu_buttons_backgrounds[button].initialize(RIGHT_UP, 1.0f,
                                                                   ui_vector2_t(-0.1f, current_button_y),
                                                                   ui_vector2_t(0.15f, 0.15f),
                                                                   nullptr,
                                                                   0x1616161636,
                                                                   get_backbuffer_resolution());
    
        main_menu.main_menu_buttons[button].initialize(CENTER, 1.0f,
                                                       ui_vector2_t(0.0f, 0.0f),
                                                       ui_vector2_t(0.8f, 0.8f),
                                                       &main_menu.main_menu_buttons_backgrounds[button],
                                                       0xFFFFFF36,
                                                       get_backbuffer_resolution());

        main_menu.widgets[button].uniform = create_texture_uniform("textures/gui/quit_icon.png", &main_menu.widgets[button].image);

        current_button_y -= button_size;
    }
}


static void push_main_menu(gui_textured_vertex_render_list_t *textured_render_list,
                           gui_colored_vertex_render_list_t *colored_render_list)
{
    for (uint32_t i = 0; i < main_menu_t::buttons_t::INVALID_MENU_BUTTON; ++i)
    {
        main_menu_t::buttons_t button = (main_menu_t::buttons_t)i;
        push_box_to_render_with_texture(&main_menu.main_menu_buttons[button], main_menu.widgets[button].uniform);

        push_box_to_render(&main_menu.main_menu_buttons_backgrounds[button]);
    }
}


static bool detect_if_user_clicked_on_button(main_menu_t::buttons_t button, float32_t cursor_x, float32_t cursor_y)
{
    ui_box_t *box = &main_menu.main_menu_buttons_backgrounds[button];
    
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;

    float32_t x_min = normalized_base_position.x,
        x_max = normalized_base_position.x + normalized_size.x,
        y_min = normalized_base_position.y,
        y_max = normalized_base_position.y + normalized_size.y;

    if (x_min < cursor_x && x_max > cursor_x
        && y_min < cursor_y && y_max > cursor_y)
    {
        return(true);
    }
    else
    {
        return(false);
    }
}
