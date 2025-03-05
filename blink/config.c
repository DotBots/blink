#include <nrf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "config.h"

//=========================== defines ==========================================

typedef struct {
    bl_node_type_t node_type;
} config_vars_t;

//=========================== variables ========================================

static queue_vars_t queue_vars = { 0 };

//=========================== prototypes =======================================

//=========================== public ===========================================

void bl_config_init(bl_node_type_t node_type) {
    config_vars.node_type = node_type;
}

bl_node_type_t bl_get_node_type(void) {
    return config_vars.node_type;
}
