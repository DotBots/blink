/**
 * @file
 * @ingroup     app
 *
 * @brief       blink Node application example
 *
 * @author Geovane Fedrecheski <geovane.fedrecheski@inria.fr>
 *
 * @copyright Inria, 2024
 */
#include <nrf.h>
#include <stdio.h>

int main(void)
{
    printf("Hello blink Node\n");

    while (1) {
        __WFE();
    }
}
