#include <nrf.h>
#include <stdio.h>
#include <stdbool.h>

#include "bl_timer_hf.h"
#include "sec.h"

//=========================== defines ==========================================

#define BLINK_APP_TIMER_DEV 1

typedef struct {
    uint8_t m1[MAX_MESSAGE_SIZE_LEN];
    uint8_t m1_len;
} edhoc_vars_t;

//=========================== variables ========================================

edhoc_vars_t node_vars = { 0 };

//=========================== prototypes =======================================

//=========================== main =============================================

int main(void)
{
    printf("Hello Blink Node\n");
    int8_t res = 0;
    uint32_t ts;

    bl_timer_hf_init(BLINK_APP_TIMER_DEV);

    bl_sec_init();

    while (1) {
        ts = bl_timer_hf_now(BLINK_APP_TIMER_DEV);
        res = bl_sec_edhoc_init();
        printf("init time: %u\n", bl_timer_hf_now(BLINK_APP_TIMER_DEV)-ts);
        if (res != 0) {
            printf("Error sec: %d\n", res);
        }

        ts = bl_timer_hf_now(BLINK_APP_TIMER_DEV);
        res = bl_sec_edhoc_prepare_m1(node_vars.m1, &node_vars.m1_len);
        printf("m1 time: %u\n", bl_timer_hf_now(BLINK_APP_TIMER_DEV)-ts);
        if (res != 0) {
            printf("Error sec: %d\n", res);
        }

        __SEV();
        __WFE();
        __WFE();
    }
}
