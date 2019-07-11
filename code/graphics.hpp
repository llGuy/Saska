#pragma once

#include "core.hpp"
#include "vulkan.hpp"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

enum {INVALID_HANDLE = -1};

typedef s32 Handle;
template <typename T, u32 Max = 40> struct Object_Manager
{
    using Type = T;
    u32 count {0};
    T objects[ Max ];
    Hash_Table_Inline<u32, Max, 4, 4> object_name_map {""}; // To be used during initialization only

    Handle
    add(const Constant_String &name, u32 allocation_count = 1)
    {
	object_name_map.insert(name.hash, count);

	u32 prev = count;
	count += allocation_count;
	
	return(prev);
    }

    inline Handle
    get_handle(const Constant_String &name)
    {
	return(*object_name_map.get(name.hash));
    }
    
    inline T *
    get(Handle handle)
    {
	return(&objects[handle]);
    }

    // To use for convenience, not for performance
    inline T *
    get(const Constant_String &name)
    {
	return(&objects[ *object_name_map.get(name.hash) ]);
    }

    inline void
    clean_up(GPU *gpu)
    {
	for (u32 i = 0; i < count; ++i)
	{
	    objects[i].destroy(gpu);
	}
    }
};

typedef VkCommandPool GPU_Command_Queue_Pool;
typedef VkCommandBufferLevel Submit_Level;
typedef Handle GPU_Buffer_Handle;
typedef Handle Image_Handle;
typedef Handle Framebuffer_Handle;
typedef Handle Render_Pass_Handle;
typedef Handle Pipeline_Handle;
typedef Handle Model_Handle;
typedef Object_Manager<GPU_Buffer> GPU_Buffer_Manager;
typedef Object_Manager<Image2D> Image_Manager;
typedef Object_Manager<Framebuffer> Framebuffer_Manager;
typedef Object_Manager<Render_Pass> Render_Pass_Manager;
typedef Object_Manager<Graphics_Pipeline> Pipeline_Manager;
typedef Object_Manager<Model> Model_Manager;
typedef VkDescriptorPool Uniform_Pool;
typedef VkDescriptorSet Uniform_Group;
typedef VkDescriptorSetLayout Uniform_Layout;
typedef VkExtent2D Resolution;
typedef VkRect2D Rect2D;
typedef Object_Manager<Uniform_Layout> Uniform_Layout_Manager;
typedef Object_Manager<Uniform_Group> Uniform_Group_Manager;
extern GPU_Buffer_Manager g_gpu_buffer_manager;
extern Image_Manager g_image_manager;
extern Framebuffer_Manager g_framebuffer_manager;
extern Render_Pass_Manager g_render_pass_manager;
extern Pipeline_Manager g_pipeline_manager;
extern Uniform_Layout_Manager g_uniform_layout_manager;
extern Uniform_Group_Manager g_uniform_group_manager;
extern Model_Manager g_model_manager;
extern Uniform_Pool g_uniform_pool;

struct GPU_Command_Queue
{
    VkCommandBuffer q{VK_NULL_HANDLE};

    s32 subpass{-1};
    Render_Pass_Handle current_pass_handle{INVALID_HANDLE};
    Framebuffer_Handle fbo_handle{INVALID_HANDLE};
    Submit_Level submit_level = Submit_Level::VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    void
    invalidate(void)
    {
        subpass = -1;
        current_pass_handle = INVALID_HANDLE;
        fbo_handle = INVALID_HANDLE;
    }

