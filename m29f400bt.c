/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2000 Silicon Integrated System Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "flash.h"
#include "chipdrivers.h"

/* WARNING! 
   This chip uses the standard JEDEC Addresses in 16-bit mode as word
   addresses. In byte mode, 0xAAA has to be used instead of 0x555 and
   0x555 instead of 0x2AA. Do *not* blindly replace with standard JEDEC
   functions. */

/* chunksize is 1 */
int write_m29f400bt(struct flashctx *flash, const uint8_t *src, unsigned int start, unsigned int len)
{
	int i;
	chipaddr bios = flash->virtual_memory;
	chipaddr dst = flash->virtual_memory + start;

	for (i = 0; i < len; i++) {
		chip_writeb(flash, 0xAA, bios + 0xAAA);
		chip_writeb(flash, 0x55, bios + 0x555);
		chip_writeb(flash, 0xA0, bios + 0xAAA);

		/* transfer data from source to destination */
		chip_writeb(flash, *src, dst);
		toggle_ready_jedec(flash, dst);
#if 0
		/* We only want to print something in the error case. */
		msg_cerr("Value in the flash at address 0x%lx = %#x, want %#x\n",
		     (dst - bios), chip_readb(flash, dst), *src);
#endif
		dst++;
		src++;
	}

	/* FIXME: Ignore errors for now. */
	return 0;
}

int probe_m29f400bt(struct flashctx *flash, struct probe_res *res, unsigned int ignored, const struct probe *ignored2)
{
	chipaddr bios = flash->virtual_memory;

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0x90, bios + 0xAAA);

	programmer_delay(10);

#if (NUM_PROBE_BYTES < 2)
#error probe_m29f400bt requires NUM_PROBE_BYTES to be at least 2.
#endif
	res->vals[0] = chip_readb(flash, bios);
	/* The data sheet says id2 is at (bios + 0x01) and id2 listed in
	 * flash.h does not match. It should be possible to use JEDEC probe.
	 */
	res->vals[1] = chip_readb(flash, bios + 0x02);

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0xF0, bios + 0xAAA);

	programmer_delay(10);

	uint8_t cont[2];
	cont[0] = chip_readb(flash, bios);
	cont[1] = chip_readb(flash, bios + 0x02);

	if (test_for_valid_ids(res->vals, cont, 2)) {
		res->len = 2;
		return 1;
	} else {
		res->len = 0;
		return 0;
	}
}
