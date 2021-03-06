#ifndef BACKTRACE_H
#define BACKTRACE_H

#define BACKTRACE_SIZE 64

#include "wavesim/config.h"

C_BEGIN

/*!
 * @brief Generates a backtrace.
 * @param[in] size The maximum number of frames to walk.
 * @return Returns an array of char* arrays.
 * @note The returned array must be freed manually with FREE(returned_array).
 */
WAVESIM_PRIVATE_API char**
get_backtrace(int* size);

C_END

#endif /* BACKTRACE_H */
