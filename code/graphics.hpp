#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include <glm/glm.hpp>
#include "utils.hpp"
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>

enum invalid_t {INVALID_HANDLE = -1};

// TODO: Possibly get rid of this system
template <typename T, uint32_t Max = 40> struct object_manager_t
{
    using Type = T;
    uint32_t count {0};
    T objects[ Max ];
    hash_table_inline_t<uint32_t, Max, 4, 4> object_name_map {""}; // To be used during initialization only

    int32_t
    add(const constant_string_t &name, uint32_t allocation_count = 1)
    {
	object_name_map.insert(name.hash, count);

	uint32_t prev = count;
	count += allocation_count;
	
	return(prev);
    }

    inline int32_t
    get_handle(const constant_string_t &name)
    {
	return(*object_name_map.get(name.hash));
    }
    
    inline T *
    get(int32_t handle)
    {
	return(&objects[handle]);
    }

    // To use for convenience, not for performance
    inline T *
    get(const constant_string_t &name)
    {
	return(&objects[ *object_name_map.get(name.hash) ]);
    }

    inline void
    clean_up(void)
    {
	for (uint32_t i = 0; i < count; ++i)
	{
	    objects[i].destroy();
	}
    }
};

typedef VkCommandPool gpu_command_queue_pool_t;
typedef VkCommandBufferLevel submit_level_t;
typedef int32_t gpu_buffer_handle_t;
typedef int32_t image_handle_t;
typedef int32_t framebuffer_handle_t;
typedef int32_t render_pass_handle_t;
typedef int32_t pipeline_handle_t;
typedef int32_t model_handle_t;
typedef object_manager_t<gpu_buffer_t> gpu_buffer_manager_t;
typedef object_manager_t<image2d_t> image_manager_t;
typedef object_manager_t<framebuffer_t> framebuffer_manager_t;
typedef object_manager_t<render_pass_t> render_pass_manager_t;
typedef object_manager_t<graphics_pipeline_t> pipeline_manager_t;
typedef object_manager_t<model_t> model_manager_t;
typedef VkDescriptorPool uniform_pool_t;
typedef VkDescriptorSet uniform_group_t;
typedef VkDescriptorSetLayout uniform_layout_t;
typedef VkExtent2D resolution_t;
typedef VkRect2D rect2D_t;
typedef object_manager_t<uniform_layout_t> uniform_layout_manager_t;
typedef object_manager_t<uniform_group_t> uniform_group_manager_t;
extern gpu_buffer_manager_t g_gpu_buffer_manager;
extern image_manager_t g_image_manager;
extern framebuffer_manager_t g_framebuffer_manager;
extern render_pass_manager_t g_render_pass_manager;
extern pipeline_manager_t g_pipeline_manager;
extern uniform_layout_manager_t g_uniform_layout_manager;
extern uniform_group_manager_t g_uniform_group_manager;
extern model_manager_t g_model_manager;
extern uniform_pool_t g_uniform_pool;

struct gpu_command_queue_t
{
    VkCommandBuffer q{VK_NULL_HANDLE};

    int32_t subpass{-1};
    render_pass_handle_t current_pass_handle{INVALID_HANDLE};
    framebuffer_handle_t fbo_handle{INVALID_HANDLE};
    submit_level_t submit_level = submit_level_t::VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    void
    invalidate(void)
    {
        subpass = -1;
        current_pass_handle = INVALID_HANDLE;
        fbo_handle = INVALID_HANDLE;
    }

    template <typename ...Clears> void
    begin_render_pass(render_pass_handle_t pass
                      , framebuffer_handle_t fbo
                      , VkSubpassContents contents
                      , const Clears &...clear_values)
    {
        subpass = 0;

        current_pass_handle = pass;
        fbo_handle = fbo;

        VkClearValue clears[sizeof...(clear_values) + 1] = {clear_values..., VkClearValue{}};

        auto *fbo_object = g_framebuffer_manager.get(fbo);
        command_buffer_begin_render_pass(g_render_pass_manager.get(pass), fbo_object
                                                 , init_render_area({0, 0}, fbo_object->extent)
                                                 , {sizeof...(clear_values), clears}
                                                 , contents
                                                 , &q);
    }

    inline void
    next_subpass(VkSubpassContents contents)
    {
        command_buffer_next_subpass(&q, contents);

        ++subpass;
    }

