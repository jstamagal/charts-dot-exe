#include <math.h>
#include <stdio.h>

#ifndef __SW_FPC
#error -fpc was not selected
#endif

int main(void)
{
    volatile unsigned long count;
    double x, root;

    count = 12UL;
    x = (double)count;
    root = sqrt(x * x);
    printf("COUNT %.0f SQRT %.0f\n", x, root);
    return x == 12.0 && root == 12.0 ? 0 : 1;
}
