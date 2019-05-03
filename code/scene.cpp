#include <chrono>
#include "scene.hpp"
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include "rendering.hpp"
#include <glm/gtx/transform.hpp>
#include "load.hpp"
#include <glm/gtc/quaternion.hpp>

#define MAX_ENTITIES_UNDER_TOP 10
#define MAX_ENTITIES_UNDER_PLANET 150

constexpr f32 PI = 3.14159265359f;

// index into array
typedef struct Entity_View
{
    // may have other data in the future in case we create separate arrays for different types of entities
    s32 id :28;
    
    enum Is_Group { IS_NOT_GROUP = false
		    , IS_GROUP = true } is_group :4;

    Entity_View(void) : id(-1), is_group(Is_Group::IS_NOT_GROUP) {}
} Entity_View, Entity_Group_View;

#define MAX_ENTITIES 100

typedef struct Entity
{
    Entity(void) = default;
    
    enum Is_Group { IS_NOT_GROUP = false
		    , IS_GROUP = true } is_group{};

    Constant_String id {""_hash};
    // position, direction, velocity
    // in above entity group space
    glm::vec3 gs_p{0.0f}, ws_d{0.0f}, gs_v{0.0f};
    glm::quat gs_r{0.0f, 0.0f, 0.0f, 0.0f};

    // push constant stuff for the graphics pipeline
    struct
    {
	// in world space
	glm::mat4x4 ws_t{1.0f};
    } push_k;
    
    // always will be a entity group - so, to update all the groups only, will climb UP the ladder
    Entity_View above;
    Entity_View index;

    using Entity_State_Flags = u32;
    enum {GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT = 1 << 0};
    Entity_State_Flags flags{};
    
    union
    {
	// all data for entities that are not entity groups
	struct
	{
	    // for rendering information
	    Rendering::Material_Access mtrl_access;
	};

	struct
	{
	    u32 below_count;
	    Memory_Buffer_View<Entity_View> below;
	};
    };
} Entity, Entity_Group;

Entity
construct_entity(const Constant_String &name
		 , Entity::Is_Group is_group
		 , glm::vec3 gs_p
		 , glm::vec3 ws_d
		 , glm::quat gs_r)
{
    Entity e;
    e.is_group = is_group;
    e.gs_p = gs_p;
    e.ws_d = ws_d;
    e.gs_r = gs_r;
    e.id = name;
    return(e);
}

global_var struct Entities
{
    s32 count_singles = {};
    Entity list_singles[MAX_ENTITIES] = {};

    s32 count_groups = {};
    Entity_Group list_groups[10] = {};

    Hash_Table_Inline<Entity_View, 25, 5, 5> name_map{"map.entities"};
    
    // have some sort of stack of REMOVED entities
} entities;

// contains the top entity / entity_group
global_var Entity_View scene_graph;

internal Entity *
get_entity(const Constant_String &name)
{
    Entity_View v = *entities.name_map.get(name.hash);
    return(&entities.list_singles[v.id]);
}

internal Entity_Group *
get_entity(Entity_View v)
{
    return(&entities.list_singles[v.id]);
}

internal Entity *
get_entity_group(const Constant_String &name)
{
    Entity_View v = *entities.name_map.get(name.hash);
    return(&entities.list_groups[v.id]);
}

internal Entity *
get_entity_group(Entity_Group_View v)
{
    return(&entities.list_groups[v.id]);
}

internal Entity_View
add_entity(const Entity &e
	   , Entity_Group *group_e_belongs_to)
{
    Entity_View view;
    view.id = entities.count_singles;
    view.is_group = (Entity_View::Is_Group)e.is_group;

    entities.name_map.insert(e.id.hash, view);
    
    entities.list_singles[entities.count_singles++] = e;

    auto e_ptr = get_entity(view);

    e_ptr->index = view;

    if (group_e_belongs_to)
    {
	assert(group_e_belongs_to->below_count <= group_e_belongs_to->below.count);
	
	e_ptr->above = group_e_belongs_to->index;
	group_e_belongs_to->below[group_e_belongs_to->below_count++] = e_ptr->index;
    }
    else e_ptr->above.id = -1;

    return(view);
}

