#ifndef __CONFIG_H
#define __CONFIG_H

#include <stdint.h>
#include <stdbool.h>

//=========================== defines =========================================

typedef enum {
    BLINK_GATEWAY = 'G',
    BLINK_NODE = 'D',
} bl_node_type_t;

//=========================== prototypes ======================================

void bl_config_init(bl_node_type_t node_type);
bl_node_type_t bl_config_get_node_type(void);

#endif // __CONFIG_H
