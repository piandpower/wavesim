#include "wavesim/attribute.h"
#include "wavesim/intersections.h"
#include "wavesim/log.h"
#include "wavesim/memory.h"
#include "wavesim/mesh.h"
#include "wavesim/octree.h"
#include "wavesim/medium.h"
#include <string.h>
#include <assert.h>

/* ------------------------------------------------------------------------- */
/*!
 * Subdivides an AABB into smaller "cells" and iterates through every cell.
 * This function begins a new iteration. Thereafter, call iterate_cells_next()
 * to get to the next cell.
 * @param[out] cell The first cell to be iterated is written to this paramter.
 * @param[in] extents The AABB to subdivide.
 * @param[in] cell_size The x,y,z dimensions of a single cell.
 */
static void
iterate_cells_begin(wsreal_t cell[6], const wsreal_t extents[6], const wsreal_t cell_size[3])
{
    /* Begin in lower left corner... */
    memcpy(cell, extents, sizeof(wsreal_t)*3);
    memcpy(cell+3, extents, sizeof(wsreal_t)*3);
    vec3_add_vec3(cell+3, cell_size);
}

/* ------------------------------------------------------------------------- */
/*!
 * Subdivides an AABB into smaller "cells" and iterates through every cell.
 * This function continues to the next cell after having begun an iteration
 * with iterate_cells_begin().
 * @param[out] cell The next cell AABB will be written to this parameter.
 * @param[in] extents The AABB to subdivide.
 * @return Returns 1 if the cell written to "cell" is still within the extents.
 * Returns 0 if all cells have been iterated.
 */
static int
iterate_cells_next(wsreal_t cell[6], const wsreal_t extents[6])
{
    /* Advance on the Z axis */
    wsreal_t z_size = cell[5] - cell[2];
    cell[2] = cell[5];
    cell[5] += z_size;

    if (cell[5] > extents[5])
    {
        /* Reset Z axis and advance on the Y axis */
        wsreal_t y_size = cell[4] - cell[1];
        cell[1] = cell[4];
        cell[4] += y_size;
        cell[2] = extents[2];
        cell[5] = extents[2] + z_size;

        if (cell[4] > extents[4])
        {
            /* Reset Y axis and advance on the X axis */
            wsreal_t x_size = cell[3] - cell[0];
            cell[0] = cell[3];
            cell[3] += x_size;
            cell[1] = extents[1];
            cell[4] = extents[1] + y_size;

            if (cell[3] > extents[3])
            {
                /* Done */
                return 0;
            }
        }
    }

    return 1;
}

/* ------------------------------------------------------------------------- */
static int
determine_cell_attribute(attribute_t* cell_attribute,
                         const octree_t* octree,
                         const wsreal_t cell_aabb[6])
{
    wsib_t i;
    wsreal_t weights_sum;
    vector_t query_result;
    vec3_t cell_center;

    /* XXX This is super ugly, maybe add a result_construct() function that takes a mesh? */
    vector_construct(&query_result, octree->mesh->ib_size);
    if (octree_query_potential_faces(octree, &query_result, cell_aabb) < 1)
        goto octree_query_failed;

    /* Calculate the center of the AABB, required for attribute interpolation */
    vec3_copy(&cell_center, cell_aabb);
    vec3_add_vec3(cell_center.xyz, cell_aabb+3);
    vec3_mul_scalar(cell_center.xyz, 0.5);

    /*
     * Octree delivers a number of faces that *might* intersect the cell AABB,
     * but we cannot be sure until we do a proper intersection test.
     */
    attribute_set_zero(cell_attribute);
    weights_sum = 0.0;
    for (i = 0; i != (wsib_t)vector_count(&query_result) / 3; ++i)
    {
        int v;

        /* Do intersection test of face with our cell */
        const mesh_t* m = octree->mesh;
        face_t face = mesh_get_face_from_buffers(m->vb, query_result.data, m->ab,
                                                i, m->vb_type, m->ib_type);
        if (intersect_triangle_aabb_test(
                face.vertices[0].position.xyz,
                face.vertices[1].position.xyz,
                face.vertices[2].position.xyz,
                cell_aabb) == 0)
            continue; /* face doesn't intersect our cell, so ignore it */

        /*
         * Using Shepard's method, weight all vertex attributes according to
         * their distance to the cell's AABB center.
         *
         * https://en.wikipedia.org/wiki/Inverse_distance_weighting
         */
        for (v = 0; v != 3; ++v)
        {
            wsreal_t weight;
            vec3_t distance = face.vertices[v].position;
            vec3_sub_vec3(distance.xyz, cell_center.xyz);
            weight = vec3_length_squared(distance.xyz);
            if (weight == 0.0) /* catch division by 0, if the cell center is  right on top of a vertex */
            {
                *cell_attribute = face.vertices[v].attr;
                goto only_one_vertex_matters;
            }
            weight = 1.0 / weight; /* We're using p=2, since weight is the squared length */
            cell_attribute->reflection   += face.vertices[v].attr.reflection * weight;
            cell_attribute->transmission += face.vertices[v].attr.transmission * weight;
            cell_attribute->absorption   += face.vertices[v].attr.absorption * weight;
            weights_sum += weight;
        }
    }

    /* It's possible that no faces intersected, in which case we assume it's air */
    if (weights_sum == 0.0)
    {
        attribute_set_default_air(cell_attribute);
    }
    else
    {
        weights_sum = 1.0 / weights_sum;
        cell_attribute->absorption *= weights_sum;
        cell_attribute->reflection *= weights_sum;
        cell_attribute->transmission *= weights_sum;
        /* Need to normalize it so 1 = reflection + transmission + absorption */
        weights_sum = cell_attribute->reflection + cell_attribute->transmission + cell_attribute->absorption;
        weights_sum = 1.0 / weights_sum;
        cell_attribute->absorption *= weights_sum;
        cell_attribute->reflection *= weights_sum;
        cell_attribute->transmission *= weights_sum;
    }

    only_one_vertex_matters:
    octree_query_failed : vector_clear_free(&query_result);
    return -1;
}

