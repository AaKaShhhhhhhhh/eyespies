/*
 * resource_table_empty.h
 * ----------------------
 * Minimal, self-contained EMPTY resource table for a PRU remoteproc firmware.
 *
 * WHY SELF-CONTAINED:
 * The classic TI example includes <stddef.h> and <rsc_types.h> (the latter
 * defines `struct resource_table`). Those headers live in TI's Code Generation
 * Tools / pru-software-support-package and are written for TI's PROPRIETARY
 * compiler. On Debian we build with GNU gcc-pru, which ships its OWN standard
 * C headers and does NOT understand TI's __PTRDIFF_T_TYPE__ macros. To avoid
 * the include-path hunt and the TI-vs-GNU mismatch, we define the tiny
 * 4-field resource_table struct directly here. This is exactly the layout the
 * Linux remoteproc loader expects for an empty table.
 *
 * The loader REQUIRES every PRU firmware to carry a resource table, or it
 * refuses to load ("no resource table found"). Our firmware uses no special
 * resources (no rpmsg, no carve-outs), so an empty table (num = 0) is enough.
 *
 * PORTABLE: uses a standard GCC/Clang attribute (no TI-only #pragma), so it
 * builds with GNU gcc-pru AND TI's pru-gcc.
 */
#ifndef _RESOURCE_TABLE_EMPTY_H_
#define _RESOURCE_TABLE_EMPTY_H_

#include <stdint.h>

/* The Linux remoteproc standard resource-table header:
 *   ver     : version (must be 1)
 *   num     : number of resource entries that follow (0 = empty)
 *   resv1/2 : reserved
 * With num = 0 there are no trailing offset entries. */
struct resource_table {
    uint32_t ver;
    uint32_t num;
    uint32_t resv1;
    uint32_t resv2;
};

struct my_resource_table {
    struct resource_table base;
};

/* Put the table in the ".resource_table" ELF section the loader scans for,
 * and keep it even if it looks unused. Aligned to 4 bytes. */
__attribute__((section(".resource_table"), used, aligned(4)))
struct my_resource_table resourceTable = {
    { 1, 0, 0, 0 }
};

#endif /* _RESOURCE_TABLE_EMPTY_H_ */
