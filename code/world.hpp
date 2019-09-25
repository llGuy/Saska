/* world.hpp */

#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include "graphics.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

struct morphable_terrain_t
{
    uint32_t terrain_base_id;
    
    // TODO: Possible fix to hard setting position at collision whilst morphing
    persist_var constexpr uint32_t MAX_MORPHED_POINTS = 20;
    ivector2_t morphed_points[MAX_MORPHED_POINTS] = {};
    uint32_t current_morphed_points_count = 0;
    
    // Gravity constant
    float32_t k_g;
    
    bool is_modified = false;
    
    ivector2_t xz_dim;
    // ---- up vector of the terrain ----
    vector3_t ws_n;
    float32_t *heights;

    vector3_t size;
    vector3_t ws_p;
    quaternion_t gs_r;

    uint32_t offset_into_heights_gpu_buffer;
    // ---- later on this will be a pointer (index into g_gpu_buffer_manager)
    gpu_buffer_t heights_gpu_buffer;

    mesh_t mesh;
    VkBuffer vbos[2];

    matrix4_t inverse_transform;
    matrix4_t inverse_rotation;
    struct push_k_t
    {
        matrix4_t transform;
        vector3_t color;
    } push_k;
};

struct terrain_create_staging_t
{
    uint32_t dimensions;
    float32_t size;
    vector3_t ws_p;
    vector3_t rotation;
    vector3_t color;
};

struct terrain_triangle_t
{
    bool triangle_exists;
    float32_t ts_height;
    vector3_t ws_exact_pointed_at;
    vector3_t ws_triangle_position[3];
    // Used for morphing function
    ivector2_t offsets[4];
    // Indices
    uint32_t idx[4 /* if need the entire square */];

    // Extra information for sliding collision detection algorithm
    vector3_t ts_collision_point;
};

struct terrain_base_info_t
{
    uint32_t width, depth;
    gpu_buffer_t mesh_xz_values;
    gpu_buffer_t idx_buffer;
    model_t model_info;

    uint32_t base_id;
};

struct morphable_terrains_t
{
    // ---- X and Z values stored as vec2 (binding 0) ----
    uint32_t base_count;
    terrain_base_info_t terrain_bases[10];
    hash_table_inline_t<int32_t, 10, 3, 3> terrain_base_table;

    static constexpr uint32_t MAX_TERRAINS = 10;
    morphable_terrain_t terrains[MAX_TERRAINS];
    uint32_t terrain_count {0};

    // For lua stuff when spawning terrains
    terrain_create_staging_t create_stagings[10];
    uint32_t create_count = 0;
    
    pipeline_handle_t terrain_ppln;
    pipeline_handle_t terrain_shadow_ppln;

    struct
    {
        pipeline_handle_t ppln;
        terrain_triangle_t triangle;
        // will not be a pointer in the future
        morphable_terrain_t *t;
    } terrain_pointer;

    bool dbg_is_rendering_sphere_collision_triangles = 1;
    bool dbg_is_rendering_edge_collision_line = 1;
    struct
    {
        morphable_terrain_t *edge_container;
        vector3_t ts_a, ts_b;
    } dbg_edge_data;
};

using entity_handle_t = int32_t;

struct hitbox_t
{
    // Relative to the size of the entity
    // These are the values of when the entity size = 1
    float32_t x_max, x_min;
    float32_t y_max, y_min;
    float32_t z_max, z_min;
};

// Gravity acceleration on earth = 9.81 m/s^2
struct physics_component_t
{
    // Sum of mass of all particles
    float32_t mass = 1.0f; // KG
    // Sum ( all points of mass (vector quantity) * all masses of each point of mass ) / total body mass
    vector3_t center_of_gravity;
    // Depending on the shape of the body (see formula for rectangle if using hitbox, and for sphere if using bounding sphere)
    vector3_t moment_of_inertia;

    float32_t coefficient_of_restitution = 0.0f;
    
    vector3_t acceleration {0.0f};
    vector3_t ws_velocity {0.0f};
    vector3_t displacement {0.0f};

    enum is_resting_t { NOT_RESTING = 0, JUST_COLLIDED = 1, RESTING = 2 } is_resting {NOT_RESTING};

    float32_t momentum = 0.0f;
    
    // F = ma
    vector3_t total_force_on_body;
    // G = mv
    //vector3_t momentum;

    uint32_t entity_index;
    
    vector3_t gravity_accumulation = {};
    vector3_t friction_accumulation = {};
    vector3_t slide_accumulation = {};
    
    bool enabled;
    hitbox_t hitbox;
    vector3_t surface_normal;
    vector3_t surface_position;

    vector3_t force;

    // other forces (friction...)
};

struct camera_component_t
{
    uint32_t entity_index;
    
    // Can be set to -1, in that case, there is no camera bound
    camera_handle_t camera{-1};

    // Maybe some other variables to do with 3rd person / first person configs ...

    // Variable allows for smooth animation between up vectors when switching terrains
    bool in_animation = false;
    quaternion_t current_rotation;

    bool is_third_person;
    float32_t distance_from_player = 40.0f;
};

struct input_component_t
{
    uint32_t entity_index;

    enum movement_flags_t { FORWARD, LEFT, BACK, RIGHT, DOWN, RUN };
    uint8_t movement_flags = 0;

    /*float32_t horizontal_angle = 0.0f;
    float32_t vertical_angle = 0.0f;*/
};

struct animation_component_t
{
    uint32_t entity_index;
    // Rendering the animated entity
    animated_instance_t animation_instance;
    animation_cycles_t *cycles;
};

struct rendering_component_t
{
    uint32_t entity_index;
    
