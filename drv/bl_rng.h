#ifndef __BL_RNG_H
#define __BL_RNG_H

/**
 * @brief       Read the RNG peripheral
 *
 * @{
 * @file
 * @author Alexandre Abadie <alexandre.abadie@inria.fr>
 * @copyright Inria, 2024
 * @}
 */

#include <stdint.h>

//=========================== defines ==========================================

//=========================== prototypes =======================================

/**
 * @brief Configure the random number generator (RNG)
 */
void bl_rng_init(void);

/**
 * @brief Read a random value (8 bits)
 *
 * @param[out] value address of the output value
 */
void bl_rng_read(uint8_t *value);

void bl_rng_read_range(uint8_t *value, uint8_t min, uint8_t max);

#endif // __BL_RNG_H
