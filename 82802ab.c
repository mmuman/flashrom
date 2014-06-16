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

/*
 * Datasheet:
 *  - Name: Intel 82802AB/82802AC Firmware Hub (FWH)
 *  - URL: http://www.intel.com/design/chipsets/datashts/290658.htm
 *  - PDF: http://download.intel.com/design/chipsets/datashts/29065804.pdf
 *  - Order number: 290658-004
 */

#include "flash.h"
#include "chipdrivers.h"

void print_status_82802ab(uint8_t status)
{
	msg_cdbg("%s", status & 0x80 ? "Ready:" : "Busy:");
	msg_cdbg("%s", status & 0x40 ? "BE SUSPEND:" : "BE RUN/FINISH:");
	msg_cdbg("%s", status & 0x20 ? "BE ERROR:" : "BE OK:");
	msg_cdbg("%s", status & 0x10 ? "PROG ERR:" : "PROG OK:");
	msg_cdbg("%s", status & 0x8 ? "VP ERR:" : "VPP OK:");
	msg_cdbg("%s", status & 0x4 ? "PROG SUSPEND:" : "PROG RUN/FINISH:");
	msg_cdbg("%s", status & 0x2 ? "WP|TBL#|WP#,ABORT:" : "UNLOCK:");
}

static int probe_82802ab(struct flashctx *flash, struct probe_res *res, bool shifted)
{
	chipaddr bios = flash->virtual_memory;

	/* Reset to get a clean state */
	chip_writeb(flash, 0xFF, bios);
	programmer_delay(10);

	/* Enter ID mode */
	chip_writeb(flash, 0x90, bios);
	programmer_delay(10);

#if (NUM_PROBE_BYTES < 2)
#error probe_82802ab requires NUM_PROBE_BYTES to be at least 2.
#endif
	res->vals[0] = chip_readb(flash, bios + (0x00 << shifted));
	res->vals[1] = chip_readb(flash, bios + (0x01 << shifted));

	/* Leave ID mode */
	chip_writeb(flash, 0xFF, bios);

	programmer_delay(10);

	uint8_t cont[2];
	cont[0] = chip_readb(flash, bios + (0x00 << shifted));
	cont[1] = chip_readb(flash, bios + (0x01 << shifted));
	if (test_for_valid_ids(res->vals, cont, 2)) {
		res->len = 2;
		return 1;
	} else {
		res->len = 0;
		return 0;
	}
}

int probe_82802ab_shifted(struct flashctx *flash, struct probe_res *res, unsigned int ignored, const struct probe *ignored2)
{
	return probe_82802ab(flash, res, true);
}

int probe_82802ab_unshifted(struct flashctx *flash, struct probe_res *res, unsigned int ignored, const struct probe *ignored2)
{
	return probe_82802ab(flash, res, false);
}

/* FIXME: needs timeout */
uint8_t wait_82802ab(struct flashctx *flash)
{
	uint8_t status;
	chipaddr bios = flash->virtual_memory;

	chip_writeb(flash, 0x70, bios);
	if ((chip_readb(flash, bios) & 0x80) == 0) {	// it's busy
		while ((chip_readb(flash, bios) & 0x80) == 0) ;
	}

	status = chip_readb(flash, bios);

	/* Reset to get a clean state */
	chip_writeb(flash, 0xFF, bios);

	return status;
}

int unlock_82802ab(struct flashctx *flash)
{
	int i;
	//chipaddr wrprotect = flash->virtual_registers + page + 2;

	for (i = 0; i < flash->chip->total_size * 1024; i+= flash->chip->page_size)
		chip_writeb(flash, 0, flash->virtual_registers + i + 2);

	return 0;
}

int erase_block_82802ab(struct flashctx *flash, unsigned int page,
			unsigned int pagesize)
{
	chipaddr bios = flash->virtual_memory;
	uint8_t status;

	// clear status register
	chip_writeb(flash, 0x50, bios + page);

	// now start it
	chip_writeb(flash, 0x20, bios + page);
	chip_writeb(flash, 0xd0, bios + page);
	programmer_delay(10);

	// now let's see what the register is
	status = wait_82802ab(flash);
	print_status_82802ab(status);

	/* FIXME: Check the status register for errors. */
	return 0;
}

