#ifndef WAVESIM_OBJ_H
#define WAVESIM_OBJ_H

#include "wavesim/config.h"
#include "wavesim/btree.h"
#include <stdio.h>

C_BEGIN

typedef struct mesh_t mesh_t;
typedef struct medium_t medium_t;
typedef struct octree_t octree_t;

typedef struct obj_exporter_t
{
    FILE*    fp;
    uint32_t index_counter;
    btree_t  vi_map;
} obj_exporter_t;

/* --- Import --- */

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_import_mesh(const char* filename, mesh_t* mesh);

/* --- Export --- */

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_export_medium(const char* filename, const medium_t* mesh);

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_export_octree(const char* filename, const octree_t* octree);

/* --- Basic functionality used in all export/import functions --- */

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_exporter_open(obj_exporter_t* exporter, const char* filename);

WAVESIM_PUBLIC_API void
obj_exporter_close(obj_exporter_t* exporter);

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_write_vertex(obj_exporter_t* exporter, const wsreal_t vert[3]);

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_write_aabb_vertices(obj_exporter_t* exporter, const wsreal_t aabb[6]);

WAVESIM_PUBLIC_API wsret WS_WARN_UNUSED
obj_write_aabb_indices(obj_exporter_t* exporter, const wsreal_t aabb[6]);

C_END

#endif /* WAVESIM_OBJ_H */