internal Entity_Group_View
add_entity_group(const Entity_Group &e
		 , Entity_Group *group_e_belongs_to
		 , u32 group_below_max)
{
    Entity_Group_View view;
    view.id = entities.count_groups;
    view.is_group = (Entity_View::Is_Group)e.is_group;

    entities.name_map.insert(e.id.hash, view);
    
    entities.list_groups[entities.count_groups++] = e;

    auto e_ptr = get_entity_group(view);

    e_ptr->index = view;

    if (group_e_belongs_to)
    {
	// make sure that the capacity of the below buffer wasn't met
	assert(group_e_belongs_to->below_count <= group_e_belongs_to->below.count);
	
	e_ptr->above = group_e_belongs_to->index;
	group_e_belongs_to->below.buffer[group_e_belongs_to->below_count++] = e_ptr->index;
    }
    else e_ptr->above.id = -1;

    e_ptr->below_count = 0;
    allocate_memory_buffer(e_ptr->below, group_below_max);

    return(view);
}

internal void
init_scene_graph(void)
{
    // init first entity group (that will contain all the entities)
    // everything moves with top (aka the base entitygroup)
    Entity_Group top = construct_entity("entity.group.top"_hash
					, Entity::Is_Group::IS_GROUP
					, glm::vec3(10.0f)
					, glm::vec3(0.0f)
					, glm::quat(0.0f, 0.0f, 0.0f, 0.0f));
    
    Entity_Group_View top_view = add_entity_group(top, nullptr, MAX_ENTITIES_UNDER_TOP);
}

internal void
make_entity_renderable(Entity *e_ptr
		       , R_Mem<Vulkan_API::Model> model
		       , const Constant_String &e_mtrl_name)
{
    Rendering::Material_Data mtrl_data = {};
    mtrl_data.data = &e_ptr->push_k;
    mtrl_data.data_size = sizeof(e_ptr->push_k);
    mtrl_data.model = model;

    mtrl_data.draw_info = Vulkan_API::init_draw_indexed_data_default(1, model.p->index_data.index_count);
    
    Rendering::init_material(e_mtrl_name, &mtrl_data);
}

internal void
make_entity_instanced_renderable(R_Mem<Vulkan_API::Model> model
				 , const Constant_String &e_mtrl_name)
{
    // TODO(luc) : first need to add support for instance rendering in material renderers.
}

// problem : rotation doesn't incorporate position
internal void
update_entity_group(Entity_Group *g)
{
    if (!(g->flags & Entity::GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT))
    {
	// update position or whatever + including the position of above groups
	if (g->above.id != -1) update_entity_group(get_entity_group(g->above));

	const glm::mat4x4 *above_ws_t = (g->above.id == -1) ? &IDENTITY_MAT4X4 : &get_entity_group(g->above)->push_k.ws_t;
	g->push_k.ws_t = glm::translate(g->gs_p) * glm::mat4_cast(g->gs_r);
	g->push_k.ws_t = *above_ws_t * g->push_k.ws_t;

	g->flags |= Entity_Group::GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT;
    }
    else
    {
	// do nothing
    }
}

internal void
update_scene_graph(void)
{
    // first only update entity groups
    for (s32 i = 0; i < entities.count_groups; ++i)
    {
	Entity_Group *g = &entities.list_groups[i];
	if (g->flags & Entity_Group::GROUP_PHYSICS_INFO_HAS_BEEN_UPDATED_BIT)
	{
	    continue;
	}
	else
	{
	    update_entity_group(g);
	}
    }

    for (s32 i = 0; i < entities.count_singles; ++i)
    {
	Entity *e = &entities.list_singles[i];
	e->push_k.ws_t = get_entity_group(e->above)->push_k.ws_t * glm::translate(e->gs_p) * glm::mat4_cast(e->gs_r);
    }

    for (s32 i = 0; i < entities.count_groups; ++i)
    {
	Entity_Group *g = &entities.list_groups[i];
	g->flags = 0;
	g->push_k.ws_t = glm::mat4(1.0f);
    }
}

