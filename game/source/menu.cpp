#include "ui.hpp"
#include "menu.hpp"
#include "core.hpp"


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

    // Won't get rendered
    ui_box_t main_menu;

    // These are relative to the main menu
    // Basially represents the area where the user can click
    ui_box_t main_menu_buttons[buttons_t::INVALID_MENU_BUTTON];
    ui_box_t main_menu_buttons_backgrounds[buttons_t::INVALID_MENU_BUTTON];

    ui_box_t main_menu_slider;

    bool in_out_transition = 0;
    buttons_t selected_menu = INVALID_MENU_BUTTON;
    float32_t slider_x_min;
    float32_t slider_x_max_size;
    float32_t slider_y_max_size;
    smooth_linear_interpolation_t<float32_t> main_menu_slider_x = {};

    buttons_t hovering_over;

    // TODO: Make this something that each button has (for a fade out animation)
    smooth_linear_interpolation_t<vector3_t> background_color_interpolation;
    smooth_linear_interpolation_t<vector3_t> icon_color_interpolation;

    // For the browse server menu
    struct
    {
        bool typing_in_input = 0;
        ui_box_t ip_address_input_box;
        ui_input_text_t input_text;
    } browse_menu;
};


enum menu_mode_t { NONE, MAIN_MENU, INVALID_MENU_MODE };

static menu_mode_t current_menu_mode;

static main_menu_t main_menu;

static font_t *menus_font;
static uniform_group_t font_uniform;
static image2d_t font_image;


static void initialize_main_menu(void);
static void push_main_menu(gui_textured_vertex_render_list_t *textured_render_list,
                           gui_colored_vertex_render_list_t *colored_render_list, float32_t dt);

static bool detect_if_user_clicked_on_button(main_menu_t::buttons_t button, float32_t cursor_x, float32_t cursor_y);

static void update_open_menu(main_menu_t::buttons_t button, raw_input_t *raw_input, float32_t dt);
static void open_menu(main_menu_t::buttons_t button);
static void initialize_menu_windows(void);


void initialize_menus(void)
{
    current_menu_mode = menu_mode_t::MAIN_MENU;
    
    // Initialize menu font
    menus_font = load_font("menu.font"_hash, "fonts/liberation_mono.fnt", "");
    font_uniform = create_texture_uniform("fonts/liberation_mono.png", &font_image);
    
    initialize_main_menu();
}


void update_menus(raw_input_t *raw_input, element_focus_t focus)
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

    bool hovered_over_button = 0;
    
    for (uint32_t i = 0; i < (uint32_t)main_menu_t::buttons_t::INVALID_MENU_BUTTON; ++i)
    {
        if (detect_if_user_clicked_on_button((main_menu_t::buttons_t)i, cursor_x, cursor_y))
        {
            hovered_over_button = 1;
            
            if (main_menu.hovering_over != i)
            {
                // Start interpolation
                main_menu.background_color_interpolation.in_animation = 1;
                vector4_t background_start = ui32b_color_to_vec4(0x16161636);
                vector4_t background_final = ui32b_color_to_vec4(0x76767636);
                main_menu.background_color_interpolation.current = vector3_t(background_start.r, background_start.g, background_start.b);
                main_menu.background_color_interpolation.prev = main_menu.background_color_interpolation.current;
                main_menu.background_color_interpolation.next = vector3_t(background_final.r, background_final.g, background_final.b);
                main_menu.background_color_interpolation.current_time = 0;
                main_menu.background_color_interpolation.max_time = 0.5f;

                main_menu.icon_color_interpolation.in_animation = 1;
                vector4_t icon_start = ui32b_color_to_vec4(0xFFFFFF36);
                vector4_t icon_final = ui32b_color_to_vec4(0x46464636);
                main_menu.icon_color_interpolation.current = vector3_t(icon_start.r, icon_start.g, icon_start.b);
                main_menu.icon_color_interpolation.prev = main_menu.icon_color_interpolation.current;
                main_menu.icon_color_interpolation.next = vector3_t(icon_final.r, icon_final.g, icon_final.b);
                main_menu.icon_color_interpolation.current_time = 0;
                main_menu.icon_color_interpolation.max_time = 0.5f;
            }
            
            main_menu.hovering_over = (main_menu_t::buttons_t)i;

            if (main_menu.background_color_interpolation.in_animation)
            {
                main_menu.background_color_interpolation.animate(raw_input->dt);
                main_menu.icon_color_interpolation.animate(raw_input->dt);

                vector4_t current_background = vector4_t(main_menu.background_color_interpolation.current.r,
                                                         main_menu.background_color_interpolation.current.g,
                                                         main_menu.background_color_interpolation.current.b,
                                                         ((float32_t)0x36) / 255.0f);

                main_menu.main_menu_buttons_backgrounds[i].color = vec4_color_to_ui32b(current_background);

                vector4_t current_icon = vector4_t(main_menu.icon_color_interpolation.current.r,
                                                   main_menu.icon_color_interpolation.current.g,
                                                   main_menu.icon_color_interpolation.current.b,
                                                   ((float32_t)0x36) / 255.0f);

                main_menu.main_menu_buttons[i].color = vec4_color_to_ui32b(current_icon);
            }
            //main_menu.icon_color_interpolation.animate(raw_input->dt);
        }
        else
        {
            main_menu.main_menu_buttons[i].color = 0xFFFFFF36;
            main_menu.main_menu_buttons_backgrounds[i].color = 0x16161636;
        }
    }

    if (!hovered_over_button)
    {
        main_menu.hovering_over = main_menu_t::buttons_t::INVALID_MENU_BUTTON;
    }

    static bool clicked_previous_frame = 0;
    
    if (raw_input->buttons[button_type_t::MOUSE_LEFT].state >= button_state_t::INSTANT)
    {
        switch(main_menu.hovering_over)
        {
        case main_menu_t::buttons_t::BROWSE_SERVER: case main_menu_t::buttons_t::HOST_SERVER: case main_menu_t::buttons_t::SETTINGS: {
            if (!clicked_previous_frame)
            {
                open_menu(main_menu.hovering_over);
            }
        } break;
            
        case main_menu_t::buttons_t::QUIT: {
            request_quit();
        } break;
        }

        clicked_previous_frame = 1;
    }
    else
    {
        clicked_previous_frame = 0;
    }

    update_open_menu(main_menu.selected_menu, raw_input, raw_input->dt);
}


