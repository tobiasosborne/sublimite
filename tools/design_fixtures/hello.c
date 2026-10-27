/* hello.c: fixed fixture for tools/design_shots.sh; never edit casually. */
#include <stdio.h>
#include <stdint.h>

#define LIMIT 16

typedef struct point { int32_t x, y; } point;

static int32_t dot(point a, point b)
{
    return a.x * b.x + a.y * b.y;
}

int main(int argc, char **argv)
{
    point origin = {0, 0}, step = {3, -4};
    const char *name = argc > 1 ? argv[1] : "world";
    for (int i = 0; i < LIMIT; i++) {
        if (dot(step, origin) > 100) break; // stop early
        origin.x += step.x;
        origin.y += step.y;
    }
    printf("hello, %s: %d %d\n", name, origin.x, origin.y);
    return 0;
}