void
Camera::set_default(f32 w, f32 h, f32 m_x, f32 m_y)
{
    mp = glm::vec2(m_x, m_y);
    p = glm::vec3(10.0f, 10.0f, -10.0f);
    d = glm::vec3(+1, 0.0f, +1);
    u = glm::vec3(0, 1, 0);

    fov = 60.0f;
    asp = w / h;
    n = 0.1f;
    f = 100000.0f;
}

void
Camera::compute_projection(void)
{
    p_m = glm::perspective(fov, asp, n, f);
}

void
Camera::compute_view(void)
{
    v_m = glm::lookAt(p, p + d, u);
}

// atmosphere stuff
internal void
init_atmosphere_descriptor_set_layout(Vulkan_API::GPU *gpu)
{
    R_Mem<VkDescriptorSetLayout> atmos_layout = register_memory("descriptor_set_layout.atmosphere_layout"_hash
								, sizeof(VkDescriptorSetLayout));
    Vulkan_API::init_descriptor_set_layout(null_buffer<VkDescriptorSetLayoutBinding>()
					   , gpu
					   , atmos_layout.p);
}

internal void
init_atmosphere_cubemap(Vulkan_API::GPU *gpu)
{
    persist constexpr u32 ATMOSPHERE_CUBEMAP_IMAGE_WIDTH = 1000;
    persist constexpr u32 ATMOSPHERE_CUBEMAP_IMAGE_HEIGHT = 1000;
    
    R_Mem<Vulkan_API::Image2D> cubemap = register_memory("image2D.atmosphere_cubemap"_hash
								     , sizeof(Vulkan_API::Image2D));

    Vulkan_API::init_image(ATMOSPHERE_CUBEMAP_IMAGE_WIDTH
			   , ATMOSPHERE_CUBEMAP_IMAGE_HEIGHT
			   , VK_FORMAT_R8G8B8A8_UNORM
			   , VK_IMAGE_TILING_OPTIMAL
			   , VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
			   , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
			   , 6
			   , gpu
			   , cubemap.p
			   , VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);
 
    Vulkan_API::init_image_view(&cubemap->image
				, VK_FORMAT_R8G8B8A8_UNORM
				, VK_IMAGE_ASPECT_COLOR_BIT
				, gpu
				, &cubemap->image_view
				, VK_IMAGE_VIEW_TYPE_2D_ARRAY
				, 6);

    Vulkan_API::init_image_sampler(VK_FILTER_LINEAR
				   , VK_FILTER_LINEAR
				   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				   , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
				   , VK_TRUE
				   , 16
				   , VK_BORDER_COLOR_INT_OPAQUE_BLACK
				   , VK_TRUE
				   , VK_COMPARE_OP_ALWAYS
				   , VK_SAMPLER_MIPMAP_MODE_LINEAR
				   , 0.0f, 0.0f, 0.0f
				   , gpu
				   , &cubemap->image_sampler);
}

internal void
init_atmosphere_render_set_layout(Vulkan_API::GPU *gpu)
{
    R_Mem<VkDescriptorSetLayout> render_atmos_layout = register_memory("descriptor_set_layout.render_atmosphere_layout"_hash
								       , sizeof(VkDescriptorSetLayout));

    VkDescriptorSetLayoutBinding bindings[] {Vulkan_API::init_descriptor_set_layout_binding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
											    , 0
											    , 1
											    , VK_SHADER_STAGE_FRAGMENT_BIT)};
    
    Vulkan_API::init_descriptor_set_layout(Memory_Buffer_View<VkDescriptorSetLayoutBinding>{sizeof(bindings) / sizeof(bindings[0]), bindings}
					   , gpu
					   , render_atmos_layout.p);


}

