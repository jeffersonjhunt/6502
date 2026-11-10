/*
 * CTEST: the C target's test program (Plan C1), for test_oodos_c. Its first argument picks a case:
 *
 *   ARGS        argc, and each argv in brackets
 *   PRINTF      printf's conversions, through stdout, and sprintf's through a buffer
 *   HEAP        malloc, realloc and free: blocks that keep their bytes, and the heap's room
 *   READ        lines from stdin (fgets) until the end of the file, each echoed as got[...]
 *   PARTS       read() of stdin by more than a byte: 5 and 5 of one line, the rest by 300, a line by 300
 *   EXIT n      exit(n)
 *   BDOS        bdos(): function 9's string and function 12's version
 *   BADFD       write() to a file descriptor that isn't the console's
 *   (none)      a line, and main's return 0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "oodos.h"

static void args(int argc, char **argv)
{
    int i;
    printf("argc=%d", argc);
    for (i = 0; i < argc; ++i)
        printf(" [%s]", argv[i]);
    printf(" null=%d\n", argv[argc] == NULL);
}

static void printfs(void)
{
    static char buf[80];
    printf("%d %u %x %X %o\n", -12345, 54321u, 0xbeef, 0xbeef, 8);
    printf("%ld %lu %lx\n", -1234567L, 4000000000UL, 0x12345678UL);
    printf("%s %c|%5d|%-5d|%05d|%.3s|%%\n", "str", 'Q', 42, 42, 42, "abcdef");
    sprintf(buf, "%s=%d", "sprintf", 7);
    puts(buf);
}

static void heap(void)
{
    unsigned i, bad = 0;
    unsigned char *a = malloc(1000), *b = malloc(2000), *c;
    if (a == NULL || b == NULL)
    {
        puts("heap: no block");
        return;
    }
    memset(a, 0xA5, 1000);
    memset(b, 0x5A, 2000);
    free(a);
    c = malloc(500);
    a = realloc(b, 4000);
    for (i = 0; i < 2000; ++i)
        bad += a[i] != 0x5A;
    free(c);
    free(a);
    a = malloc(30000u);
    printf("heap: bad=%u big=%d\n", bad, a != NULL);
    free(a);
}

static void reads(void)
{
    static char line[100];
    unsigned n = 0;
    while (fgets(line, sizeof line, stdin) != NULL)
    {
        line[strcspn(line, "\n")] = 0;
        printf("got[%s]\n", line);
        ++n;
    }
    printf("lines=%u eof=%d\n", n, feof(stdin) != 0);
}

static void part(unsigned n)
{
    static char buf[300];
    int got = read(0, buf, n), i;
    printf("read(%u)=%d [", n, got);
    for (i = 0; i < got; ++i)
        putchar(buf[i] == '\n' ? '|' : buf[i]);
    puts("]");
}

static void parts(void)
{
    part(5);
    part(5);
    part(300);
    part(300);
}

static void bdoss(void)
{
    unsigned v;
    bdos(C_WRITESTR, (unsigned)"by function 9\r\n$");
    v = bdos(S_BDOSVER, 0);
    printf("version=%02X\n", v & 0xFF);
}

static void badfd(void)
{
    int r = write(5, "x", 1);
    printf("write(5)=%d ebadf=%d\n", r, errno == EBADF);
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    if (strcmp(what, "ARGS") == 0)
        args(argc, argv);
    else if (strcmp(what, "PRINTF") == 0)
        printfs();
    else if (strcmp(what, "HEAP") == 0)
        heap();
    else if (strcmp(what, "READ") == 0)
        reads();
    else if (strcmp(what, "PARTS") == 0)
        parts();
    else if (strcmp(what, "EXIT") == 0)
        exit(argc > 2 ? atoi(argv[2]) : 0);
    else if (strcmp(what, "BDOS") == 0)
        bdoss();
    else if (strcmp(what, "BADFD") == 0)
        badfd();
    else
        puts("hello from C");
    return 0;
}
