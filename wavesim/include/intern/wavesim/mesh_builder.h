#ifndef MESH_BUILDER_H
#define MESH_BUILDER_H

#include "wavesim/config.h"
#include "wavesim/aabb.h"
#include "wavesim/vector.h"

C_BEGIN

typedef struct mesh_t mesh_t;
typedef struct face_t face_t;

typedef struct mesh_builder_t
{
    vector_t  faces;  /* holds face_t instances */
    aabb_t    aabb;
} mesh_builder_t;

WAVESIM_PRIVATE_API mesh_builder_t*
mesh_builder_create(void);

WAVESIM_PRIVATE_API int
mesh_builder_add_face(mesh_builder_t* mb, face_t face);

WAVESIM_PRIVATE_API mesh_t*
mesh_builder_build(mesh_builder_t* mb);

WAVESIM_PRIVATE_API void
mesh_builder_destroy(mesh_builder_t* mb);

C_END

#endif /* MESH_BUILDER_H */