internal void
init_atmosphere_render_descriptor_set(Vulkan_API::GPU *gpu)
{
    R_Mem<VkDescriptorSetLayout> render_atmos_layout = get_memory("descriptor_set_layout.render_atmosphere_layout"_hash);
    R_Mem<Vulkan_API::Descriptor_Pool> descriptor_pool = get_memory("descriptor_pool.test_descriptor_pool"_hash);
    R_Mem<Vulkan_API::Image2D> cubemap_image = get_memory("image2D.atmosphere_cubemap"_hash);
    
    // just initialize the combined sampler one
    // the uniform buffer will be already created
    R_Mem<Vulkan_API::Descriptor_Set> cubemap_set = register_memory("descriptor_set.cubemap"_hash
								    , sizeof(Vulkan_API::Descriptor_Set));

    auto *p = cubemap_set.p;
    Memory_Buffer_View<Vulkan_API::Descriptor_Set *> sets = {1, &p};
    Vulkan_API::allocate_descriptor_sets(sets
					 , Memory_Buffer_View<VkDescriptorSetLayout>{1, render_atmos_layout.p}
					 , gpu
					 , &descriptor_pool.p->pool);

    VkImageView v;
    Vulkan_API::init_image_view(&cubemap_image.p->image
				, VK_FORMAT_R8G8B8A8_UNORM
				, VK_IMAGE_ASPECT_COLOR_BIT
				, gpu
				, &v
				, VK_IMAGE_VIEW_TYPE_CUBE
				, 6);
    
    VkDescriptorImageInfo image_info = {};
    Vulkan_API::init_descriptor_set_image_info(cubemap_image.p->image_sampler
					       , v
					       , VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					       , &image_info);

    VkWriteDescriptorSet descriptor_write = {};
    Vulkan_API::init_image_descriptor_set_write(cubemap_set.p, 0, 0, 1, &image_info, &descriptor_write);

    Vulkan_API::update_descriptor_sets(Memory_Buffer_View<VkWriteDescriptorSet>{1, &descriptor_write}
				       , gpu);
}

internal void
init_atmosphere(Vulkan_API::GPU *gpu)
{
    init_atmosphere_descriptor_set_layout(gpu);

    init_atmosphere_render_set_layout(gpu);
}

