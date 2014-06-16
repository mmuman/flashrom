/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2000 Silicon Integrated System Corporation
 * Copyright (C) 2012 Rudolf Marek <r.marek@assembler.cz>
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

/*
 * WARNING!
 * This chip uses the standard JEDEC addresses in 16-bit mode as word
 * addresses. In byte mode, 0xAAA has to be used instead of 0x555 and
 * 0x555 instead of 0x2AA. Do *not* blindly replace with standard JEDEC
 * functions.
 */

/* chunksize is 1 */
int write_en29lv640b(struct flashctx *flash, const uint8_t *src, unsigned int start, unsigned int len)
{
	int i;
	chipaddr bios = flash->virtual_memory;
	chipaddr dst = flash->virtual_memory + start;

	for (i = 0; i < len; i += 2) {
		chip_writeb(flash, 0xAA, bios + 0xAAA);
		chip_writeb(flash, 0x55, bios + 0x555);
		chip_writeb(flash, 0xA0, bios + 0xAAA);

		/* Transfer data from source to destination. */
		chip_writew(flash, (*src) | ((*(src + 1)) << 8 ), dst);
		toggle_ready_jedec(flash, dst);
#if 0
		/* We only want to print something in the error case. */
		msg_cerr("Value in the flash at address 0x%lx = %#x, want %#x\n",
			 (dst - bios), chip_readb(flash, dst), *src);
#endif
		dst += 2;
		src += 2;
	}

	/* FIXME: Ignore errors for now. */
	return 0;
}

int probe_en29lv640b(struct flashctx *flash, struct probe_res *res, unsigned int ignored, const struct probe *ignored2)
{
	chipaddr bios = flash->virtual_memory;

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0x90, bios + 0xAAA);

	programmer_delay(10);

#if (NUM_PROBE_BYTES < 3)
#error probe_en29lv640b requires NUM_PROBE_BYTES to be at least 3.
#endif
	res->vals[0] = chip_readb(flash, bios + 0x200);
	res->vals[1] = chip_readb(flash, bios);

	res->vals[2] = chip_readb(flash, bios + 0x02);

	chip_writeb(flash, 0xF0, bios + 0xAAA);
	programmer_delay(10);

	uint8_t cont[3];
	cont[0] = chip_readb(flash, bios + 0x200);
	cont[1] = chip_readb(flash, bios);
	cont[2] = chip_readb(flash, bios + 0x02);

	if (test_for_valid_ids(res->vals, cont, 3)) {
		res->len = 3;
		return 1;
	} else {
		res->len = 0;
		return 0;
	}
}

static int erase_chip_shifted_jedec(struct flashctx *flash)
{
	chipaddr bios = flash->virtual_memory;

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0x80, bios + 0xAAA);

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0x10, bios + 0xAAA);

	programmer_delay(10);
	toggle_ready_jedec(flash, bios);

	/* FIXME: Check the status register for errors. */
	return 0;
}

int erase_block_shifted_jedec(struct flashctx *flash, unsigned int start, unsigned int len)
{
	chipaddr bios = flash->virtual_memory;
	chipaddr dst = bios + start;

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0x80, bios + 0xAAA);

	chip_writeb(flash, 0xAA, bios + 0xAAA);
	chip_writeb(flash, 0x55, bios + 0x555);
	chip_writeb(flash, 0x30, dst);

	programmer_delay(10);
	toggle_ready_jedec(flash, bios);

	/* FIXME: Check the status register for errors. */
	return 0;
}

int erase_chip_block_shifted_jedec(struct flashctx *flash, unsigned int address, unsigned int blocklen)
{
	if ((address != 0) || (blocklen != flash->chip->total_size * 1024)) {
		msg_cerr("%s called with incorrect arguments\n", __func__);
		return -1;
	}
	return erase_chip_shifted_jedec(flash);
}