    inline void
    end_render_pass()
    {
        command_buffer_end_render_pass(&q);
        invalidate();
    }
};

gpu_command_queue_t
make_command_queue(VkCommandPool *pool, submit_level_t level);

inline VkCommandBufferInheritanceInfo
make_queue_inheritance_info(render_pass_t *pass, framebuffer_t *framebuffer)
{
    VkCommandBufferInheritanceInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    info.renderPass = pass->render_pass;
    info.framebuffer = framebuffer->framebuffer;
    info.subpass = 0;
    return(info);
}

void
begin_command_queue(gpu_command_queue_t *queue, VkCommandBufferInheritanceInfo *inheritance = nullptr);
    
void
end_command_queue(gpu_command_queue_t *queue);

// --------------------- Uniform stuff ---------------------
// Naming is better than Descriptor in case of people familiar with different APIs / also will be useful when introducing other APIs
using uniform_binding_t = VkDescriptorSetLayoutBinding;

uniform_binding_t
make_uniform_binding_s(uint32_t count
		       , uint32_t binding
		       , VkDescriptorType uniform_type
		       , VkShaderStageFlags shader_flags);

// Layout depends on uniform bindings --> almost like a prototype for making uniform groups
// Separate Uniform_Layout_Info (list of binding structs) from Uniform_Layout (API struct) for optimisation reasons
struct uniform_layout_info_t // --> VkDescriptorSetLayout
{
    persist_var constexpr uint32_t MAX_BINDINGS = 15;
    uniform_binding_t bindings_buffer[MAX_BINDINGS] = {};
    uint32_t binding_count = 0;
    
    void
    push(const uniform_binding_t &binding_info);

    void
    push(uint32_t count
	 , uint32_t binding
	 , VkDescriptorType uniform_type
	 , VkShaderStageFlags shader_flags);
};



uniform_layout_t
make_uniform_layout(uniform_layout_info_t *blueprint);



uniform_group_t
make_uniform_group(uniform_layout_t *layout, VkDescriptorPool *pool);

enum binding_type_t { BUFFER, INPUT_ATTACHMENT, TEXTURE };
struct update_binding_t
{
    binding_type_t type;
    void *object;
    uint32_t binding;
    uint32_t t_changing_data = 0; // Offset into the buffer or image layout
    // Stuff that can change optionally
    uint32_t count = 1;
    uint32_t dst_element = 0;
};

// Template function, need to define here
template <typename ...Update_Ts> void
update_uniform_group(uniform_group_t *group, const Update_Ts &...updates)
{
    update_binding_t bindings[] = { updates... };

    uint32_t image_info_count = 0;
    VkDescriptorImageInfo image_info_buffer[20] = {};
    uint32_t buffer_info_count = 0;
    VkDescriptorBufferInfo buffer_info_buffer[20] = {};

    VkWriteDescriptorSet writes[sizeof...(updates)] = {};
    
    for (uint32_t i = 0; i < sizeof...(updates); ++i)
    {
        switch (bindings[i].type)
        {
        case binding_type_t::BUFFER:
            {
                gpu_buffer_t *ubo = (gpu_buffer_t *)bindings[i].object;
                buffer_info_buffer[buffer_info_count] = ubo->make_descriptor_info(bindings[i].t_changing_data);
                init_buffer_descriptor_set_write(group, bindings[i].binding, bindings[i].dst_element, bindings[i].count, &buffer_info_buffer[buffer_info_count], &writes[i]);
                ++buffer_info_count;
                break;
            }
        case binding_type_t::TEXTURE:
            {
                image2d_t *tx = (image2d_t *)bindings[i].object;
                image_info_buffer[image_info_count] = tx->make_descriptor_info((VkImageLayout)bindings[i].t_changing_data);
                init_image_descriptor_set_write(group, bindings[i].binding, bindings[i].dst_element, bindings[i].count, &image_info_buffer[image_info_count], &writes[i]);
                ++image_info_count;
                break;
            }
        case binding_type_t::INPUT_ATTACHMENT:
            {
                image2d_t *tx = (image2d_t *)bindings[i].object;
                image_info_buffer[image_info_count] = tx->make_descriptor_info((VkImageLayout)bindings[i].t_changing_data);
                init_input_attachment_descriptor_set_write(group, bindings[i].binding, bindings[i].dst_element, bindings[i].count, &image_info_buffer[image_info_count], &writes[i]);
                ++image_info_count;
                break;
            }
        }
    }

    update_descriptor_sets({sizeof...(updates), writes});
}

