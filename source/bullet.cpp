#include "bullet.hpp"

void bullet_t::initialize(bullet_create_info_t *info) {
    static vector4_t colors[player_color_t::INVALID_COLOR] = { vector4_t(0.0f, 0.0f, 0.7f, 1.0f),
                                                               vector4_t(0.7f, 0.0f, 0.0f, 1.0f),
                                                               vector4_t(0.4f, 0.4f, 0.4f, 1.0f),
                                                               vector4_t(0.1f, 0.1f, 0.1f, 1.0f),
                                                               vector4_t(0.0f, 0.7f, 0.0f, 1.0f),
                                                               vector4_t(246.0f, 177.0f, 38.0f, 256.0f) / 256.0f };
    
    ws_position = info->ws_position;
    ws_direction = info->ws_direction;
    size = info->ws_size;
    ws_rotation = info->ws_rotation;
    rendering.push_k.color = colors[info->color];
    rendering.push_k.roughness = 0.8f;
    rendering.push_k.metalness = 0.6f;
    bullet_index = info->bullet_index;
}
