#include "wavesim/octree.h"
#include "wavesim/memory.h"
#include "wavesim/mesh.h"
#include "wavesim/intersections.h"
#include "string.h"

#define IDX(x, y, z) \
    x*4 + y*2 + z

#define CX(idx) \
    (idx >= 4 ? 1 : 0)

#define CY(idx) \
    ((idx >= 2 && idx < 4) || idx >= 6 ? 1 : 0)

#define CZ(idx) \
    (idx % 2 ? 1 : 0)

/* ------------------------------------------------------------------------- */
octree_t*
octree_create(void)
{
    octree_t* octree = MALLOC(sizeof *octree);
    if (octree == NULL)
        OUT_OF_MEMORY(NULL);
    octree_construct(octree);
    return octree;
}

/* ------------------------------------------------------------------------- */
void
octree_construct(octree_t* octree)
{
    octree->mesh = NULL;
    octree->root = NULL;
}

/* ------------------------------------------------------------------------- */
void
octree_destruct(octree_t* octree)
{
    octree_clear(octree);
}

/* ------------------------------------------------------------------------- */
void
octree_destroy(octree_t* octree)
{
    octree_destruct(octree);
    FREE(octree);
}

/* ------------------------------------------------------------------------- */
static void
node_destroy(octree_node_t* node)
{
    /* Destroy all children recursively */
    if (node->children != NULL)
    {
        int i;
        for (i = 0; i != 8; ++i)
        {
            node_destroy(&node->children[i]);
        }
        FREE(node->children);
    }

    /* Clear list of faces, UNLESS we're the root node */
    if (node->parent != NULL)
        vector_clear_free(&node->index_buffer);
}

/* ------------------------------------------------------------------------- */
static octree_node_t*
node_create_children(octree_node_t* parent, const mesh_t* mesh)
{
    int i;
    aabb_t bb_parent;
    vec3_t bb_dims;

    octree_node_t* children = MALLOC(sizeof(octree_node_t) * 8);
    if (children == NULL)
        OUT_OF_MEMORY(NULL);

    /* Initialize children fields */
    for (i = 0; i != 8; ++i)
    {
        children[i].children = NULL;
        children[i].parent = parent;
        vector_construct(&children[i].index_buffer, mesh->ib_size);
    }

    /* Calculate child bounding boxes */
    if (parent == NULL)
    {
        bb_parent = mesh->aabb;
        bb_dims = AABB_DIMS(mesh->aabb);
    }
    else
    {
        bb_parent = parent->aabb;
        bb_dims = AABB_DIMS(parent->aabb);
    }
    for (i = 0; i != 8; ++i)
    {
        AABB_AX(children[i].aabb) = AABB_AX(bb_parent) + (CX(i) + 0) * bb_dims.data.x;
        AABB_BX(children[i].aabb) = AABB_AX(bb_parent) + (CX(i) + 1) * bb_dims.data.x;
        AABB_AY(children[i].aabb) = AABB_AY(bb_parent) + (CY(i) + 0) * bb_dims.data.y;
        AABB_BY(children[i].aabb) = AABB_AY(bb_parent) + (CY(i) + 1) * bb_dims.data.y;
        AABB_AZ(children[i].aabb) = AABB_AZ(bb_parent) + (CZ(i) + 0) * bb_dims.data.z;
        AABB_BZ(children[i].aabb) = AABB_AZ(bb_parent) + (CZ(i) + 1) * bb_dims.data.z;
    }

    return children;
}

/* ------------------------------------------------------------------------- */
void
octree_clear(octree_t* octree)
{
    if (octree->root == NULL)
        return;

    /* destroy all children */
    node_destroy(octree->root);
    octree->root = NULL;

    /* release reference to mesh object */
    octree->mesh = NULL;
}

