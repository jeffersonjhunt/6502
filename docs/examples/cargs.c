/*
 * CARGS: a program in C. It asks the BDOS for its version through bdos(), then prints its
 * arguments, which the CCP gives in capitals. The Programmer's Guide's example in appendix F.
 */

#include <stdio.h>

#include "oodos.h"

int main(int argc, char **argv)
{
    int i;

    printf("BDOS version %02X\n", bdos(S_BDOSVER, 0) & 0xFF);
    printf("%d arguments\n", argc - 1);
    for (i = 1; i < argc; ++i)
        printf("  %s\n", argv[i]);
    return 0;
}