/* ------------------------------------------------------------------------- */
wsret
medium_create(medium_t** medium)
{
    *medium = MALLOC(sizeof **medium);
    if (*medium == NULL)
        WSRET(WS_ERR_OUT_OF_MEMORY);
    medium_construct(*medium);
    return WS_OK;
}

/* ------------------------------------------------------------------------- */
void
medium_destroy(medium_t* medium)
{
    medium_destruct(medium);
    FREE(medium);
}

/* ------------------------------------------------------------------------- */
void
medium_construct(medium_t* medium)
{
    vector_construct(&medium->partitions, sizeof(medium_partition_t));
    medium->decompose = medium_decompose_systematic;
}

/* ------------------------------------------------------------------------- */
void
medium_destruct(medium_t* medium)
{
    medium_clear(medium);
}

/* ------------------------------------------------------------------------- */
void
medium_clear(medium_t* medium)
{
    VECTOR_FOR_EACH(&medium->partitions, medium_partition_t, partition)
        vector_clear_free(&partition->adcacent_partitions);
    VECTOR_END_EACH
    vector_clear_free(&medium->partitions);
}

/* ------------------------------------------------------------------------- */
wsret
medium_add_partition(medium_t* medium, const wsreal_t bb[6], wsreal_t sound_speed)
{
    medium_partition_t* partition = vector_emplace(&medium->partitions);
    if (partition == NULL)
        WSRET(WS_ERR_OUT_OF_MEMORY);

    partition->aabb = aabb(bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]);
    partition->sound_speed = sound_speed;
    vector_construct(&partition->adcacent_partitions, sizeof(int32_t));

    return 0;
}

/* ------------------------------------------------------------------------- */
void
medium_set_decomposition_method(medium_t* medium,
                                medium_decomposition_func method)
{
    medium->decompose = method;
}