/* ------------------------------------------------------------------------- */
int
octree_subdivide(octree_t* octree, octree_node_t* node)
{
    /* Subdivide is only valid for leaf nodes */
    if (node && node->children != NULL)
        return 1;

    /* If node is NULL then we have to create a new root node for the octree */
    if (node == NULL)
    {
        if ((octree->root = node_create_children(NULL, octree->mesh)) == NULL)
            return -1;
    }
    else
    {
        if ((node->children = node_create_children(node, octree->mesh)) == NULL)
            return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------------- */
static intptr_t
octree_build_from_mesh_recursive(octree_t* octree, octree_node_t* node, vec3_t smallest_subdivision)
{
    unsigned int i;
    const mesh_t* mesh = octree->mesh;

    if (node->parent != NULL)
    {
        for (i = 0; i != vector_count(&node->parent->index_buffer); i += 3)
        {
            /*
            * Here we use the index buffer of the *parent octree node* to look up
            * 3 vertices in the mesh to form a face. This is dangerous and only
            * works if the octree node's index buffer was initialized with the
            * same datatype as the mesh's index buffer (which, if nothing broke,
            * should always be the case).
            */
            WS_IB indices[3] = {
                mesh_get_index_from_buffer(node->parent->index_buffer.data, i + 0, mesh->ib_type),
                mesh_get_index_from_buffer(node->parent->index_buffer.data, i + 1, mesh->ib_type),
                mesh_get_index_from_buffer(node->parent->index_buffer.data, i + 2, mesh->ib_type)
            };
            vec3_t face[3] = {
                mesh_get_vertex_position_from_buffer(mesh->vb, indices[0], mesh->vb_type),
                mesh_get_vertex_position_from_buffer(mesh->vb, indices[1], mesh->vb_type),
                mesh_get_vertex_position_from_buffer(mesh->vb, indices[2], mesh->vb_type)
            };

            /*
            * Add face to this node's index buffer if its bounding box intersects
            * with the node's bounding box
            */
            aabb_t face_bb = aabb_from_3_points(face[0].xyz, face[1].xyz, face[2].xyz);
            if (intersect_aabb_aabb_test(node->aabb.xyzxyz, face_bb.xyzxyz) == 1)
            {
                if (vector_push(&node->index_buffer, &indices[0]) == VECTOR_ERROR) return -1;
                if (vector_push(&node->index_buffer, &indices[1]) == VECTOR_ERROR) return -1;
                if (vector_push(&node->index_buffer, &indices[2]) == VECTOR_ERROR) return -1;
            }
        }
    }

    /* Stop subdividing when we reach one face */
    if (vector_count(&node->index_buffer) <= 3)
        return 0;

    /* Abort if below smallest division */
    for (i = 0; i != 3; ++i)
        if (node->aabb.data.b.xyz[i] - node->aabb.data.a.xyz[i] < smallest_subdivision.xyz[i])
            return 0;

    if (octree_subdivide(octree, node) < 0)
        return -1;

    for (int i = 0; i != 8; ++i)
        octree_build_from_mesh_recursive(octree, &node->children[i], smallest_subdivision);
    return 0;
}
intptr_t
octree_build_from_mesh(octree_t* octree, const mesh_t* mesh, vec3_t smallest_subdivision)
{
    int i, total_faces;

    (void)i;
    (void)total_faces;

    /* Clear old octree if it exists */
    octree_clear(octree);

    /*
     * Create root
     * WARNING: This is a pretty terrible hack; we manually set up the index
     * buffer vector to point to the mesh's index buffer, instead of allocating
     * and copying all of the indices. The root node's IB is identical to the
     * mesh's IB and we save memory by avoiding a copy.
     */
    octree->mesh = mesh;
    octree->root = node_create_children(NULL, octree->mesh);
    octree->root->index_buffer.capacity = 0;
    octree->root->index_buffer.count = mesh->ib_count;
    octree->root->index_buffer.element_size = mesh->ib_size;
    octree->root->index_buffer.data = mesh->ib;

    /* Handle empty meshes */
    if (mesh_face_count(octree->mesh) == 0)
        return 0;

    if (octree_build_from_mesh_recursive(octree, octree->root, smallest_subdivision) < 0)
            return -1;
    return 0;
}

/* ------------------------------------------------------------------------- */
const vector_t*
octree_intersect_aabb(const octree_t* octree, aabb_t aabb)
{
    (void)octree;
    (void)aabb;
    return NULL;
}