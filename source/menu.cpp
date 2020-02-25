#include "variables.hpp"
#include "ui.hpp"
#include "menu.hpp"
#include "core.hpp"
#include "deferred_renderer.hpp"

#include "event_system.hpp"
#include "allocators.hpp"


// Main menu

struct widget_t {
    image2d_t image;
    uniform_group_t uniform;
};

struct text_input_t {
    bool typing_in_input = 0;
    ui_box_t input_box;
    ui_input_text_t input_text;
};

struct main_menu_t {
    enum buttons_t { BROWSE_SERVER, BUILD_MAP, SETTINGS, QUIT, INVALID_MENU_BUTTON };

    const char *button_names[5] = { "BROWSE_SERVER", "BUILD_MAP", "SETTINGS", "QUIT", "INVALID" };
    
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
    struct {
        text_input_t input;

        bool joining = 0;
    } browse_menu;

    struct {
        text_input_t input;
    } build_menu;


    struct {
        ui_box_t background;
        ui_input_text_t input_text;
        bool prompting_user_name;

        smooth_linear_interpolation_t<float32_t> alpha;

        bool entered = 0;
    } user_name_prompt;
};


enum menu_mode_t { NONE, MAIN_MENU, INVALID_MENU_MODE };

static menu_mode_t current_menu_mode;

static main_menu_t main_menu;

static font_t *menus_font;
static uniform_group_t font_uniform;
static image2d_t font_image;


static void s_initialize_main_menu(void);
static void s_push_main_menu(gui_textured_vertex_render_list_t *textured_render_list,
                           gui_colored_vertex_render_list_t *colored_render_list, float32_t dt);

static bool s_detect_if_user_clicked_on_button(main_menu_t::buttons_t button, float32_t cursor_x, float32_t cursor_y);

static void s_update_open_menu(main_menu_t::buttons_t button, raw_input_t *raw_input, float32_t dt, event_dispatcher_t *dispatcher);
static void s_open_menu(main_menu_t::buttons_t button);
static void s_initialize_menu_windows(void);


void initialize_menus(void) {
    current_menu_mode = menu_mode_t::MAIN_MENU;
    
    // Initialize menu font
    menus_font = load_font("menu.font"_hash, "fonts/liberation_mono.fnt", "");
    font_uniform = create_texture_uniform("fonts/liberation_mono.png", &font_image);
    
    s_initialize_main_menu();
}


void prompt_user_for_name(void) {
    main_menu.user_name_prompt.prompting_user_name = 1;
    main_menu.user_name_prompt.background.initialize(CENTER, 7.5f,
                                                     ui_vector2_t(0.0f, 0.0f),
                                                     ui_vector2_t(0.3f, 0.3f),
                                                     nullptr,
                                                     0x46464646,
                                                     backbuffer_resolution());

    main_menu.user_name_prompt.input_text.text.initialize(&main_menu.user_name_prompt.background,
                                                          menus_font,
                                                          ui_text_t::font_stream_box_relative_to_t::BOTTOM,
                                                          0.8f, 0.9f,
                                                          25, 1.8f);

    main_menu.user_name_prompt.alpha.in_animation = 1;
    main_menu.user_name_prompt.alpha.prev = 0.0f;
    main_menu.user_name_prompt.alpha.current = main_menu.user_name_prompt.alpha.prev;
    main_menu.user_name_prompt.alpha.next = (float32_t)0x46 / (float32_t)255.0f;
    main_menu.user_name_prompt.alpha.current_time = 0;
    main_menu.user_name_prompt.alpha.max_time = 2.5f;
}


