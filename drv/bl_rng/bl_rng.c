/**
 * @file
 * @ingroup bsp_rng
 *
 * @brief  definition of the "rng" module.
 *
 * @author Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 * @copyright Inria, 2023
 */

#if defined(NRF5340_XXAA) && defined(NRF_APPLICATION)
#include "bl_rng_nrf5340_app.c"
#else
#include "bl_rng_default.c"
#endif
