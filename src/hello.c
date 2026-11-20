/* Minimal arm64 Mach-O that writes a line through the BSD syscall interface and exits.
 *
 * Used as a stand-in /sbin/launchd so the kernel can finish exec'ing process 1 without a full
 * userland. Numbers are the Darwin syscall indices: exit 1, write 4.
 */
static long sys3(long n, long a, long b, long c)
{
    register long x16 __asm__("x16") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static unsigned long len(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void start(void)
{
    const char *msg = "PD1936: process 1 is running (stand-in launchd)\n";
    sys3(4, 1, (long)msg, (long)len(msg));
    sys3(1, 0, 0, 0);
    for (;;) { }
}