using uniform_layout_handle_t = int32_t;
using uniform_group_handle_t = int32_t;

// --------------------- Rendering stuff ---------------------
// Material is submittable to a GPU_Material_Submission_Queue to be eventually submitted to the GPU for render
struct material_t
{
    // ---- push constant information
    void *push_k_ptr = nullptr;
    uint32_t push_k_size = 0;
    // ---- vbo and ibo information
    struct mesh_t *mesh;
    // Some materials may have a ubo per instance (animated materials)
    uniform_group_t *ubo = nullptr;
};

// Queue of materials to be submitted
struct gpu_material_submission_queue_t
{
    uint32_t mtrl_count;
    memory_buffer_view_t<material_t> mtrls;
    
    VkShaderStageFlags push_k_dst;

    // for multi-threaded rendering in the future when needed
    int32_t cmdbuf_index{-1};

    uint32_t
    push_material(void *push_k_ptr, uint32_t push_k_size,
                  mesh_t *mesh,
                  uniform_group_t *ubo = nullptr);

    gpu_command_queue_t *
    get_command_buffer(gpu_command_queue_t *queue = nullptr);
    
    void
    submit_queued_materials(const memory_buffer_view_t<uniform_group_t> &uniform_groups
			    , graphics_pipeline_t *graphics_pipeline
			    , gpu_command_queue_t *main_queue
			    , submit_level_t level);
	
    void
    flush_queue(void);

    void
    submit_to_cmdbuf(gpu_command_queue_t *queue);
};

gpu_material_submission_queue_t
make_gpu_material_submission_queue(uint32_t max_materials, VkShaderStageFlags push_k_dst // for rendering purposes (quite Vulkan specific)
				   , submit_level_t level, gpu_command_queue_pool_t *pool);

void
submit_queued_materials_from_secondary_queues(gpu_command_queue_t *queue);

void
make_framebuffer_attachment(image2d_t *img, uint32_t w, uint32_t h, VkFormat format, uint32_t layer_count, VkImageUsageFlags usage, uint32_t dimensions);

// Need to fill in this texture with data from a separate function
void
make_texture(image2d_t *img, uint32_t w, uint32_t h, VkFormat, uint32_t layer_count, uint32_t dimensions, VkImageUsageFlags usage, VkFilter filter);

void
make_framebuffer(framebuffer_t *fbo
                 , uint32_t w, uint32_t h
                 , uint32_t layer_count
                 , render_pass_t *compatible
                 , const memory_buffer_view_t<image2d_t> &colors
                 , image2d_t *depth);

struct render_pass_attachment_t
{
    VkFormat format;
    // Initial layout is always undefined
    VkImageLayout final_layout;
};

struct render_pass_attachment_reference_t
{
    uint32_t index;
    VkImageLayout layout;
};

struct render_pass_subpass_t
{
    static constexpr uint32_t MAX_COLOR_ATTACHMENTS = 7;
    render_pass_attachment_reference_t color_attachments[MAX_COLOR_ATTACHMENTS];
    uint32_t color_attachment_count {0};

    render_pass_attachment_reference_t depth_attachment;
    bool enable_depth {0};

    render_pass_attachment_reference_t input_attachments[MAX_COLOR_ATTACHMENTS];
    uint32_t input_attachment_count {0};

    template <typename ...T> void
    set_color_attachment_references(const T &...ts)
    {
        render_pass_attachment_reference_t references[] { ts... };
        for (uint32_t i = 0; i < sizeof...(ts); ++i)
        {
            color_attachments[i] = references[i];
        }
        color_attachment_count = sizeof...(ts);
    }

    void
    set_depth(const render_pass_attachment_reference_t &reference)
    {
        enable_depth = true;
        depth_attachment = reference;
    }

    template <typename ...T> void
    set_input_attachment_references(const T &...ts)
    {
        render_pass_attachment_reference_t references[] { ts... };
        for (uint32_t i = 0; i < sizeof...(ts); ++i)
        {
            input_attachments[i] = references[i];
        }
        input_attachment_count = sizeof...(ts);
    }
};

