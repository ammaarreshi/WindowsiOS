#ifndef HENGEVM_DISK_H
#define HENGEVM_DISK_H

#include <stdint.h>

/* A raw disk image file, optionally starting `offset` bytes in (QEMU's vmapple
   notes say the first 16 KB of auxiliary.bin is metadata the guest never sees). */
typedef struct HvDisk {
    int fd;
    uint64_t offset;
    uint64_t size;   /* bytes visible to the guest */
    int readonly;
} HvDisk;

int hv_disk_open(HvDisk *d, const char *path, uint64_t offset, int readonly);
void hv_disk_close(HvDisk *d);
/* Return 0, or -1 on an I/O error or an access past the end. */
int hv_disk_read(HvDisk *d, uint64_t off, void *buf, uint64_t len);
int hv_disk_write(HvDisk *d, uint64_t off, const void *buf, uint64_t len);
/* Gives the range's space back to the host: whole 4 KB blocks become holes in the image file. With must_zero
   (NVMe Write Zeroes) the whole range also reads back as zeros afterwards: the partial blocks at its edges, and
   everything where holes cannot be punched, are written with zeros. Without it (NVMe Deallocate, a hint) the
   edges and, where punching fails, the range keep their data, and nothing is written. */
int hv_disk_discard(HvDisk *d, uint64_t off, uint64_t len, int must_zero);
int hv_disk_flush(HvDisk *d);
int hv_disk_sync(HvDisk *d);

#endif