/* chunksize is 1 */
int write_82802ab(struct flashctx *flash, const uint8_t *src, unsigned int start, unsigned int len)
{
	int i;
	chipaddr dst = flash->virtual_memory + start;

	for (i = 0; i < len; i++) {
		/* transfer data from source to destination */
		chip_writeb(flash, 0x40, dst);
		chip_writeb(flash, *src++, dst++);
		wait_82802ab(flash);
	}

	/* FIXME: Ignore errors for now. */
	return 0;
}

int unlock_28f004s5(struct flashctx *flash)
{
	chipaddr bios = flash->virtual_memory;
	uint8_t mcfg, bcfg, need_unlock = 0, can_unlock = 0;
	int i;

	/* Clear status register */
	chip_writeb(flash, 0x50, bios);

	/* Read identifier codes */
	chip_writeb(flash, 0x90, bios);

	/* Read master lock-bit */
	mcfg = chip_readb(flash, bios + 0x3);
	msg_cdbg("master lock is ");
	if (mcfg) {
		msg_cdbg("locked!\n");
	} else {
		msg_cdbg("unlocked!\n");
		can_unlock = 1;
	}

	/* Read block lock-bits */
	for (i = 0; i < flash->chip->total_size * 1024; i+= (64 * 1024)) {
		bcfg = chip_readb(flash, bios + i + 2); // read block lock config
		msg_cdbg("block lock at %06x is %slocked!\n", i, bcfg ? "" : "un");
		if (bcfg) {
			need_unlock = 1;
		}
	}

	/* Reset chip */
	chip_writeb(flash, 0xFF, bios);

	/* Unlock: clear block lock-bits, if needed */
	if (can_unlock && need_unlock) {
		msg_cdbg("Unlock: ");
		chip_writeb(flash, 0x60, bios);
		chip_writeb(flash, 0xD0, bios);
		chip_writeb(flash, 0xFF, bios);
		msg_cdbg("Done!\n");
	}

	/* Error: master locked or a block is locked */
	if (!can_unlock && need_unlock) {
		msg_cerr("At least one block is locked and lockdown is active!\n");
		return -1;
	}

	return 0;
}

int unlock_lh28f008bjt(struct flashctx *flash)
{
	chipaddr bios = flash->virtual_memory;
	uint8_t mcfg, bcfg;
	uint8_t need_unlock = 0, can_unlock = 0;
	int i;

	/* Wait if chip is busy */
	wait_82802ab(flash);

	/* Read identifier codes */
	chip_writeb(flash, 0x90, bios);

	/* Read master lock-bit */
	mcfg = chip_readb(flash, bios + 0x3);
	msg_cdbg("master lock is ");
	if (mcfg) {
		msg_cdbg("locked!\n");
	} else {
		msg_cdbg("unlocked!\n");
		can_unlock = 1;
	}

	/* Read block lock-bits, 8 * 8 KB + 15 * 64 KB */
	for (i = 0; i < flash->chip->total_size * 1024;
	     i += (i >= (64 * 1024) ? 64 * 1024 : 8 * 1024)) {
		bcfg = chip_readb(flash, bios + i + 2); /* read block lock config */
		msg_cdbg("block lock at %06x is %slocked!\n", i,
			 bcfg ? "" : "un");
		if (bcfg)
			need_unlock = 1;
	}

	/* Reset chip */
	chip_writeb(flash, 0xFF, bios);

	/* Unlock: clear block lock-bits, if needed */
	if (can_unlock && need_unlock) {
		msg_cdbg("Unlock: ");
		chip_writeb(flash, 0x60, bios);
		chip_writeb(flash, 0xD0, bios);
		chip_writeb(flash, 0xFF, bios);
		wait_82802ab(flash);
		msg_cdbg("Done!\n");
	}

	/* Error: master locked or a block is locked */
	if (!can_unlock && need_unlock) {
		msg_cerr("At least one block is locked and lockdown is active!\n");
		return -1;
	}

	return 0;
}