struct render_pass_dependency_t
{
    int32_t src_index;
    VkPipelineStageFlags src_stage;
    uint32_t src_access;

    int32_t dst_index;
    VkPipelineStageFlags dst_stage;
    uint32_t dst_access;
};

render_pass_dependency_t
make_render_pass_dependency(int32_t src_index,
                            VkPipelineStageFlags src_stage,
                            uint32_t src_access,
                            int32_t dst_index,
                            VkPipelineStageFlags dst_stage,
                            uint32_t dst_access);

void
make_render_pass(render_pass_t *render_pass
                 , const memory_buffer_view_t<render_pass_attachment_t> &attachments
                 , const memory_buffer_view_t<render_pass_subpass_t> &subpasses
                 , const memory_buffer_view_t<render_pass_dependency_t> &dependencies
                 , bool clear_every_frame = true);

struct shader_module_info_t
{
    const char *filename;
    VkShaderStageFlagBits stage;
};

struct shader_modules_t
{
    static constexpr uint32_t MAX_SHADERS = 5;
    shader_module_info_t modules[MAX_SHADERS];
    uint32_t count;
    
    template <typename ...T>
    shader_modules_t(T ...modules_p)
        : modules{modules_p...}, count(sizeof...(modules_p))
    {
    }
};

struct shader_uniform_layouts_t
{
    static constexpr uint32_t MAX_LAYOUTS = 10;
    uniform_layout_handle_t layouts[MAX_LAYOUTS];
    uint32_t count;
    
    template <typename ...T>
    shader_uniform_layouts_t(T ...layouts_p)
        : layouts{layouts_p...}, count(sizeof...(layouts_p))
    {
    }
};

struct shader_pk_data_t
{
    uint32_t size;
    uint32_t offset;
    VkShaderStageFlags stages;
};

struct shader_blend_states_t
{
    static constexpr uint32_t MAX_BLEND_STATES = 10;
    // For now, is just boolean
    bool blend_states[MAX_BLEND_STATES];
    uint32_t count;
    
    template <typename ...T>
    shader_blend_states_t(T ...states)
        : blend_states{states...}, count(sizeof...(states))
    {
    }
};

struct dynamic_states_t
{
    static constexpr uint32_t MAX_DYNAMIC_STATES = 10;
    VkDynamicState dynamic_states[MAX_DYNAMIC_STATES];
    uint32_t count;

    template <typename ...T>
    dynamic_states_t(T ...states)
        : dynamic_states{states...}, count(sizeof...(states))
    {
    }
};

enum gpu_buffer_usage_t { VERTEX_BUFFER = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          INDEX_BUFFER = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          UNIFORM_BUFFER = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                          INVALID_USAGE };

void make_unmappable_gpu_buffer(gpu_buffer_t *dst_buffer,
                                uint32_t size,
                                void *data,
                                gpu_buffer_usage_t usage,
                                gpu_command_queue_pool_t *pool);

void
make_graphics_pipeline(graphics_pipeline_t *ppln
                       , const shader_modules_t &modules
                       , bool primitive_restart, VkPrimitiveTopology topology
                       , VkPolygonMode polygonmode, VkCullModeFlags culling
                       , shader_uniform_layouts_t &layouts
                       , const shader_pk_data_t &pk
                       , VkExtent2D viewport
                       , const shader_blend_states_t &blends
                       , model_t *model
                       , bool enable_depth
                       , float32_t depth_bias
                       , const dynamic_states_t &dynamic_states
                       , render_pass_t *compatible
                       , uint32_t subpass);

void
load_external_graphics_data(swapchain_t *swapchain
                            , VkDescriptorPool *pool
                            , VkCommandPool *cmdpool);

// Rendering pipeline
struct camera_t
{
    vector2_t mp;
    vector3_t p; // position
    vector3_t d; // direction
    vector3_t u; // up

    float32_t fov;
    float32_t asp; // aspect ratio
    float32_t n, f; // near and far planes
    
    vector4_t captured_frustum_corners[8] {};
    vector4_t captured_shadow_corners[8] {};
    
    matrix4_t p_m;
    matrix4_t v_m;
    
    void
    set_default(float32_t w, float32_t h, float32_t m_x, float32_t m_y)
    {
	mp = vector2_t(m_x, m_y);
	p = vector3_t(50.0f, 10.0f, 280.0f);
	d = vector3_t(+1, 0.0f, +1);
	u = vector3_t(0, 1, 0);

	fov = glm::radians(60.0f);
	asp = w / h;
	n = 1.0f;
	f = 100000.0f;
    }
    