    template <typename ...Clears> void
    begin_render_pass(Render_Pass_Handle pass
                      , Framebuffer_Handle fbo
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

GPU_Command_Queue
make_command_queue(VkCommandPool *pool, Submit_Level level, GPU *gpu);

inline VkCommandBufferInheritanceInfo
make_queue_inheritance_info(Render_Pass *pass, Framebuffer *framebuffer)
{
    VkCommandBufferInheritanceInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    info.renderPass = pass->render_pass;
    info.framebuffer = framebuffer->framebuffer;
    info.subpass = 0;
    return(info);
}

void
begin_command_queue(GPU_Command_Queue *queue, GPU *gpu, VkCommandBufferInheritanceInfo *inheritance = nullptr);
    
void
end_command_queue(GPU_Command_Queue *queue, GPU *gpu);

// --------------------- Uniform stuff ---------------------
// Naming is better than Descriptor in case of people familiar with different APIs / also will be useful when introducing other APIs
using Uniform_Binding = VkDescriptorSetLayoutBinding;

Uniform_Binding
make_uniform_binding_s(u32 count
		       , u32 binding
		       , VkDescriptorType uniform_type
		       , VkShaderStageFlags shader_flags);

// Layout depends on uniform bindings --> almost like a prototype for making uniform groups
// Separate Uniform_Layout_Info (list of binding structs) from Uniform_Layout (API struct) for optimisation reasons
struct Uniform_Layout_Info // --> VkDescriptorSetLayout
{
    persist constexpr u32 MAX_BINDINGS = 15;
    Uniform_Binding bindings_buffer[MAX_BINDINGS] = {};
    u32 binding_count = 0;
    
    void
    push(const Uniform_Binding &binding_info);

    void
    push(u32 count
	 , u32 binding
	 , VkDescriptorType uniform_type
	 , VkShaderStageFlags shader_flags);
};



Uniform_Layout
make_uniform_layout(Uniform_Layout_Info *blueprint, GPU *gpu);



Uniform_Group
make_uniform_group(Uniform_Layout *layout, VkDescriptorPool *pool, GPU *gpu);

enum Binding_Type { BUFFER, INPUT_ATTACHMENT, TEXTURE };
struct Update_Binding
{
    Binding_Type type;
    void *object;
    u32 binding;
    u32 t_changing_data = 0; // Offset into the buffer or image layout
    // Stuff that can change optionally
    u32 count = 1;
    u32 dst_element = 0;
};

// Template function, need to define here
template <typename ...Update_Ts> void
update_uniform_group(GPU *gpu, Uniform_Group *group, const Update_Ts &...updates)
{
    Update_Binding bindings[] = { updates... };

    u32 image_info_count = 0;
    VkDescriptorImageInfo image_info_buffer[20] = {};
    u32 buffer_info_count = 0;
    VkDescriptorBufferInfo buffer_info_buffer[20] = {};

    VkWriteDescriptorSet writes[sizeof...(updates)] = {};
    
    for (u32 i = 0; i < sizeof...(updates); ++i)
    {
        switch (bindings[i].type)
        {
        case Binding_Type::BUFFER:
            {
                GPU_Buffer *ubo = (GPU_Buffer *)bindings[i].object;
                buffer_info_buffer[buffer_info_count] = ubo->make_descriptor_info(bindings[i].t_changing_data);
                init_buffer_descriptor_set_write(group, bindings[i].binding, bindings[i].dst_element, bindings[i].count, &buffer_info_buffer[buffer_info_count], &writes[i]);
                ++buffer_info_count;
                break;
            }
        case Binding_Type::TEXTURE:
            {
                Image2D *tx = (Image2D *)bindings[i].object;
                image_info_buffer[image_info_count] = tx->make_descriptor_info((VkImageLayout)bindings[i].t_changing_data);
                init_image_descriptor_set_write(group, bindings[i].binding, bindings[i].dst_element, bindings[i].count, &image_info_buffer[image_info_count], &writes[i]);
                ++image_info_count;
                break;
            }
        case Binding_Type::INPUT_ATTACHMENT:
            {
                Image2D *tx = (Image2D *)bindings[i].object;
                image_info_buffer[image_info_count] = tx->make_descriptor_info((VkImageLayout)bindings[i].t_changing_data);
                init_input_attachment_descriptor_set_write(group, bindings[i].binding, bindings[i].dst_element, bindings[i].count, &image_info_buffer[image_info_count], &writes[i]);
                ++image_info_count;
                break;
            }
        }
    }

    update_descriptor_sets({sizeof...(updates), writes}, gpu);
}

using Uniform_Layout_Handle = Handle;
using Uniform_Group_Handle = Handle;

// --------------------- Rendering stuff ---------------------
// Material is submittable to a GPU_Material_Submission_Queue to be eventually submitted to the GPU for render
struct Material 
{
    // ---- push constant information
    void *push_k_ptr = nullptr;
    u32 push_k_size = 0;
    // ---- vbo information
    Memory_Buffer_View<VkBuffer> vbo_bindings;
    // ---- for sorting
    u32 model_id;
    // ---- ibo information
    Model_Index_Data index_data;
    Draw_Indexed_Data draw_index_data;
};

// Queue of materials to be submitted
struct GPU_Material_Submission_Queue
{
    u32 mtrl_count;
    Memory_Buffer_View<Material> mtrls;
    
    VkShaderStageFlags push_k_dst;

    // for multi-threaded rendering in the future when needed
    s32 cmdbuf_index{-1};

    u32
    push_material(void *push_k_ptr, u32 push_k_size
		  , const Memory_Buffer_View<VkBuffer> &vbo_bindings
		  , const Model_Index_Data &index_data
		  , const Draw_Indexed_Data &draw_index_data);

    GPU_Command_Queue *
    get_command_buffer(GPU_Command_Queue *queue = nullptr);
    
    void
    submit_queued_materials(const Memory_Buffer_View<Uniform_Group> &uniform_groups
			    , Graphics_Pipeline *graphics_pipeline
			    , GPU_Command_Queue *main_queue
			    , Submit_Level level);
	
    void
    flush_queue(void);

    void
    submit_to_cmdbuf(GPU_Command_Queue *queue);
};

GPU_Material_Submission_Queue
make_gpu_material_submission_queue(u32 max_materials, VkShaderStageFlags push_k_dst // for rendering purposes (quite Vulkan specific)
				   , Submit_Level level, GPU_Command_Queue_Pool *pool, GPU *gpu);

void
submit_queued_materials_from_secondary_queues(GPU_Command_Queue *queue);

void
make_framebuffer_attachment(Image2D *img, u32 w, u32 h, VkFormat format, u32 layer_count, VkImageUsageFlags usage, u32 dimensions, GPU *gpu);

// Need to fill in this texture with data from a separate function
void
make_texture(Image2D *img, u32 w, u32 h, VkFormat, u32 layer_count, u32 dimensions, GPU *gpu);

void
make_framebuffer(Framebuffer *fbo
                 , u32 w, u32 h
                 , u32 layer_count
                 , Render_Pass *compatible
                 , const Memory_Buffer_View<Image2D> &colors
                 , Image2D *depth
                 , GPU *gpu);

struct Render_Pass_Attachment
{
    VkFormat format;
    // Initial layout is always undefined
    VkImageLayout final_layout;
};

struct Render_Pass_Attachment_Reference
{
    u32 index;
    VkImageLayout layout;
};

struct Render_Pass_Subpass
{
    static constexpr u32 MAX_COLOR_ATTACHMENTS = 7;
    Render_Pass_Attachment_Reference color_attachments[MAX_COLOR_ATTACHMENTS];
    u32 color_attachment_count {0};

    Render_Pass_Attachment_Reference depth_attachment;
    bool enable_depth {0};

    Render_Pass_Attachment_Reference input_attachments[MAX_COLOR_ATTACHMENTS];
    u32 input_attachment_count {0};

    template <typename ...T> void
    set_color_attachment_references(const T &...ts)
    {
        Render_Pass_Attachment_Reference references[] { ts... };
        for (u32 i = 0; i < sizeof...(ts); ++i)
        {
            color_attachments[i] = references[i];
        }
        color_attachment_count = sizeof...(ts);
    }

    void
    set_depth(const Render_Pass_Attachment_Reference &reference)
    {
        enable_depth = true;
        depth_attachment = reference;
    }

    template <typename ...T> void
    set_input_attachment_references(const T &...ts)
    {
        Render_Pass_Attachment_Reference references[] { ts... };
        for (u32 i = 0; i < sizeof...(ts); ++i)
        {
            input_attachments[i] = references[i];
        }
        input_attachment_count = sizeof...(ts);
    }
};

struct Render_Pass_Dependency
{
    s32 src_index;
    VkPipelineStageFlags src_stage;
    u32 src_access;

    s32 dst_index;
    VkPipelineStageFlags dst_stage;
    u32 dst_access;
};

Render_Pass_Dependency
make_render_pass_dependency(s32 src_index,
                            VkPipelineStageFlags src_stage,
                            u32 src_access,
                            s32 dst_index,
                            VkPipelineStageFlags dst_stage,
                            u32 dst_access);

void
make_render_pass(Render_Pass *render_pass
                 , const Memory_Buffer_View<Render_Pass_Attachment> &attachments
                 , const Memory_Buffer_View<Render_Pass_Subpass> &subpasses
                 , const Memory_Buffer_View<Render_Pass_Dependency> &dependencies
                 , GPU *gpu
                 , bool clear_every_frame = true);

struct Shader_Module_Info
{
    const char *filename;
    VkShaderStageFlagBits stage;
};

struct Shader_Modules
{
    static constexpr u32 MAX_SHADERS = 5;
    Shader_Module_Info modules[MAX_SHADERS];
    u32 count;
    
    template <typename ...T>
    Shader_Modules(T ...modules_p)
        : modules{modules_p...}, count(sizeof...(modules_p))
    {
    }
};

struct Shader_Uniform_Layouts
{
    static constexpr u32 MAX_LAYOUTS = 10;
    Uniform_Layout_Handle layouts[MAX_LAYOUTS];
    u32 count;
    
    template <typename ...T>
    Shader_Uniform_Layouts(T ...layouts_p)
        : layouts{layouts_p...}, count(sizeof...(layouts_p))
    {
    }
};

struct Shader_PK_Data
{
    u32 size;
    u32 offset;
    VkShaderStageFlagBits stages;
};

struct Shader_Blend_States
{
    static constexpr u32 MAX_BLEND_STATES = 10;
    // For now, is just boolean
    bool blend_states[MAX_BLEND_STATES];
    u32 count;
    
    template <typename ...T>
    Shader_Blend_States(T ...states)
        : blend_states{states...}, count(sizeof...(states))
    {
    }
};

struct Dynamic_States
{
    static constexpr u32 MAX_DYNAMIC_STATES = 10;
    VkDynamicState dynamic_states[MAX_DYNAMIC_STATES];
    u32 count;

    template <typename ...T>
    Dynamic_States(T ...states)
        : dynamic_states{states...}, count(sizeof...(states))
    {
    }
};

void
make_graphics_pipeline(Graphics_Pipeline *ppln
                       , const Shader_Modules &modules
                       , bool primitive_restart, VkPrimitiveTopology topology
                       , VkPolygonMode polygonmode, VkCullModeFlags culling
                       , Shader_Uniform_Layouts &layouts
                       , const Shader_PK_Data &pk
                       , VkExtent2D viewport
                       , const Shader_Blend_States &blends
                       , Model *model
                       , bool enable_depth
                       , f32 depth_bias
                       , const Dynamic_States &dynamic_states
                       , Render_Pass *compatible
                       , u32 subpass
                       , GPU *gpu);

void
load_external_graphics_data(Swapchain *swapchain
                            , GPU *gpu
                            , VkDescriptorPool *pool
                            , VkCommandPool *cmdpool);

// Rendering pipeline
struct Camera
{
    v2 mp;
    v3 p; // position
    v3 d; // direction
    v3 u; // up

    f32 fov;
    f32 asp; // aspect ratio
    f32 n, f; // near and far planes
    
    v4 captured_frustum_corners[8] {};
    v4 captured_shadow_corners[8] {};
    
    m4x4 p_m;
    m4x4 v_m;
    
    void
    set_default(f32 w, f32 h, f32 m_x, f32 m_y)
    {
	mp = v2(m_x, m_y);
	p = v3(50.0f, 10.0f, 280.0f);
	d = v3(+1, 0.0f, +1);
	u = v3(0, 1, 0);

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

using Camera_Handle = Handle;

Camera_Handle
add_camera(Window_Data *window, Resolution resolution);

void
make_camera(Camera *camera, f32 fov, f32 asp, f32 near, f32 far);

enum { CAMERA_BOUND_TO_3D_OUTPUT = -1 };

Camera *
get_camera(Camera_Handle handle);

Camera *
get_camera_bound_to_3D_output(void);

void
bind_camera_to_3D_scene_output(Camera_Handle handle);

Memory_Buffer_View<GPU_Buffer>
get_camera_transform_ubos(void);

Memory_Buffer_View<Uniform_Group>
get_camera_transform_uniform_groups(void);

struct Camera_Transform_Uniform_Data
{
    alignas(16) m4x4 view_matrix;
    alignas(16) m4x4 projection_matrix;
    alignas(16) m4x4 shadow_projection_matrix;
    alignas(16) m4x4 shadow_view_matrix;
    
    alignas(16) v4 debug_vector;
};

void
make_camera_transform_uniform_data(Camera_Transform_Uniform_Data *data,
                                   const m4x4 &view_matrix,
                                   const m4x4 &projection_matrix,
                                   const m4x4 &shadow_view_matrix,
                                   const m4x4 &shadow_projection_matrix,
                                   const v4 &debug_vector);

void
update_3D_output_camera_transforms(u32 image_index, GPU *gpu);

struct Shadow_Matrices
{
    m4x4 projection_matrix;
    m4x4 light_view_matrix;
    m4x4 inverse_light_view;
};

struct Shadow_Debug
{
    // For debugging the frustum
    union
    {
        struct {f32 x_min, x_max, y_min, y_max, z_min, z_max;};
        f32 corner_values[6];
    };

    v4 frustum_corners[8];
};

struct Shadow_Display
{
    u32 shadowmap_w, shadowmap_h;
    Uniform_Group texture;
};

void
render_3D_frustum_debug_information(GPU_Command_Queue *queue, u32 image_index);

Shadow_Matrices
get_shadow_matrices(void);

Shadow_Debug
get_shadow_debug(void);

Shadow_Display
get_shadow_display(void);

void
update_shadows(f32 far, f32 near, f32 fov, f32 aspect
               // Later to replace with a Camera structure
               , const v3 &ws_p
               , const v3 &ws_d
               , const v3 &ws_up);

void
begin_shadow_offscreen(u32 shadow_map_width, u32 shadow_map_height
                       , GPU_Command_Queue *queue);

void
end_shadow_offscreen(GPU_Command_Queue *queue);

Resolution
get_backbuffer_resolution(void);

void
begin_deferred_rendering(u32 image_index /* To remove in the future */
                         , GPU_Command_Queue *queue);

void
end_deferred_rendering(const m4x4 &view_matrix
                       , GPU_Command_Queue *queue);

void
render_atmosphere(const Memory_Buffer_View<Uniform_Group> &sets
                  , const v3 &camera_position // To change to camera structure
                  , Model *cube
                  , GPU_Command_Queue *queue);

void
update_atmosphere(GPU_Command_Queue *queue);

void
make_postfx_data(GPU *gpu
                 , Swapchain *swapchain);

Framebuffer_Handle
get_pfx_framebuffer_hdl(void);

void
apply_pfx_on_scene(GPU_Command_Queue *queue
                   , Uniform_Group *transforms_group
                   , const m4x4 &view_matrix
                   , const m4x4 &projection_matrix
                   , GPU *gpu);

void
render_final_output(u32 image_index, GPU_Command_Queue *queue, Swapchain *swapchain);

void
initialize_game_3D_graphics(GPU *gpu,
                            Swapchain *swapchain,
                            GPU_Command_Queue_Pool *pool);

void
initialize_game_2D_graphics(GPU *gpu,
                            Swapchain *swapchain,
                            GPU_Command_Queue_Pool *pool);

void
destroy_graphics(GPU *gpu);
