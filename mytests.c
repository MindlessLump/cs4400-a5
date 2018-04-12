#include <stdio.h>
#include <string.h>

#include "mm.h"

int main() {
    mm_init();
    void *ptr1 = mm_malloc(400);
    void *ptr2 = mm_malloc(300);
    mm_free(ptr1);
    mm_free(ptr2);
    void *ptr3 = mm_malloc(800);
    mm_init();
}
