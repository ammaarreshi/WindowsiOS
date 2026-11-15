#include <fcntl.h>
#include "../core/machine.h"
#include "disk.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int hv_disk_open(HvDisk *d, const char *path, uint64_t offset, int readonly) {
    d->fd = open(path, readonly ? O_RDONLY : O_RDWR);
    if (d->fd < 0) return -1;
    struct stat st;
    if (fstat(d->fd, &st) != 0 || (uint64_t)st.st_size < offset) {
        close(d->fd);
        return -1;
    }
    d->offset = offset;
    d->size = (uint64_t)st.st_size - offset;
    d->readonly = readonly;
    return 0;
}

void hv_disk_close(HvDisk *d) {
    if (d->fd >= 0) close(d->fd);
    d->fd = -1;
}

int hv_disk_read(HvDisk *d, uint64_t off, void *buf, uint64_t len) {
    if (off > d->size || len > d->size - off) return -1;
    uint8_t *p = buf;
    while (len) {
        ssize_t n = pread(d->fd, p, len, (off_t)(d->offset + off));
        if (n <= 0) return -1;
        p += n;
        off += (uint64_t)n;
        len -= (uint64_t)n;
    }
    return 0;
}

int hv_disk_write(HvDisk *d, uint64_t off, const void *buf, uint64_t len) {
    if (d->readonly || off > d->size || len > d->size - off) return -1;
    const uint8_t *p = buf;
    while (len) {
        ssize_t n = pwrite(d->fd, p, len, (off_t)(d->offset + off));
        if (n <= 0) return -1;
        p += n;
        off += (uint64_t)n;
        len -= (uint64_t)n;
    }
    return 0;
}

static int disk_write_zeros(HvDisk *d, uint64_t off, uint64_t len) {
    static const uint8_t zero[65536];
    while (len) {
        uint64_t n = len < sizeof zero ? len : sizeof zero;
        if (hv_disk_write(d, off, zero, n) != 0) return -1;
        off += n;
        len -= n;
    }
    return 0;
}

int hv_disk_discard(HvDisk *d, uint64_t off, uint64_t len, int must_zero) {
    if (d->readonly || off > d->size || len > d->size - off) return -1;
    if (!len) return 0;
    const uint64_t blk = 4096; /* APFS allocation block; F_PUNCHHOLE wants whole blocks */
    uint64_t abs = d->offset + off, end = abs + len;
    uint64_t a = (abs + blk - 1) / blk * blk, b = end / blk * blk;
    if (a >= b) return must_zero ? disk_write_zeros(d, off, len) : 0; /* smaller than one whole block */
    if (must_zero && a > abs && disk_write_zeros(d, off, a - abs) != 0) return -1;
    if (must_zero && b < end && disk_write_zeros(d, b - d->offset, end - b) != 0) return -1;
#ifdef F_PUNCHHOLE
    fpunchhole_t ph = {0};
    ph.fp_offset = (off_t)a;
    ph.fp_length = (off_t)(b - a);
    if (fcntl(d->fd, F_PUNCHHOLE, &ph) == 0) return 0;
#endif
    /* No hole punching here. Zeros keep reads correct where they must; a plain discard is only a hint. */
    return must_zero ? disk_write_zeros(d, a - d->offset, b - a) : 0;
}

/* HV_FLUSH: 0 fsync; 1 nothing here, what was written is safe in the host's cache as long as the
   host runs; 2 fcntl(F_BARRIERFSYNC). */
int hv_disk_flush(HvDisk *d) {
    if (d->readonly) return 0;
    switch (hv_knob[HV_K_FLUSH]) {
    case 1: return 0;
    case 2: return fcntl(d->fd, F_BARRIERFSYNC) == -1 ? -1 : 0;
    default: return fsync(d->fd);
    }
}

int hv_disk_sync(HvDisk *d) {
    if (d->fd < 0 || d->readonly) return 0;
    return fsync(d->fd);
}
