/*
 * resource_table_empty.h
 * ----------------------
 * PURPOSE: The PRU loader (remoteproc) REQUIRES every firmware to carry a
 * "resource table" -- a small data block describing what hardware the PRU
 * uses. Our firmware uses NO special hardware (no rpmsg, no memory carve-out),
 * so this is the minimal EMPTY table. It must be present or the loader
 * rejects the file with "no resource table found".
 *
 * It comes from the TI / BeagleBoard examples. Include it from your .pru0.c.
 *
 * PORTABLE: uses a standard GCC/Clang attribute instead of TI-only
 * #pragma DATA_SECTION / #pragma RETAIN, so it builds with both GNU pru-gcc
 * (Debian's gcc-pru / pru-software-support-package) AND TI's pru-gcc.
 */
#ifndef _RESOURCE_TABLE_EMPTY_H_
#define _RESOURCE_TABLE_EMPTY_H_

#include <stddef.h>
#include <rsc_types.h>

struct my_resource_table {
	struct resource_table base;
};

/* ver = 1, num = 0 -> an empty table (no resources). */
__attribute__((section(".resource_table"), used, aligned(4)))
struct my_resource_table resourceTable = {
	{ 1, 0, 0, 0 }
};

#endif /* _RESOURCE_TABLE_EMPTY_H_ */
