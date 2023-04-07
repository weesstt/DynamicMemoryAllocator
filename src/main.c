#include <stdio.h>
#include "sfmm.h"
#include <errno.h>



int main(int argc, char const *argv[]) {
    int x = 10;
    int *y = malloc(sizeof(int));
    *y = 10;
    printf("x=%p, y = %p, truth:%d", &x, y, x > y);
    return EXIT_SUCCESS;
}