void
init_scene(Scene *scene
	   , Window_Data *window
	   , Vulkan_API::State *vk
	   , Rendering::Rendering_State *rnd)
{
    scene->user_camera.set_default(window->w, window->h, window->m_x, window->m_y);

    // initialize cmdbuf semaphores and fence
    Vulkan_API::allocate_command_pool(vk->gpu.queue_families.graphics_family
				      , &vk->gpu
				      , &scene->cmdpool);

    Vulkan_API::allocate_command_buffers(&scene->cmdpool
					 , VK_COMMAND_BUFFER_LEVEL_PRIMARY
					 , &vk->gpu
					 , Memory_Buffer_View<VkCommandBuffer>{1, &scene->cmdbuf});

    Vulkan_API::init_semaphore(&vk->gpu, &scene->img_ready);
    Vulkan_API::init_semaphore(&vk->gpu, &scene->rndr_finished);
    Vulkan_API::init_fence(&vk->gpu, VK_FENCE_CREATE_SIGNALED_BIT, &scene->cpu_wait);


    R_Mem<Vulkan_API::Render_Pass> n {};
    Rendering::init_render_passes_from_json(&vk->swapchain, &vk->gpu);

    // initialize the cubemap that will be used as an attachment by the fbo for rendering the skybox
    init_atmosphere_cubemap(&vk->gpu);
    
    Rendering::init_framebuffers_from_json(&vk->swapchain, &vk->gpu);
    
    Rendering::init_descriptor_sets_and_layouts(&vk->swapchain, &vk->gpu);
    // testing atmosphere stuff right now
    init_atmosphere(&vk->gpu);
    Rendering::init_rendering_state(vk, rnd);
    init_atmosphere_render_descriptor_set(&vk->gpu);
    
    Rendering::init_pipelines_from_json(&vk->swapchain, &vk->gpu);
    
    Rendering::init_rendering_system(&vk->swapchain
				     , &vk->gpu
				     , get_memory("render_pass.deferred_render_pass"_hash));
    
    Rendering::Renderer_Init_Data rndr_d = {};
    rndr_d.rndr_id = "renderer.test_material_renderer"_hash;
    rndr_d.mtrl_max = 5;
    rndr_d.ppln_id = "pipeline.main_pipeline"_hash;
    rndr_d.mtrl_unique_data_stage_dst = VK_SHADER_STAGE_VERTEX_BIT;

    allocate_memory_buffer(rndr_d.descriptor_sets, 1);
    rndr_d.descriptor_sets[0] = "descriptor_set.test_descriptor_sets"_hash;

    Rendering::add_renderer(&rndr_d, &scene->cmdpool, &vk->gpu);


    
    //    load_renderers_from_json(&vk->gpu, &scene->cmdpool);
    
    init_scene_graph();
    // add en entity group
    Entity_Group test_g0 = construct_entity("entity.group.test0"_hash
					   , Entity::Is_Group::IS_GROUP
					   , glm::vec3(0.0f, 0.0f, 0.0f)
					   , glm::vec3(0.0f)
					   , glm::quat(0, 0, 0, 0));
    add_entity_group(test_g0
		     , get_entity_group("entity.group.top"_hash)
		     , MAX_ENTITIES_UNDER_TOP);

    // concrete representation of group test0
    Entity e = construct_entity("entity.bound_group_test0"_hash
				, Entity::Is_Group::IS_NOT_GROUP
				, glm::vec3(0.0f)
				, glm::vec3(0.0f)
				, glm::quat(0, 0, 0, 0));

    Entity_View ev = add_entity(e
				, get_entity_group("entity.group.test0"_hash));

    auto *e_ptr = get_entity(ev);

    make_entity_renderable(e_ptr
			   , get_memory("vulkan_model.test_model"_hash)
			   , "renderer.test_material_renderer"_hash);

    // create entity group that rotates around group test0
    Entity_Group rotate = construct_entity("entity.group.rotate"_hash
					   , Entity::Is_Group::IS_GROUP
					   , glm::vec3(0.0f)
					   , glm::vec3(0.0f)
					   , glm::quat(glm::vec3(0.0f))); // quat is going to change every frame

    add_entity_group(rotate
		     , get_entity_group("entity.group.test0"_hash)
		     , MAX_ENTITIES_UNDER_TOP);

    // add rotating entity
    Entity r = construct_entity("entity.rotating"_hash
				, Entity::Is_Group::IS_NOT_GROUP
				, glm::vec3(0.0f, 30.0f, 0.0f)
				, glm::vec3(0.0f)
				, glm::quat(0, 0, 0, 0));

    Entity_View rv = add_entity(r
				, get_entity_group("entity.group.rotate"_hash));

    auto *r_ptr = get_entity(rv);

    make_entity_renderable(r_ptr
			   , get_memory("vulkan_model.test_model"_hash)
			   , "renderer.test_material_renderer"_hash);

    Entity_Group rg2 = construct_entity("entity.group.rotate2"_hash
					, Entity::Is_Group::IS_GROUP
					, glm::vec3(0.0f, 0.0f, 0.0f)
					, glm::vec3(0.0f)
					, glm::quat(glm::vec3(0)));

    Entity_Group_View rg2_view = add_entity_group(rg2
						  , get_entity_group("entity.group.rotate"_hash)
						  , 5);

    Entity re2 = construct_entity("entity.rotating2"_hash
				       , Entity::Is_Group::IS_NOT_GROUP
				       , glm::vec3(0.0f, 10.0f, 0.0f)
				       , glm::vec3(0.0f)
				       , glm::quat(0, 0, 0, 0));

    Entity_View rev2 = add_entity(re2
				  , get_entity_group("entity.group.rotate2"_hash));

    auto *rev2_ptr = get_entity(rev2);
    make_entity_renderable(rev2_ptr
			   , get_memory("vulkan_model.test_model"_hash)
			   , "renderer.test_material_renderer"_hash);
}

