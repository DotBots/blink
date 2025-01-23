/**
 * @file
 * @ingroup     app
 *
 * @brief       blink Gateway application example
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */
#include <nrf.h>
#include <stdio.h>

int main(void)
{
    printf("Hello blink Gateway\n");

    while (1) {
        __WFE();
    }
}
