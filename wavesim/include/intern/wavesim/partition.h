#ifndef PARTITION_H
#define PARTITION_H

#include "wavesim/config.h"
#include "wavesim/vector.h"
#include "wavesim/aabb.h"

C_BEGIN

typedef struct mesh_t mesh_t;

typedef struct partition_t
{
    aabb_t boundary;
    vec3_t grid_size;
    vector_t areas; /* partition_area_t */
} partition_t;

WAVESIM_PRIVATE_API partition_t*
partition_create(void);

WAVESIM_PRIVATE_API void
partition_destroy(partition_t* partition);

WAVESIM_PRIVATE_API void
partition_construct(partition_t* partition);

WAVESIM_PRIVATE_API void
partition_destruct(partition_t* partition);

WAVESIM_PRIVATE_API void
partition_clear(partition_t* partition);

WAVESIM_PRIVATE_API int
partition_add_area(partition_t* partition, const WS_REAL bounding_box[6], WS_REAL sound_speed);

WAVESIM_PRIVATE_API int
partition_build_from_mesh(partition_t* partition,
                          const partition_t* medium,
                          const mesh_t* mesh,
                          vec3_t grid_size);

C_END

#endif /* PARTITION_H */
