#include "graphics.hpp"
#include "gui_box.hpp"
#include "hud.hpp"
#include "math.hpp"

#include "deferred_renderer.hpp"


struct crosshair_t {
    image2d_t crosshair_image;
    uniform_group_t crosshair_group;

    uint32_t selected_crosshair;

    ui_box_t crosshair_box;

    vector2_t uvs[4];

    uniform_group_t png_image_uniform;
    
    // Crosshair image is 8x8
    void get_uvs_for_crosshair(void) {
        vector2_t *list = uvs;
        
        // Starting coordinate
        vector2_t starting_coord = convert_1d_to_2d_coord(selected_crosshair, 8) / 8.0f;

        float32_t unit = (1.0f / 8.0f);
        
        list[0] = vector2_t(starting_coord.x, starting_coord.y + unit);
        list[1] = vector2_t(starting_coord.x, starting_coord.y);
        list[2] = vector2_t(starting_coord.x + unit, starting_coord.y + unit);
        list[3] = vector2_t(starting_coord.x + unit, starting_coord.y);
    }
};


// HUD elements
static crosshair_t crosshair;


static void s_initialize_crosshair(void);
static void s_push_crosshair_for_render(gui_textured_vertex_render_list_t *render_list);



void initialize_hud(void) {
    s_initialize_crosshair();
}


void push_hud_to_render(gui_textured_vertex_render_list_t *render_list, element_focus_t focus) {
    if (focus == element_focus_t::WORLD_3D_ELEMENT_FOCUS) {
        s_push_crosshair_for_render(render_list);
    }
}


static void s_initialize_crosshair(void) {
    // Just a dot
    crosshair.selected_crosshair = 1;

    crosshair.get_uvs_for_crosshair();
    
    file_handle_t crosshair_texture_file = create_file("textures/gui/crosshair.png", file_type_flags_t::IMAGE | file_type_flags_t::ASSET);
    external_image_data_t image_data = read_image(crosshair_texture_file);

    make_texture(&crosshair.crosshair_image,
                 image_data.width,
                 image_data.height,
                 VK_FORMAT_R8G8B8A8_UNORM,
                 1,
                 1,
                 2,
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_FILTER_NEAREST);
    transition_image_layout(&crosshair.crosshair_image.image,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            get_global_command_pool());
    invoke_staging_buffer_for_device_local_image({(uint32_t)(4 * image_data.width * image_data.height), image_data.pixels},
                                                 get_global_command_pool(),
                                                 &crosshair.crosshair_image,
                                                 (uint32_t)image_data.width,
                                                 (uint32_t)image_data.height);
    transition_image_layout(&crosshair.crosshair_image.image,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            get_global_command_pool());

    free_external_image_data(&image_data);

    uniform_layout_t *tx_layout = g_uniform_layout_manager->get(g_uniform_layout_manager->get_handle("uniform_layout.tx_ui_quad"_hash));
    crosshair.png_image_uniform = make_uniform_group(tx_layout, g_uniform_pool);
    update_uniform_group(&crosshair.png_image_uniform,
                         update_binding_t{ TEXTURE, &crosshair.crosshair_image, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });

    crosshair.crosshair_box.initialize(CENTER, 1.0f, ui_vector2_t(-0.02f, -0.02f), ui_vector2_t(0.04f, 0.04f), nullptr, 0xffffffff, backbuffer_resolution());
}


static void s_push_crosshair_for_render(gui_textured_vertex_render_list_t *render_list) {
    render_list->mark_section(crosshair.png_image_uniform);
    
    vector2_t normalized_base_position = convert_glsl_to_normalized(crosshair.crosshair_box.gls_position.to_fvec2());
    vector2_t normalized_size = crosshair.crosshair_box.gls_current_size.to_fvec2() * 2.0f;

    normalized_base_position = vector2_t(0.0f) - normalized_size / 2.0f;

    render_list->push_vertex({normalized_base_position, crosshair.uvs[0], 0xffffff88});
    render_list->push_vertex({normalized_base_position + vector2_t(0.0f, normalized_size.y), crosshair.uvs[1], 0xffffff88});
    render_list->push_vertex({normalized_base_position + vector2_t(normalized_size.x, 0.0f), crosshair.uvs[2], 0xffffff88});
    render_list->push_vertex({normalized_base_position + vector2_t(0.0f, normalized_size.y), crosshair.uvs[1], 0xffffff88});
    render_list->push_vertex({normalized_base_position + vector2_t(normalized_size.x, 0.0f), crosshair.uvs[2], 0xffffff88});
    render_list->push_vertex({normalized_base_position + normalized_size, crosshair.uvs[3], 0xffffff88});
}