    void
    compute_projection(void)
    {
	p_m = glm::perspective(fov, asp, n, f);
    } 
};

using camera_handle_t = int32_t;

camera_handle_t
add_camera(input_state_t *input_state, resolution_t resolution);

void
make_camera(camera_t *camera, float32_t fov, float32_t asp, float32_t near, float32_t far);

enum { CAMERA_BOUND_TO_3D_OUTPUT = -1 };

camera_t *
get_camera(camera_handle_t handle);

camera_t *
get_camera_bound_to_3d_output(void);

void
bind_camera_to_3d_scene_output(camera_handle_t handle);

memory_buffer_view_t<gpu_buffer_t>
get_camera_transform_ubos(void);

memory_buffer_view_t<uniform_group_t>
get_camera_transform_uniform_groups(void);

struct camera_transform_uniform_data_t
{
    alignas(16) matrix4_t view_matrix;
    alignas(16) matrix4_t projection_matrix;
    alignas(16) matrix4_t shadow_projection_matrix;
    alignas(16) matrix4_t shadow_view_matrix;
    
    alignas(16) vector4_t debug_vector;
};

void
make_camera_transform_uniform_data(camera_transform_uniform_data_t *data,
                                   const matrix4_t &view_matrix,
                                   const matrix4_t &projection_matrix,
                                   const matrix4_t &shadow_view_matrix,
                                   const matrix4_t &shadow_projection_matrix,
                                   const vector4_t &debug_vector);

void
update_3d_output_camera_transforms(uint32_t image_index);

struct shadow_matrices_t
{
    matrix4_t projection_matrix;
    matrix4_t light_view_matrix;
    matrix4_t inverse_light_view;
};

struct shadow_debug_t
{
    // For debugging the frustum
    union
    {
        struct {float32_t x_min, x_max, y_min, y_max, z_min, z_max;};
        float32_t corner_values[6];
    };

    vector4_t frustum_corners[8];
};

struct shadow_display_t
{
    uint32_t shadowmap_w, shadowmap_h;
    uniform_group_t texture;
};

void
render_3d_frustum_debug_information(gpu_command_queue_t *queue, uint32_t image_index);

shadow_matrices_t
get_shadow_matrices(void);

shadow_debug_t
get_shadow_debug(void);

shadow_display_t
get_shadow_display(void);

void
update_shadows(float32_t far, float32_t near, float32_t fov, float32_t aspect
               // later to replace with a Camera structure
               , const vector3_t &ws_p
               , const vector3_t &ws_d
               , const vector3_t &ws_up);

void
begin_shadow_offscreen(uint32_t shadow_map_width, uint32_t shadow_map_height
                       , gpu_command_queue_t *queue);

void
end_shadow_offscreen(gpu_command_queue_t *queue);

resolution_t
get_backbuffer_resolution(void);

void
begin_deferred_rendering(uint32_t image_index /* to remove in the future */
                         , gpu_command_queue_t *queue);

void
end_deferred_rendering(const matrix4_t &view_matrix
                       , gpu_command_queue_t *queue);

void
render_atmosphere(const memory_buffer_view_t<uniform_group_t> &sets
                  , const vector3_t &camera_position // to change to camera structure
                  , gpu_command_queue_t *queue);

void
update_atmosphere(gpu_command_queue_t *queue);

void
make_postfx_data(swapchain_t *swapchain);

framebuffer_handle_t
get_pfx_framebuffer_hdl(void);

// For debug purposes like capture frame
void
dbg_handle_input(input_state_t *input_state);

void
apply_pfx_on_scene(gpu_command_queue_t *queue
                   , uniform_group_t *transforms_group
                   , const matrix4_t &view_matrix
                   , const matrix4_t &projection_matrix);

void
render_final_output(uint32_t image_index, gpu_command_queue_t *queue);

void
initialize_game_3d_graphics(gpu_command_queue_pool_t *pool);

void
initialize_game_2d_graphics(gpu_command_queue_pool_t *pool);

void
destroy_graphics(void);



// TODO: Model loading stuff
// TODO: Possibly support more formats
// TODO: Support colors and textures (for now, just positions and normals)
enum mesh_file_format_t { OBJ, CUSTOM_MESH, INVALID_MESH_FILE_FORMAT };