void push_menus_to_render(gui_textured_vertex_render_list_t *textured_render_list,
                          gui_colored_vertex_render_list_t *colored_render_list,
                          element_focus_t focus,
                          float32_t dt)
{
    if (current_menu_mode == menu_mode_t::MAIN_MENU)
    {
        push_main_menu(textured_render_list, colored_render_list, dt);
    }
}


static void initialize_main_menu(void)
{
    main_menu.main_menu.initialize(CENTER, 2.0f,
                                   ui_vector2_t(0.0f, 0.0f),
                                   ui_vector2_t(0.8f, 0.8f),
                                   nullptr,
                                   0xFF000036,
                                   get_backbuffer_resolution());
    
    main_menu.main_menu_slider_x.in_animation = 0;

    main_menu.main_menu_slider.initialize(RIGHT_UP, 1.75f,
                                          ui_vector2_t(-0.125f, 0.0f),
                                          ui_vector2_t(1.0f, 1.0f),
                                          &main_menu.main_menu,
                                          0x76767636,
                                          get_backbuffer_resolution());


    initialize_menu_windows();
    
    
    main_menu.slider_x_max_size = main_menu.main_menu_slider.gls_current_size.to_fvec2().x;
    main_menu.slider_y_max_size = main_menu.main_menu_slider.gls_current_size.to_fvec2().y;

    main_menu.main_menu_slider.gls_current_size.fx = 0.0f;
    
    main_menu.background_color_interpolation.in_animation = 0;
    main_menu.background_color_interpolation.current = vector3_t(0);
    
    float32_t button_size = 0.25;

    // Relative to top right
    float32_t current_button_y = 0.0f;
    
    {
        main_menu_t::buttons_t button = main_menu_t::buttons_t::BROWSE_SERVER;
    
        main_menu.main_menu_buttons_backgrounds[button].initialize(RIGHT_UP, 1.0f,
                                                                   ui_vector2_t(0.0f, current_button_y),
                                                                   ui_vector2_t(0.25f, 0.25f),
                                                                   &main_menu.main_menu,
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
                                                                   ui_vector2_t(0.0f, current_button_y),
                                                                   ui_vector2_t(0.25f, 0.25f),
                                                                   &main_menu.main_menu,
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
                                                                   ui_vector2_t(0.0f, current_button_y),
                                                                   ui_vector2_t(0.25f, 0.25f),
                                                                   &main_menu.main_menu,
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
                                                                   ui_vector2_t(0.0f, current_button_y),
                                                                   ui_vector2_t(0.25f, 0.25f),
                                                                   &main_menu.main_menu,
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
                           gui_colored_vertex_render_list_t *colored_render_list,
                           float32_t dt)
{
    for (uint32_t i = 0; i < main_menu_t::buttons_t::INVALID_MENU_BUTTON; ++i)
    {
        main_menu_t::buttons_t button = (main_menu_t::buttons_t)i;
        push_box_to_render_with_texture(&main_menu.main_menu_buttons[button], main_menu.widgets[button].uniform);

        push_box_to_render(&main_menu.main_menu_buttons_backgrounds[button]);
    }

    push_box_to_render_reversed(&main_menu.main_menu_slider, vector2_t(main_menu.slider_x_max_size, main_menu.slider_y_max_size) * 2.0f);

    if (main_menu.selected_menu != main_menu_t::buttons_t::INVALID_MENU_BUTTON && !main_menu.main_menu_slider_x.in_animation)
    {
        switch(main_menu.selected_menu)
        {
            
            // Render the selected menu
        case main_menu_t::buttons_t::BROWSE_SERVER: {
            push_box_to_render(&main_menu.browse_menu.ip_address_input_box);

            textured_render_list->mark_section(font_uniform);
            push_input_text_to_render(&main_menu.browse_menu.input_text, &main_menu.browse_menu.ip_address_input_box, get_backbuffer_resolution(), 0xFFFFFFC0, dt);
        } break;
            
        }
    }
}


static bool detect_if_user_clicked_on_widget(ui_box_t *box, float32_t cursor_x, float32_t cursor_y)
{
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


static bool detect_if_user_clicked_on_button(main_menu_t::buttons_t button, float32_t cursor_x, float32_t cursor_y)
{
    return detect_if_user_clicked_on_widget(&main_menu.main_menu_buttons_backgrounds[button], cursor_x, cursor_y);
}


static void open_menu(main_menu_t::buttons_t button)
{
    if (button != main_menu.selected_menu)
    {
        if (main_menu.selected_menu == main_menu_t::buttons_t::INVALID_MENU_BUTTON)
        {
            // Just do transition in
            main_menu.selected_menu = button;

            // Start animation
            main_menu.main_menu_slider_x.in_animation = 1;
            main_menu.main_menu_slider_x.prev = main_menu.main_menu_slider_x.current;
            main_menu.main_menu_slider_x.next = main_menu.slider_x_max_size;
            main_menu.main_menu_slider_x.current_time = 0.0f;
            main_menu.main_menu_slider_x.max_time = 0.3f;
        }
        else
        {
            // Need to do transition out, then back in
            main_menu.selected_menu = button;

            // Start animation
            main_menu.main_menu_slider_x.in_animation = 1;
            main_menu.main_menu_slider_x.prev = main_menu.main_menu_slider_x.current;
            main_menu.main_menu_slider_x.next = 0.0f;
            main_menu.main_menu_slider_x.current_time = 0.0f;
            main_menu.main_menu_slider_x.max_time = 0.3f;

            main_menu.in_out_transition = 1;
        }
    }
    else
    {
        // Just do transition out
        main_menu.selected_menu = main_menu_t::buttons_t::INVALID_MENU_BUTTON;

        // Start animation
        main_menu.main_menu_slider_x.in_animation = 1;
        main_menu.main_menu_slider_x.prev = main_menu.main_menu_slider_x.current;
        main_menu.main_menu_slider_x.next = 0.0f;
        main_menu.main_menu_slider_x.current_time = 0.0f;
        main_menu.main_menu_slider_x.max_time = 0.3f;
    }
}


static void update_open_menu(main_menu_t::buttons_t button, raw_input_t *raw_input, float32_t dt)
{
    main_menu.main_menu_slider_x.animate(dt);

    main_menu.main_menu_slider.gls_current_size.fx = main_menu.main_menu_slider_x.current;

    if (main_menu.in_out_transition && !main_menu.main_menu_slider_x.in_animation)
    {
        main_menu.in_out_transition = 0;
        
        main_menu.main_menu_slider_x.in_animation = 1;
        main_menu.main_menu_slider_x.prev = main_menu.main_menu_slider_x.current;
        main_menu.main_menu_slider_x.next = main_menu.slider_x_max_size;
        main_menu.main_menu_slider_x.current_time = 0.0f;
        main_menu.main_menu_slider_x.max_time = 0.3f;
    }

    if (main_menu.selected_menu != main_menu_t::buttons_t::INVALID_MENU_BUTTON && !main_menu.main_menu_slider_x.in_animation)
    {
        switch(main_menu.selected_menu)
        {
            
            // Render the selected menu
        case main_menu_t::buttons_t::BROWSE_SERVER: {

            main_menu.browse_menu.input_text.input(raw_input);

            if (raw_input->buttons[button_type_t::ENTER].state != button_state_t::NOT_DOWN)
            {
                // Join server
                main_menu.browse_menu.input_text.text.null_terminate();
                
                const char *ip_address = main_menu.browse_menu.input_text.text.characters;
                
                join_server(ip_address, "");
            }
            
        } break;
            
        }
    }
}


static void initialize_menu_windows(void)
{
    main_menu.browse_menu.ip_address_input_box.initialize(LEFT_DOWN, 15.0f,
                                                          ui_vector2_t(0.05f, 0.05f),
                                                          ui_vector2_t(0.6f, 0.6f),
                                                          &main_menu.main_menu_slider,
                                                          0x16161646,
                                                          get_backbuffer_resolution());

    main_menu.browse_menu.input_text.text.initialize(&main_menu.browse_menu.ip_address_input_box,
                                                     menus_font,
                                                     ui_text_t::font_stream_box_relative_to_t::BOTTOM,
                                                     0.8f, 1.0f,
                                                     55, 1.8f);

    main_menu.browse_menu.input_text.text_color = 0xFFFFFFC0;
}