internal void
update_ubo(u32 current_image
	   , Vulkan_API::GPU *gpu
	   , Vulkan_API::Swapchain *swapchain
	   , R_Mem<Vulkan_API::Buffer> &uniform_buffers
	   , Scene *scene)
{
    struct Uniform_Buffer_Object
    {
	alignas(16) glm::mat4 model_matrix;
	alignas(16) glm::mat4 view_matrix;
	alignas(16) glm::mat4 projection_matrix;
    };
	
    persist auto start_time = std::chrono::high_resolution_clock::now();

    auto current_time = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

    Uniform_Buffer_Object ubo = {};

    ubo.model_matrix = glm::rotate(time * glm::radians(90.0f)
				   , glm::vec3(0.0f, 0.0f, 1.0f));
    /*    ubo.view_matrix = glm::lookAt(glm::vec3(2.0f)
				  , glm::vec3(0.0f)
				  , glm::vec3(0.0f, 0.0f, 1.0f));*/
    ubo.view_matrix = scene->user_camera.v_m;
    ubo.projection_matrix = glm::perspective(glm::radians(60.0f)
					     , (float)swapchain->extent.width / (float)swapchain->extent.height
					     , 0.1f
					     , 1000.0f);

    ubo.projection_matrix[1][1] *= -1;

    Vulkan_API::Buffer &current_ubo = uniform_buffers.p[current_image];

    auto map = current_ubo.construct_map();
    map.begin(gpu);
    map.fill(Memory_Byte_Buffer{sizeof(ubo), &ubo});
    map.end(gpu);
}

internal glm::mat4
test_calculate_view_matrix(const glm::vec3 &eye
			   , const glm::vec3 &center
			   , const glm::vec3 &up)
{
    glm::vec3 f(normalize(center - eye));
    glm::vec3 s(normalize(cross(f, up)));
    glm::vec3 u(cross(s, f));

    glm::mat4 m(1.0f);
    m[0][0] = s.x;
    m[1][0] = s.y;
    m[2][0] = s.z;
    m[0][1] = u.x;
    m[1][1] = u.y;
    m[2][1] = u.z;
    m[0][2] =-f.x;
    m[1][2] =-f.y;
    m[2][2] =-f.z;
    m[3][0] =-glm::dot(s, eye);
    m[3][1] =-glm::dot(u, eye);
    m[3][2] = glm::dot(f, eye);
    return(m);
}

glm::vec3 light_pos = glm::vec3(0.0f, 10.0f, 0.0f);

internal void
record_cmd(Rendering::Rendering_State *rnd_objs
	   , Vulkan_API::State *vk
	   , Scene *scene
	   , u32 image_index, u32 frame_num
	   , VkCommandBuffer *cmdbuf)
{
    Vulkan_API::begin_command_buffer(cmdbuf, 0, nullptr);


    // render atmosphere stuff
    VkClearValue clears[] {Vulkan_API::init_clear_color_color(0, 0.0, 0.0, 0)};
    R_Mem<Vulkan_API::Render_Pass> atmos_pass = get_memory("render_pass.atmosphere_render_pass"_hash);
    R_Mem<Vulkan_API::Framebuffer> atmos_fbo = get_memory("framebuffer.atmosphere_fbo"_hash);
    R_Mem<Vulkan_API::Graphics_Pipeline> atmos_ppln = get_memory("pipeline.atmosphere_pipeline"_hash);
    Vulkan_API::command_buffer_begin_render_pass(atmos_pass.p
						 , atmos_fbo.p
						 , Vulkan_API::init_render_area({0, 0}, VkExtent2D{1000, 1000})
						 , Memory_Buffer_View<VkClearValue>{sizeof(clears) / sizeof(clears[0]), clears}
						 , VK_SUBPASS_CONTENTS_INLINE
						 , cmdbuf);
 
    Vulkan_API::command_buffer_bind_pipeline(atmos_ppln.p
					     , cmdbuf);

    struct Atmos_Push_K
    {
	alignas(16) glm::mat4 inverse_projection;
	glm::vec4 light_dir;
	glm::vec2 viewport;
    } k;

    glm::mat4 atmos_proj = glm::perspective(glm::radians(90.0f), 1000.0f / 1000.0f, 0.1f, 100000.0f);
    k.inverse_projection = glm::inverse(atmos_proj);
    k.viewport = glm::vec2(1000.0f, 1000.0f);

    k.light_dir = glm::vec4(glm::normalize(-light_pos), 1.0f);
    
    Vulkan_API::command_buffer_push_constant(&k
					     , sizeof(k)
					     , 0
					     , VK_SHADER_STAGE_FRAGMENT_BIT
					     , atmos_ppln.p
					     , cmdbuf);
    
    Vulkan_API::command_buffer_draw(cmdbuf
				    , 1, 1, 0, 0);

    Vulkan_API::command_buffer_end_render_pass(cmdbuf);


    // render the scene
    R_Mem<Vulkan_API::Render_Pass> render_pass = rnd_objs->test_render_pass;
    R_Mem<Vulkan_API::Descriptor_Set> descriptor_sets = rnd_objs->descriptor_sets;

    Rendering::update_renderers(cmdbuf
				, vk->swapchain.extent
				, image_index
				, Memory_Buffer_View<VkDescriptorSet>{1, &descriptor_sets.p[image_index].set}
				, render_pass
				, scene->user_camera.p
				, k.light_dir);
   
    
    

    

    Vulkan_API::end_command_buffer(cmdbuf);
}