// Each attribute will be in different buffers
// But if the buffers aren't too big, squeeze into one buffer
enum buffer_type_t : char { INDICES, VERTEX, NORMAL, UVS, COLOR, JOINT_WEIGHT, JOINT_INDICES, EXTRA_V3, EXTRA_V2, EXTRA_V1, INVALID_BUFFER_TYPE };

struct mesh_buffer_t
{
    gpu_buffer_t gpu_buffer;
    buffer_type_t type = buffer_type_t::INVALID_BUFFER_TYPE;
};

// TODO: Refactor the model system to use mesh_t for instances of model_t
struct mesh_t
{
    persist_var constexpr uint32_t MAX_BUFFERS = 6;
    mesh_buffer_t buffers[buffer_type_t::INVALID_BUFFER_TYPE];
    uint32_t buffer_count = 0;
    buffer_type_t buffer_types_stack[MAX_BUFFERS];

    memory_buffer_view_t<VkBuffer> raw_buffer_list;
    model_index_data_t index_data;
    draw_indexed_data_t indexed_data;
};

void create_mesh_raw_buffer_list(mesh_t *mesh);

void push_buffer_to_mesh(buffer_type_t buffer_type, mesh_t *mesh);
bool32_t mesh_has_buffer_type(buffer_type_t buffer_type, mesh_t *mesh);
mesh_buffer_t *get_mesh_buffer_object(buffer_type_t buffer_type, mesh_t *mesh);

// Loaded internally (terrain)
mesh_t initialize_mesh(memory_buffer_view_t<VkBuffer> &vbos, draw_indexed_data_t *index_data, model_index_data_t *model_index_data);
// Loaded externally
mesh_t load_mesh(mesh_file_format_t format, const char *path, gpu_command_queue_pool_t *cmdpool);
model_t make_mesh_attribute_and_binding_information(mesh_t *mesh);

struct joint_t
{
    persist_var constexpr uint32_t MAX_CHILD_JOINTS = 4;
    
    uint32_t joint_id = 0;
    uint32_t parent_joint_id = 0;
    uint32_t children_joint_count = 0;
    uint32_t children_joint_ids[MAX_CHILD_JOINTS] = {};
    // TODO: To remove if not needed by the game!
    matrix4_t bind_transform;
    matrix4_t inverse_bind_transform;    
};

struct skeleton_t
{
    uint32_t joint_count;
    joint_t *joints;
    // Mostly for debugging purposes
    const char **joint_names;
};

struct key_frame_joint_transform_t
{
    quaternion_t rotation;
    vector3_t position;
};

struct key_frame_t
{
    float32_t time_stamp;
    uint32_t joint_transforms_count;
    key_frame_joint_transform_t *joint_transforms;
};

struct animation_cycle_t
{
    const char *name;
    uint32_t key_frame_count;
    key_frame_t *key_frames;

    float32_t total_animation_time;
};

struct animation_cycles_t
{
    persist_var constexpr uint32_t MAX_ANIMATIONS = 5;
    animation_cycle_t cycles[MAX_ANIMATIONS];
    uint32_t cycle_count;
};

joint_t *get_joint(uint32_t joint_id, skeleton_t *skeleton);

skeleton_t load_skeleton(const char *path);

animation_cycles_t load_animations(const char *path);

struct animated_instance_t
{
    float32_t current_animation_time;
    
    skeleton_t *skeleton;
    animation_cycle_t *bound_cycle;
    animation_cycles_t *cycles;
    // One for each joint
    matrix4_t *interpolated_transforms;
    
    gpu_buffer_t interpolated_transforms_ubo;
    uniform_group_t group;
};

void bind_to_cycle(animated_instance_t *instance, uint32_t cycle_index);
/* TODO: interpolate_between_cycles() */

animated_instance_t initialize_animated_instance(gpu_command_queue_pool_t *pool, uniform_layout_t *gpu_ubo_layout, skeleton_t *skeleton, animation_cycles_t *cycles);
void destroy_animated_instance(animated_instance_t *instance);

void interpolate_skeleton_joints_into_instance(float32_t dt, animated_instance_t *instance);
void update_animated_instance_ubo(gpu_command_queue_t *queue, animated_instance_t *instance);
