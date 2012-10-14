#ifndef __RAM_SIZE_H__
#define __RAM_SIZE_H__

#include <stdio.h>

#define SPINNN 100

uint32_t * beginning;

void read() {
    int i;
    for (i = SPINNN; i >= 0; --i) {
        if (*(beginning - i) != 0xdeadbeef) {
            printf("stack size: %d words\n", i);
            return;
        }
    }
}

void write() {
    uint32_t i;
    beginning = &i;
    uint32_t * a = beginning;
    printf("beginning: %p\n", beginning);
    a--; // Dont overwrite i
    for (i = 0; i < SPINNN; ++i) {
        *a = 0xdeadbeef;
        a--;
    }
}

#endif