internal f32 angle = 0.0f;

internal void
render_frame(Rendering::Rendering_State *rendering_objects
	     , Vulkan_API::State *vulkan_state
	     , Scene *scene)
{
    // rotate group
    angle += 0.15f;
    if (angle > 359.0f)
    {
	angle = 0.0f;
    }

    Entity_Group *rg = get_entity_group("entity.group.rotate"_hash);
    
    rg->gs_r = glm::quat(glm::radians(glm::vec3(angle, 0.0f, 0.0f)));

    Entity_Group *rg2 = get_entity_group("entity.group.rotate2"_hash);
    
    rg2->gs_r = glm::quat(glm::radians(glm::vec3(angle * 2.0f, angle * 1.0f, 0.0f)));

    update_scene_graph();
    
    persist u32 current_frame = 0;
    persist constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;
    
    Vulkan_API::wait_fences(&vulkan_state->gpu, Memory_Buffer_View<VkFence>{1, &scene->cpu_wait});

    auto next_image_data = Vulkan_API::acquire_next_image(&vulkan_state->swapchain
							  , &vulkan_state->gpu
							  , &scene->img_ready
							  , &scene->cpu_wait);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR)
    {
	// recreate swapchain
	return;
    }
    else if (next_image_data.result != VK_SUCCESS && next_image_data.result != VK_SUBOPTIMAL_KHR)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to acquire swapchain image");
    }

    update_ubo(next_image_data.image_index
	       , &vulkan_state->gpu
	       , &vulkan_state->swapchain
	       , rendering_objects->uniform_buffers
	       , scene);

    // where all the draw calls come
    record_cmd(rendering_objects, vulkan_state, scene, next_image_data.image_index, current_frame, &scene->cmdbuf);
    
    VkPipelineStageFlags wait_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;;
	    
    Vulkan_API::reset_fences(&vulkan_state->gpu, Memory_Buffer_View<VkFence>{1, &scene->cpu_wait});
    Vulkan_API::submit(Memory_Buffer_View<VkCommandBuffer>{1, &scene->cmdbuf}
                               , Memory_Buffer_View<VkSemaphore>{1, &scene->img_ready}
                               , Memory_Buffer_View<VkSemaphore>{1, &scene->rndr_finished}
                               , Memory_Buffer_View<VkPipelineStageFlags>{1, &wait_stages}
                               , &scene->cpu_wait
                               , &vulkan_state->gpu.graphics_queue);
    
    VkSemaphore signal_semaphores[] = {scene->rndr_finished};

    Vulkan_API::present(Memory_Buffer_View<VkSemaphore>{1, &scene->rndr_finished}
                                , Memory_Buffer_View<VkSwapchainKHR>{1, &vulkan_state->swapchain.swapchain}
                                , &next_image_data.image_index
                                , &vulkan_state->gpu.present_queue);
    
    if (next_image_data.result == VK_ERROR_OUT_OF_DATE_KHR || next_image_data.result == VK_SUBOPTIMAL_KHR)
    {
	// recreate swapchain
    }
    else if (next_image_data.result != VK_SUCCESS)
    {
	OUTPUT_DEBUG_LOG("%s\n", "failed to present swapchain image");
    }    

    current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void