void update_menus(raw_input_t *raw_input, element_focus_t focus, event_dispatcher_t *dispatcher) {
    if (main_menu.user_name_prompt.prompting_user_name) {
        main_menu.user_name_prompt.alpha.animate(raw_input->dt);
        uint32_t alpha = (uint32_t)(main_menu.user_name_prompt.alpha.current * 255.0f);
        main_menu.user_name_prompt.background.color &= 0xFFFFFF00;
        main_menu.user_name_prompt.background.color |= alpha;

        if (raw_input->buttons[button_type_t::ENTER].state != button_state_t::NOT_DOWN) {
            main_menu.user_name_prompt.entered = 1;

            char *buffer = (char *)allocate_free_list(sizeof(char) * main_menu.user_name_prompt.input_text.text.char_count + 1);
            memcpy(buffer, main_menu.user_name_prompt.input_text.text.characters, sizeof(char) * main_menu.user_name_prompt.input_text.text.char_count + 1);

            variables_get_user_name() = buffer;
            
            main_menu.user_name_prompt.alpha.in_animation = 1;
            main_menu.user_name_prompt.alpha.prev = (float32_t)0x46 / (float32_t)255.0f;
            main_menu.user_name_prompt.alpha.current = main_menu.user_name_prompt.alpha.prev;
            main_menu.user_name_prompt.alpha.next = 0.0f;
            main_menu.user_name_prompt.alpha.current_time = 0;
            main_menu.user_name_prompt.alpha.max_time = 2.5f;
        }
        
        main_menu.user_name_prompt.input_text.input(raw_input);
    }
    else {
        // Convert screen cursor positions to ndc coordinates
        // Need to take into account the scissor

        VkExtent2D swapchain_extent = get_swapchain_extent();

        resolution_t backbuffer_res = backbuffer_resolution();
    
        float32_t backbuffer_asp = (float32_t)backbuffer_res.width / (float32_t)backbuffer_res.height;
        float32_t swapchain_asp = (float32_t)get_swapchain_extent().width / (float32_t)get_swapchain_extent().height;

        uint32_t rect2D_width, rect2D_height, rect2Dx, rect2Dy;
    
        if (backbuffer_asp >= swapchain_asp) {
            rect2D_width = swapchain_extent.width;
            rect2D_height = (uint32_t)((float32_t)swapchain_extent.width / backbuffer_asp);
            rect2Dx = 0;
            rect2Dy = (swapchain_extent.height - rect2D_height) / 2;
        }

        if (backbuffer_asp < swapchain_asp) {
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
    
        for (uint32_t i = 0; i < (uint32_t)main_menu_t::buttons_t::INVALID_MENU_BUTTON; ++i) {
            if (s_detect_if_user_clicked_on_button((main_menu_t::buttons_t)i, cursor_x, cursor_y)) {
                hovered_over_button = 1;
            
                if (main_menu.hovering_over != i) {
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

                if (main_menu.background_color_interpolation.in_animation) {
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
            else {
                main_menu.main_menu_buttons[i].color = 0xFFFFFF36;
                main_menu.main_menu_buttons_backgrounds[i].color = 0x16161636;
            }
        }

        if (!hovered_over_button) {
            main_menu.hovering_over = main_menu_t::buttons_t::INVALID_MENU_BUTTON;
        }

        static bool clicked_previous_frame = 0;
    
        if (raw_input->buttons[button_type_t::MOUSE_LEFT].state >= button_state_t::INSTANT) {
            switch(main_menu.hovering_over) {
            case main_menu_t::buttons_t::BROWSE_SERVER: case main_menu_t::buttons_t::BUILD_MAP: case main_menu_t::buttons_t::SETTINGS: {
                if (!clicked_previous_frame) {
                    s_open_menu(main_menu.hovering_over);
                }
            } break;
            
            case main_menu_t::buttons_t::QUIT: {
                request_quit();
            } break;
            }

            clicked_previous_frame = 1;
        }
        else {
            clicked_previous_frame = 0;
        }

        s_update_open_menu(main_menu.selected_menu, raw_input, raw_input->dt, dispatcher);
    }
}


void push_menus_to_render(gui_textured_vertex_render_list_t *textured_render_list,
                          gui_colored_vertex_render_list_t *colored_render_list,
                          element_focus_t focus,
                          float32_t dt) {
    if (current_menu_mode == menu_mode_t::MAIN_MENU) {
        if (main_menu.user_name_prompt.prompting_user_name) {
            if (main_menu.user_name_prompt.entered && !main_menu.user_name_prompt.alpha.in_animation) {
                main_menu.user_name_prompt.prompting_user_name = 0;
            }
            
            push_box_to_render(&main_menu.user_name_prompt.background);

            uint32_t alpha = (uint32_t)(main_menu.user_name_prompt.alpha.current * 255.0f);
            main_menu.user_name_prompt.input_text.text_color = 0xFFFFFF00;
            main_menu.user_name_prompt.input_text.text_color &= 0xFFFFFF00;
            main_menu.user_name_prompt.input_text.text_color |= alpha;

            if (main_menu.user_name_prompt.entered) {
                for (uint32_t i = 0; i < main_menu.user_name_prompt.input_text.text.char_count; ++i) {
                    main_menu.user_name_prompt.input_text.text.colors[i] = 0;
                }
            }
            
            textured_render_list->mark_section(font_uniform);

            if (!main_menu.user_name_prompt.entered) {
                push_input_text_to_render(&main_menu.user_name_prompt.input_text,
                                          &main_menu.user_name_prompt.background,
                                          backbuffer_resolution(),
                                          main_menu.user_name_prompt.input_text.text_color,
                                          dt,
                                          1);
            }
        }
        else {
            s_push_main_menu(textured_render_list, colored_render_list, dt);
        }
    }
}


static void s_main_menu_button_init(main_menu_t::buttons_t button, const char *image_path, float32_t current_button_y, float32_t button_size) {
    main_menu.main_menu_buttons_backgrounds[button].initialize(RIGHT_UP, 1.0f,
                                                               ui_vector2_t(0.0f, current_button_y),
                                                               ui_vector2_t(button_size, button_size),
                                                               &main_menu.main_menu,
                                                               (uint32_t)0x1616161636,
                                                               backbuffer_resolution());
    
    main_menu.main_menu_buttons[button].initialize(CENTER, 1.0f,
                                                   ui_vector2_t(0.0f, 0.0f),
                                                   ui_vector2_t(0.8f, 0.8f),
                                                   &main_menu.main_menu_buttons_backgrounds[button],
                                                   (uint32_t)0xFFFFFF36,
                                                   backbuffer_resolution());

    main_menu.widgets[button].uniform = create_texture_uniform(image_path, &main_menu.widgets[button].image);

    
}


static void s_initialize_main_menu(void) {
    main_menu.main_menu.initialize(CENTER, 2.0f,
                                   ui_vector2_t(0.0f, 0.0f),
                                   ui_vector2_t(0.8f, 0.8f),
                                   nullptr,
                                   0xFF000036,
                                   backbuffer_resolution());
    
    main_menu.main_menu_slider_x.in_animation = 0;

    main_menu.main_menu_slider.initialize(RIGHT_UP, 1.75f,
                                          ui_vector2_t(-0.125f, 0.0f),
                                          ui_vector2_t(1.0f, 1.0f),
                                          &main_menu.main_menu,
                                          0x76767636,
                                          backbuffer_resolution());


    s_initialize_menu_windows();
    
    
    main_menu.slider_x_max_size = main_menu.main_menu_slider.gls_current_size.to_fvec2().x;
    main_menu.slider_y_max_size = main_menu.main_menu_slider.gls_current_size.to_fvec2().y;

    main_menu.main_menu_slider.gls_current_size.fx = 0.0f;
    
    main_menu.background_color_interpolation.in_animation = 0;
    main_menu.background_color_interpolation.current = vector3_t(0);
    
    float32_t button_size = 0.25f;

    // Relative to top right
    float32_t current_button_y = 0.0f;
    
    s_main_menu_button_init(main_menu_t::buttons_t::BROWSE_SERVER, "textures/gui/play_icon.png", current_button_y, button_size);
    current_button_y -= button_size;

    s_main_menu_button_init(main_menu_t::buttons_t::BUILD_MAP, "textures/gui/build_icon.png", current_button_y, button_size);
    current_button_y -= button_size;

    s_main_menu_button_init(main_menu_t::buttons_t::SETTINGS, "textures/gui/settings_icon.png", current_button_y, button_size);
    current_button_y -= button_size;

    s_main_menu_button_init(main_menu_t::buttons_t::QUIT, "textures/gui/quit_icon.png", current_button_y, button_size);
    current_button_y -= button_size;
}


static void s_push_main_menu(gui_textured_vertex_render_list_t *textured_render_list,
                             gui_colored_vertex_render_list_t *colored_render_list,
                             float32_t dt) {
    for (uint32_t i = 0; i < main_menu_t::buttons_t::INVALID_MENU_BUTTON; ++i) {
        main_menu_t::buttons_t button = (main_menu_t::buttons_t)i;
        push_box_to_render_with_texture(&main_menu.main_menu_buttons[button], main_menu.widgets[button].uniform);

        push_box_to_render(&main_menu.main_menu_buttons_backgrounds[button]);
    }

    push_box_to_render_reversed(&main_menu.main_menu_slider, vector2_t(main_menu.slider_x_max_size, main_menu.slider_y_max_size) * 2.0f);

    if (main_menu.selected_menu != main_menu_t::buttons_t::INVALID_MENU_BUTTON && !main_menu.main_menu_slider_x.in_animation) {
        switch(main_menu.selected_menu) {
            
            // Render the selected menu
        case main_menu_t::buttons_t::BROWSE_SERVER: {
            push_box_to_render(&main_menu.browse_menu.input.input_box);

            textured_render_list->mark_section(font_uniform);
            push_input_text_to_render(&main_menu.browse_menu.input.input_text, &main_menu.browse_menu.input.input_box, backbuffer_resolution(), 0xFFFFFFFF, dt, 1);
        } break;

        case main_menu_t::buttons_t::BUILD_MAP: {
            push_box_to_render(&main_menu.build_menu.input.input_box);

            textured_render_list->mark_section(font_uniform);
            push_input_text_to_render(&main_menu.build_menu.input.input_text, &main_menu.build_menu.input.input_box, backbuffer_resolution(), 0xFFFFFFFF, dt, 1);
        } break;
            
        }
    }
}


static bool detect_if_user_clicked_on_widget(ui_box_t *box, float32_t cursor_x, float32_t cursor_y) {
    vector2_t normalized_base_position = convert_glsl_to_normalized(box->gls_position.to_fvec2());
    vector2_t normalized_size = box->gls_current_size.to_fvec2() * 2.0f;

    float32_t x_min = normalized_base_position.x,
        x_max = normalized_base_position.x + normalized_size.x,
        y_min = normalized_base_position.y,
        y_max = normalized_base_position.y + normalized_size.y;

    if (x_min < cursor_x && x_max > cursor_x
        && y_min < cursor_y && y_max > cursor_y) {
        return(true);
    }
    else {
        return(false);
    }
}


static bool s_detect_if_user_clicked_on_button(main_menu_t::buttons_t button, float32_t cursor_x, float32_t cursor_y) {
    return detect_if_user_clicked_on_widget(&main_menu.main_menu_buttons_backgrounds[button], cursor_x, cursor_y);
}


static void s_open_menu(main_menu_t::buttons_t button) {
    if (button != main_menu.selected_menu) {
        if (main_menu.selected_menu == main_menu_t::buttons_t::INVALID_MENU_BUTTON) {
            // Just do transition in
            main_menu.selected_menu = button;

            // Start animation
            main_menu.main_menu_slider_x.in_animation = 1;
            main_menu.main_menu_slider_x.prev = main_menu.main_menu_slider_x.current;
            main_menu.main_menu_slider_x.next = main_menu.slider_x_max_size;
            main_menu.main_menu_slider_x.current_time = 0.0f;
            main_menu.main_menu_slider_x.max_time = 0.3f;
        }
        else {
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
    else {
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


static void s_update_open_menu(main_menu_t::buttons_t button, raw_input_t *raw_input, float32_t dt, event_dispatcher_t *dispatcher) {
    main_menu.main_menu_slider_x.animate(dt);

    main_menu.main_menu_slider.gls_current_size.fx = main_menu.main_menu_slider_x.current;

    if (main_menu.in_out_transition && !main_menu.main_menu_slider_x.in_animation) {
        main_menu.in_out_transition = 0;
        
        main_menu.main_menu_slider_x.in_animation = 1;
        main_menu.main_menu_slider_x.prev = main_menu.main_menu_slider_x.current;
        main_menu.main_menu_slider_x.next = main_menu.slider_x_max_size;
        main_menu.main_menu_slider_x.current_time = 0.0f;
        main_menu.main_menu_slider_x.max_time = 0.3f;
    }

    if (main_menu.selected_menu != main_menu_t::buttons_t::INVALID_MENU_BUTTON && !main_menu.main_menu_slider_x.in_animation) {
        switch(main_menu.selected_menu) {
            
            // Render the selected menu
        case main_menu_t::buttons_t::BROWSE_SERVER: {

            main_menu.browse_menu.input.input_text.input(raw_input);

            if (raw_input->buttons[button_type_t::ENTER].state != button_state_t::NOT_DOWN) {
                // Launch an event

                // Join server
                main_menu.browse_menu.joining = 1;
                
                main_menu.browse_menu.input.input_text.text.null_terminate();
                
                const char *ip_address = main_menu.browse_menu.input.input_text.text.characters;
                
                join_server(ip_address, variables_get_user_name());
            }
            
        } break;

        case main_menu_t::buttons_t::BUILD_MAP: {

            main_menu.build_menu.input.input_text.input(raw_input);

            if (raw_input->buttons[button_type_t::ENTER].state != button_state_t::NOT_DOWN) {
                // Launch an event
                main_menu.build_menu.input.input_text.text.null_terminate();

                auto *data = LN_MALLOC(event_data_launch_map_editor_t, 1);
                uint32_t str_len = (uint32_t)strlen(main_menu.build_menu.input.input_text.text.characters);
                data->map_name = FL_MALLOC(char, str_len * 2);
                for (int i = 0; i < str_len + 1; ++i) data->map_name[i] = main_menu.build_menu.input.input_text.text.characters[i];
                //memcpy_s(data->map_name, str_len + 1, main_menu.build_menu.input.input_text.text.characters, str_len + 1);
                dispatcher->submit_event(event_type_t::LAUNCH_MAP_EDITOR, data);
            }

        } break;
            
        }
    }
}


static void s_initialize_menu_windows(void) {
    main_menu.browse_menu.input.input_box.initialize(LEFT_DOWN, 15.0f, ui_vector2_t(0.05f, 0.05f), ui_vector2_t(0.6f, 0.6f),
        &main_menu.main_menu_slider, 0x16161646, backbuffer_resolution());

    main_menu.browse_menu.input.input_text.text.initialize(&main_menu.browse_menu.input.input_box, menus_font,
        ui_text_t::font_stream_box_relative_to_t::BOTTOM, 0.8f, 1.0f, 55, 1.8f);

    main_menu.browse_menu.input.input_text.text_color = 0xFFFFFFFF;

    main_menu.build_menu.input.input_box.initialize(LEFT_DOWN, 15.0f, ui_vector2_t(0.05f, 0.05f), ui_vector2_t(0.6f, 0.6f),
        &main_menu.main_menu_slider, 0x16161646, backbuffer_resolution());

    main_menu.build_menu.input.input_text.text.initialize(&main_menu.build_menu.input.input_box, menus_font,
        ui_text_t::font_stream_box_relative_to_t::BOTTOM, 0.8f, 1.0f, 55, 1.8f);

    main_menu.build_menu.input.input_text.text_color = 0xFFFFFFFF;
}