/* ------------------------------------------------------------------------- */
typedef enum direction_e
{
    UP    = 0x01,
    DOWN  = 0x02,
    LEFT  = 0x04,
    RIGHT = 0x08,
    FRONT = 0x10,
    BACK  = 0x20,
    DIR_ITER_START = 0x01,
    DIR_ITER_END   = 0x40,
    ALL_DIRECTIONS = UP | DOWN | LEFT | RIGHT | FRONT | BACK,
    DIRECTION_COUNT = 6
} direction_e;
static aabb_t
get_adjacent_slice(const medium_t* medium, const wsreal_t aabb[6], direction_e direction)
{
    aabb_t adjacent;
    memcpy(adjacent.xyzxyz, aabb, sizeof(adjacent.xyzxyz));
    switch (direction)
    {
        case UP :
            AABB_AY(adjacent) = aabb[4];
            AABB_BY(adjacent) = aabb[4] + medium->grid_size.v.y;
            break;
        case DOWN:
            AABB_AY(adjacent) = aabb[1] - medium->grid_size.v.y;
            AABB_BY(adjacent) = aabb[1];
            break;
        case LEFT :
            AABB_AX(adjacent) = aabb[0] - medium->grid_size.v.x;
            AABB_BX(adjacent) = aabb[0];
            break;
        case RIGHT:
            AABB_AX(adjacent) = aabb[3];
            AABB_BX(adjacent) = aabb[3] + medium->grid_size.v.x;
            break;
        case FRONT :
            AABB_AZ(adjacent) = aabb[2] - medium->grid_size.v.z;
            AABB_BZ(adjacent) = aabb[2];
            break;
        case BACK:
            AABB_AZ(adjacent) = aabb[5];
            AABB_BZ(adjacent) = aabb[5] + medium->grid_size.v.z;
            break;
        default: break;
    }

    return adjacent;
}
static int
medium_partition_already_occupied(const medium_t* medium, const wsreal_t aabb[6])
{
    /* First make sure it's within bounds */
    int i;
    for (i = 0; i != 3; ++i)
        if (aabb[i+0] < medium->boundary.b.min.xyz[i] ||
            aabb[i+3] > medium->boundary.b.max.xyz[i])
        {
            return 1;
        }

    /* Next, check if it intersects with any other partition in the medium */
    VECTOR_FOR_EACH(&medium->partitions, medium_partition_t, partition)
        if (intersect_aabb_aabb_test(partition->aabb.xyzxyz, aabb))
            return 1;
    VECTOR_END_EACH

    return 0;
}
static wsret
decompose_systematic_recursive(medium_t* medium,
                               size_t parent_partition_idx,
                               const octree_t* octree,
                               const medium_t* mediumdef,
                               aabb_t seed)
{
    size_t direction;
    size_t occupied_direction_flags;
    vector_t potential_new_seeds;
    size_t this_partition_idx;

    /* Determine the cell type of our seed */
    attribute_t seed_attr;
    determine_cell_attribute(&seed_attr, octree, seed.xyzxyz);

    /*
     * Try to expand the seed evenly in all directions, until we hit an adjacent
     * cell that has different attributes.
     */
    vector_construct(&potential_new_seeds, sizeof(aabb_t));
    do
    {
        occupied_direction_flags = 0;
        for (direction = DIR_ITER_START; direction != DIR_ITER_END; direction <<= 1)
        {
            aabb_t slice;
            aabb_t cell;
            int slice_is_same_as_seed;

            /* Check if this direction has been flagged as occupied. If so, no
             * need to do anything */
            if (occupied_direction_flags & direction)
                continue;

            /* Calculate a slice adjacent to this seed and make sure it doesn't
             * already exist in the medium. */
            slice = get_adjacent_slice(medium, seed.xyzxyz, direction);
            if (medium_partition_already_occupied(medium, slice.xyzxyz))
            {
                occupied_direction_flags |= direction;
                continue;
            }

            /* Iterate through all cells in the slice and confirm that these cells
             * have the same attributes as our seed cell */
            slice_is_same_as_seed = 1;
            iterate_cells_begin(cell.xyzxyz, slice.xyzxyz, medium->grid_size.xyz);
            do
            {
                attribute_t cell_attribute;
                determine_cell_attribute(&cell_attribute, octree, cell.xyzxyz);
                if (attribute_is_same(&seed_attr, &cell_attribute) == 0)
                {
                    aabb_t* new_seed = vector_emplace(&potential_new_seeds);
                    if (new_seed == NULL)
                        goto ran_out_of_memory;
                    *new_seed = cell;
                    slice_is_same_as_seed = 0;
                }
            } while(iterate_cells_next(cell.xyzxyz, slice.xyzxyz));
            if (slice_is_same_as_seed == 0)
            {
                occupied_direction_flags |= direction;
                continue;
            }

            /* Since slice has the same attributes, we can merge it with our
             * seed now */
            aabb_expand_aabb(seed.xyzxyz, slice.xyzxyz);
        }
    } while (occupied_direction_flags != ALL_DIRECTIONS);

    /*
     * At this point, the seed has been expanded as much as possible without
     * intersecting existing partitions in the medium. Add it to the medium as
     * a new partition.
     */
    assert(medium_partition_already_occupied(medium, seed.xyzxyz) == 0);
    this_partition_idx = vector_count(&medium->partitions);
    if (medium_add_partition(medium, seed.xyzxyz, 1) != 0)
        goto ran_out_of_memory;
    ws_log_info(&g_ws_log, "Adding partition #%d (%f,%f,%f,%f,%f,%f)", this_partition_idx, seed.xyzxyz[0], seed.xyzxyz[1], seed.xyzxyz[2], seed.xyzxyz[3], seed.xyzxyz[4], seed.xyzxyz[5]);

    /* Add ourselves to the parent partition's adjacent list, if possible */
    if (parent_partition_idx != VECTOR_ERROR)
    {
        medium_partition_t* parent_partition = vector_get_element(&medium->partitions, parent_partition_idx);
        size_t* adjacent_partition_idx = vector_emplace(&parent_partition->adcacent_partitions);
        if (adjacent_partition_idx == NULL)
            goto ran_out_of_memory;
        *adjacent_partition_idx = this_partition_idx;
    }

    /*
     * During expansion, we tracked which cells had different attributes than
     * our own. All of these cells are potential new seeds from which we can
     * expand new partitions.
     */
    VECTOR_FOR_EACH(&potential_new_seeds, aabb_t, new_seed)
        wsret result;
        if (medium_partition_already_occupied(medium, new_seed->xyzxyz))
            continue;
        result = decompose_systematic_recursive(medium, this_partition_idx, octree, mediumdef, *new_seed);
        if (result != WS_OK)
        {
            vector_clear_free(&potential_new_seeds);
            return result;
        }
    VECTOR_END_EACH

    vector_clear_free(&potential_new_seeds);
    (void)mediumdef;
    return WS_OK;

    ran_out_of_memory: WSRET(WS_ERR_OUT_OF_MEMORY);
}
wsret
medium_decompose_systematic(medium_t* medium,
                            const octree_t* octree,
                            const medium_t* mediumdef)
{
    /* Start at the bottom, left, front corner */
    aabb_t seed = aabb(
        AABB_AX(medium->boundary),
        AABB_AY(medium->boundary),
        AABB_AZ(medium->boundary),
        AABB_AX(medium->boundary) + medium->grid_size.v.x,
        AABB_AY(medium->boundary) + medium->grid_size.v.y,
        AABB_AZ(medium->boundary) + medium->grid_size.v.z
    );
    return decompose_systematic_recursive(medium, VECTOR_ERROR, octree, mediumdef, seed);
}