    // push constant stuff for the graphics pipeline
    struct
    {
	// in world space
	matrix4_t ws_t{1.0f};
	vector4_t color;

        float32_t roughness;
        float32_t metalness;
    } push_k;

    bool enabled = true;
};

struct entity_body_t
{
    float32_t weight = 1.0f;
    hitbox_t hitbox;
};

struct entity_t
{
    entity_t(void) = default;
    
    constant_string_t id {""_hash};
    // position, direction, velocity
    // in above entity group space
    vector3_t ws_p{0.0f}, ws_d{0.0f}, ws_v{0.0f}, ws_input_v{0.0f};
    vector3_t ws_acceleration {0.0f};
    quaternion_t ws_r{0.0f, 0.0f, 0.0f, 0.0f};
    vector3_t size{1.0f};

    // For now is a pointer - is not a component because all entities will have one
    // This is the last terrain that the player was on / is still on
    // Is used for collision detection and also the camera view matrix (up vector...)
    struct morphable_terrain_t *on_t = nullptr;
    bool is_on_terrain = false;
    vector3_t surface_normal;
    vector3_t surface_position;

    // Has effect on animations
    bool is_in_air = 0;
    bool is_sliding_not_rolling_mode = 0;

    static constexpr float32_t SWITCH_TERRAIN_ANIMATION_TIME = 0.6f;
    bool switch_terrain_animation_mode = false;
    quaternion_t previous_terrain_rot;
    quaternion_t current_rot;
    quaternion_t current_physical_rotation;
    float32_t animation_time = 0.0f;

    bool toggled_rolling_previous_frame = 0;
    bool32_t rolling_mode;
    float32_t rolling_rotation_angle = 0.0f;
    matrix4_t rolling_rotation = matrix4_t(1.0f);

    bool is_sitting = 0;

    //    struct entity_body_t body;
    // For animated rendering component
    enum animated_state_t { WALK, IDLE, RUN, HOVER, SLIDING_NOT_ROLLING_MODE, SITTING, JUMP } animated_state = animated_state_t::IDLE;
    
    struct components_t
    {

        int32_t camera_component;
        int32_t physics_component;
        int32_t input_component = -1;
        int32_t rendering_component;
        int32_t animation_component;
        
    } components;
    
    entity_handle_t index;
};

struct dbg_entities_t
{
    bool hit_box_display = false;
    entity_t *render_sliding_vector_entity = nullptr;
};

struct entities_t
{
    dbg_entities_t dbg;

    static constexpr uint32_t MAX_ENTITIES = 30;
    
    int32_t entity_count = {};
    entity_t entity_list[MAX_ENTITIES] = {};

    // All possible components: 
    int32_t physics_component_count = {};
    struct physics_component_t physics_components[MAX_ENTITIES] = {};

    int32_t camera_component_count = {};
    struct camera_component_t camera_components[MAX_ENTITIES] = {};

    int32_t input_component_count = {};
    struct input_component_t input_components[MAX_ENTITIES] = {};

    int32_t rendering_component_count = {};
    struct rendering_component_t rendering_components[MAX_ENTITIES] = {};

    int32_t animation_component_count = {};
    struct animation_component_t animation_components[MAX_ENTITIES] = {};

    struct hash_table_inline_t<entity_handle_t, 30, 5, 5> name_map{"map.entities"};

    pipeline_handle_t entity_ppln;
    pipeline_handle_t entity_shadow_ppln;

    pipeline_handle_t rolling_entity_ppln;
    pipeline_handle_t rolling_entity_shadow_ppln;
    
    pipeline_handle_t dbg_hitbox_ppln;

    mesh_t rolling_entity_mesh;
    model_t rolling_entity_model;

    mesh_t entity_mesh;
    skeleton_t entity_mesh_skeleton;
    animation_cycles_t entity_mesh_cycles;
    uniform_layout_t animation_ubo_layout;
    model_t entity_model;

    // For now:
    uint32_t main_entity;
    // have some sort of stack of REMOVED entities
};

struct server_terrain_base_state_t
{
    // Dimensions
    uint8_t x, z;
};

struct server_terrain_state_t
{
    // Gravity constant
    float32_t k_g;
    vector3_t size;
    vector3_t ws_position;
    quaternion_t quat;
    
    // Float array
    uint8_t terrain_base_id;
    float32_t *heights = nullptr;
};

// Only to send at the beginning of game
struct server_terrains_state_t
{
    uint8_t terrain_base_count;
    server_terrain_base_state_t *terrain_bases = nullptr;
    
    uint8_t terrain_count;
    // Terrain array
    server_terrain_state_t *terrains = nullptr;
};

struct network_world_state_t
{
    server_terrains_state_t terrains;
};

struct world_t
{
    struct entities_t entities;
    struct morphable_terrains_t terrains;

    static constexpr uint32_t MAX_MATERIALS = 10;
    struct gpu_material_submission_queue_t material_submission_queues[MAX_MATERIALS];

    network_world_state_t network_world_state;
};

network_world_state_t *get_network_world_state(void);
void update_network_world_state(void);

void clean_up_world_data(void);

void make_world_data(void);

// Initializes all of the rendering data, and stuff whereas make_world_data just initializes entities, terrains, etc..
void initialize_world(input_state_t *input_state, VkCommandPool *cmdpool, enum application_type_t type);

void update_world(input_state_t *input_state, float32_t dt, uint32_t image_index,
                  uint32_t current_frame, gpu_command_queue_t *queue, enum application_type_t type);

void handle_input_debug(input_state_t *input_state, float32_t dt);

void destroy_world(void);

void initialize_world_translation_unit(struct game_memory_t *memory);
