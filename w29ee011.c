/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2007 Markus Boas <ryven@ryven.de>
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

#include <string.h>
#include "flash.h"
#include "chipdrivers.h"

/* According to the Winbond W29EE011, W29EE012, W29C010M, W29C011A
 * datasheets this is the only valid probe function for those chips.
 */
int probe_w29ee011(struct flashctx *flash, struct probe_res *res, unsigned int ignored, const struct probe *ignored2)
{
	static const char *w29ee011 = "W29C010(M)/W29C011A/W29EE011/W29EE012-old";
	if (!chip_to_probe || (strcmp(chip_to_probe, w29ee011) != 0)) {
		msg_cdbg("Old Winbond W29* probe method disabled because the probing sequence puts the\n"
			 "AMIC A49LF040A in a funky state. Use 'flashrom -c \"%s\"' if you have a board\n"
			 "with such a chip.\n", w29ee011);
		return 0;
	}

	chipaddr bios = flash->virtual_memory;
	/* Issue JEDEC Product ID Entry command */
	chip_writeb(flash, 0xAA, bios + 0x5555);
	programmer_delay(10);
	chip_writeb(flash, 0x55, bios + 0x2AAA);
	programmer_delay(10);
	chip_writeb(flash, 0x80, bios + 0x5555);
	programmer_delay(10);
	chip_writeb(flash, 0xAA, bios + 0x5555);
	programmer_delay(10);
	chip_writeb(flash, 0x55, bios + 0x2AAA);
	programmer_delay(10);
	chip_writeb(flash, 0x60, bios + 0x5555);
	programmer_delay(10);

#if (NUM_PROBE_BYTES < 2)
#error probe_w29ee011 requires NUM_PROBE_BYTES to be at least 2.
#endif
	/* Read product ID */
	res->vals[0] = chip_readb(flash, bios);
	res->vals[1] = chip_readb(flash, bios + 0x01);

	/* Issue JEDEC Product ID Exit command */
	chip_writeb(flash, 0xAA, bios + 0x5555);
	programmer_delay(10);
	chip_writeb(flash, 0x55, bios + 0x2AAA);
	programmer_delay(10);
	chip_writeb(flash, 0xF0, bios + 0x5555);
	programmer_delay(10);

	uint8_t cont[2];
	cont[0] = chip_readb(flash, bios);
	cont[1] = chip_readb(flash, bios + 0x01);

	if (test_for_valid_ids(res->vals, cont, 2)) {
		res->len = 2;
		return 1;
	} else {
		res->len = 0;
		return 0;
	}

	return 1;
}
