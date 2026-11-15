/*
 * ETCH 3DS -- by Synth Centric
 * Run-time shims so devkitARM's newlib (built for devkitPro's system-call
 * layer) links with libgloss's Linux system calls, for tools/arm_parity.sh
 * under qemu-arm.  Single-threaded: the locks do nothing.  Not part of any
 * console build.
 */
#include <reent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/lock.h>

static struct _reent shim_reent = _REENT_INIT(shim_reent);
struct _reent *__getreent(void) { return &shim_reent; }
void __libc_lock_acquire_recursive(_LOCK_RECURSIVE_T *l) { (void)l; }
void __libc_lock_release_recursive(_LOCK_RECURSIVE_T *l) { (void)l; }
void __libc_lock_acquire(_LOCK_T *l) { (void)l; }
void __libc_lock_release(_LOCK_T *l) { (void)l; }
int __libc_lock_try_acquire_recursive(_LOCK_RECURSIVE_T *l) { (void)l; return 0; }
int _getpid_r(struct _reent *r) { (void)r; return 1; }
int _kill_r(struct _reent *r, int pid, int sig) { (void)r; (void)pid; (void)sig; return -1; }

/* newlib's reentrant system calls, onto libgloss's Linux ones */
#include <sys/stat.h>
int _write(int, const void *, size_t);
int _read(int, void *, size_t);
int _lseek(int, int, int);
int _close(int);
int _fstat(int, struct stat *);
int _open(const char *, int, int);
void *_sbrk(ptrdiff_t);
_ssize_t _write_r(struct _reent *r, int fd, const void *b, size_t n) { (void)r; return _write(fd, b, n); }
_ssize_t _read_r(struct _reent *r, int fd, void *b, size_t n) { (void)r; return _read(fd, b, n); }
_off_t _lseek_r(struct _reent *r, int fd, _off_t o, int w) { (void)r; return _lseek(fd, (int)o, w); }
int _close_r(struct _reent *r, int fd) { (void)r; return _close(fd); }
int _fstat_r(struct _reent *r, int fd, struct stat *s) { (void)r; return _fstat(fd, s); }
/* newlib's open flags are not Linux's */
int _open_r(struct _reent *r, const char *p, int f, int m)
{
    int lf = f & 3;
    (void)r;
    if (f & 0x0008) lf |= 0x400;      /* O_APPEND */
    if (f & 0x0200) lf |= 0x40;       /* O_CREAT  */
    if (f & 0x0400) lf |= 0x200;      /* O_TRUNC  */
    if (f & 0x0800) lf |= 0x80;       /* O_EXCL   */
    return _open(p, lf, m);
}
int _isatty_r(struct _reent *r, int fd) { (void)r; return fd <= 2; }
void *_sbrk_r(struct _reent *r, ptrdiff_t n) { (void)r; return _sbrk(n); }
