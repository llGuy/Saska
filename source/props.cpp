#include "props.hpp"
#include "graphics.hpp"


struct tree_mesh_t
{
    mesh_t trunc_mesh;
};

// 10 variations of trees
static tree_mesh_t tree_meshes[10];
static model_t tree_trunc_model_info;
// Create custom material queue for instanced rendering
static gpu_material_submission_queue_t tree_queue;


static void s_tree_renderer_init()
{
    tree_meshes[0].trunc_mesh = load_mesh(mesh_file_format_t::CUSTOM_MESH, "models/props/tree_trunc0.mesh_custom", get_global_command_pool());

    create_mesh_raw_buffer_list(&tree_meshes[0].trunc_mesh);
    tree_trunc_model_info = make_mesh_attribute_and_binding_information(&tree_meshes[0].trunc_mesh);

    tree_queue = make_gpu_material_submission_queue(30, VK_SHADER_STAGE_VERTEX_BIT, VK_COMMAND_BUFFER_LEVEL_PRIMARY, get_global_command_pool());
}


struct tree_t
{
    vector3_t position;
    // Will dictate the rotation of the tree in world space
    vector3_t normal;

    struct pushk_t
    {
        matrix4_t model_matrix;
    };
};

static tree_t trees[30];

static void s_trees_init()
{
    trees[0].position = vector3_t(0.0f);
}


void initialize_props()
{
    s_tree_renderer_init();
    s_trees_init();
}


void render_props(gpu_command_queue_t *queue)
{
    
}
