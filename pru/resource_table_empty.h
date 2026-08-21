/*
 * resource_table_empty.h
 * ----------------------
 * PURPOSE: The PRU loader (remoteproc) REQUIRES every firmware to carry a
 * "resource table" — a small data block describing what hardware the PRU
 * uses. Our firmware uses NO special hardware (no UART, no memory carve-out),
 * so this is the minimal EMPTY table. You do not need to understand this
 * deeply yet — just know it must be present or the loader rejects the file.
 *
 * It comes from the TI / BeagleBoard examples. Include it from your .pru0.c.
 */
#ifndef _RESOURCE_TABLE_EMPTY_H_
#define _RESOURCE_TABLE_EMPTY_H_

#include <stddef.h>
#include <rsc_types.h>

struct my_resource_table {
	uint32_t base[22];   /* ver=1, num=0, plus padding -> "empty" */
};

#pragma DATA_SECTION(resourceTable, ".resource_table")
#pragma RETAIN(resourceTable)
struct my_resource_table resourceTable = {
	{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

#endif /* _RESOURCE_TABLE_EMPTY_H_ */