update_scene(Scene *scene
	     , Window_Data *window
	     , Rendering::Rendering_State *rnd
	     , Vulkan_API::State *vk
	     , f32 dt)
{
    handle_input(scene, window, dt);
    scene->user_camera.compute_view();
    
    render_frame(rnd, vk, scene);
}

void
handle_input(Scene *scene
	     ,Window_Data *window
	     , f32 dt)
{
    if (window->m_moved)
    {
#define SENSITIVITY 15.0f
    
	glm::vec2 prev_mp = scene->user_camera.mp;
	glm::vec2 curr_mp = glm::vec2(window->m_x, window->m_y);

	glm::vec3 res = scene->user_camera.d;
	    
	glm::vec2 d = (curr_mp - prev_mp);

	f32 x_angle = glm::radians(-d.x) * SENSITIVITY * dt;// *elapsed;
	f32 y_angle = glm::radians(-d.y) * SENSITIVITY * dt;// *elapsed;
	res = glm::mat3(glm::rotate(x_angle, scene->user_camera.u)) * res;
	glm::vec3 rotate_y = glm::cross(res, scene->user_camera.u);
	res = glm::mat3(glm::rotate(y_angle, rotate_y)) * res;

	scene->user_camera.d = res;
	    
	scene->user_camera.mp = curr_mp;
    }

    u32 movements = 0;
    auto acc_v = [&movements](const glm::vec3 &d, glm::vec3 &dst){ ++movements; dst += d; };

    glm::vec3 d = glm::normalize(glm::vec3(scene->user_camera.d.x
					   , 0.0f
					   , scene->user_camera.d.z));
    
    glm::vec3 res = {};
	    
    if (window->key_map[GLFW_KEY_W]) acc_v(d, res);
    if (window->key_map[GLFW_KEY_A]) acc_v(-glm::cross(d, scene->user_camera.u), res);
    if (window->key_map[GLFW_KEY_S]) acc_v(-d, res);
    if (window->key_map[GLFW_KEY_D]) acc_v(glm::cross(d, scene->user_camera.u), res);
    if (window->key_map[GLFW_KEY_SPACE]) acc_v(scene->user_camera.u, res);
    if (window->key_map[GLFW_KEY_LEFT_SHIFT]) acc_v(-scene->user_camera.u, res);

    if (window->key_map[GLFW_KEY_UP]) light_pos += glm::vec3(0.0f, 0.0f, dt) * 5.0f;
    if (window->key_map[GLFW_KEY_LEFT]) light_pos += glm::vec3(-dt, 0.0f, 0.0f) * 5.0f;
    if (window->key_map[GLFW_KEY_RIGHT]) light_pos += glm::vec3(dt, 0.0f, 0.0f) * 5.0f;
    if (window->key_map[GLFW_KEY_DOWN]) light_pos += glm::vec3(0.0f, 0.0f, -dt) * 5.0f;

    if (movements > 0)
    {
	res = res * 15.0f * dt;

	scene->user_camera.p += res;
    }
}

void
destroy_scene(Scene *scene
	      , Vulkan_API::State *vk
	      , Rendering::Rendering_State *rnd)
{
    Vulkan_API::free_command_buffer(Memory_Buffer_View<VkCommandBuffer>{1, &scene->cmdbuf}, &scene->cmdpool, &vk->gpu);

    vkDestroyCommandPool(vk->gpu.logical_device
			 , scene->cmdpool
			 , nullptr);
    
    vkDestroyFence(vk->gpu.logical_device
		   , scene->cpu_wait
		   , nullptr);

    vkDestroySemaphore(vk->gpu.logical_device
		       , scene->rndr_finished
		       , nullptr);
    
    vkDestroySemaphore(vk->gpu.logical_device
		       , scene->img_ready
		       , nullptr);
}
