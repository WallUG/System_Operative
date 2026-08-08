#include <stdint.h>
static void sp(const char *s){ __asm__ volatile("int $0x80" : : "a"(1), "b"(s)); }
typedef struct { char dll[16]; char fn[16]; uint32_t resolved; } idata_slot_t;
idata_slot_t win32_imports[] __attribute__((section(".idata"))) = {
    { "kernel32.dll", "WriteFile", 0 },
    { "", "", 0 }
};
typedef uint32_t (*WriteFile_fn)(uint32_t, const void *, uint32_t, uint32_t *);
void _start(void)
{
    sp("A1\n");
    uint32_t w = 0;
    WriteFile_fn wf = (WriteFile_fn)win32_imports[0].resolved;
    sp("A2\n");
    if (wf)
        wf(1, "BB\n", 3, &w);
    __asm__ volatile("int $0x80" : : "a"(2));
    for (;;) ;
}