/* ------------------------------------------------------------------------- */
wsret
medium_decompose_greedy_random(medium_t* medium,
                               const octree_t* octree,
                               const medium_t* mediumdef)
{
    (void)medium;
    (void)octree;
    (void)mediumdef;
    return WS_OK;
}

/* ------------------------------------------------------------------------- */
static int
integrity_checks_out(const medium_t* medium, const medium_t* mediumdef)
{
    aabb_t cell;
    int integrity = 1;
    (void)mediumdef;
    ws_log_info(&g_ws_log, "Integrity check...");

    iterate_cells_begin(cell.xyzxyz, medium->boundary.xyzxyz, medium->grid_size.xyz);
    do
    {
        if (medium_partition_already_occupied(medium, cell.xyzxyz))
        {
            integrity = 0;
            ws_log_info(&g_ws_log, "Integrity failure, missing partition at (%f,%f,%f,%f,%f,%f)", AABB_AX(cell), AABB_AY(cell), AABB_AZ(cell), AABB_BX(cell), AABB_BY(cell), AABB_BZ(cell));
        }
    } while(iterate_cells_next(cell.xyzxyz, medium->boundary.xyzxyz));

    if (integrity)
        ws_log_info(&g_ws_log, "Integrity check successful");
    return integrity;
}

/* ------------------------------------------------------------------------- */
wsret
medium_build_from_mesh(medium_t* medium,
                       const medium_t* mediumdef,
                       const mesh_t* mesh,
                       const wsreal_t grid_size[3])
{
    octree_t octree;
    wsret result;

    /* Clear partitions from last time */
    medium_clear(medium);

    /* Copy arguments into medium structure, medium building relies on
     * them */
    vec3_copy(&medium->grid_size, grid_size);
    if (mediumdef == NULL)
    {
        ws_log_info(&g_ws_log, "[warning] No medium definition was provided. Falling back to mesh AABB and default parameters.");
        medium->boundary = mesh->aabb;
    }
    else
    {
        medium->boundary = mediumdef->boundary;
    }

    /* Need an octree */
    octree_construct(&octree);
    if ((result = octree_build_from_mesh(&octree, mesh, 2)) != WS_OK)
        goto bail;

    if ((result = medium->decompose(medium, &octree, mediumdef)) != WS_OK)
        goto bail;

#ifdef DEBUG
    integrity_checks_out(medium, mediumdef);
#endif

    ws_log_info(&g_ws_log, "Decomposed mesh into %d partitions", (int)vector_count(&medium->partitions));

    bail : octree_destruct(&octree);
    return result;
}
