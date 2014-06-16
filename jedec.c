/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2000 Silicon Integrated System Corporation
 * Copyright (C) 2006 Giampiero Giancipoli <gianci@email.it>
 * Copyright (C) 2006 coresystems GmbH <info@coresystems.de>
 * Copyright (C) 2007 Carl-Daniel Hailfinger
 * Copyright (C) 2009 Sean Nelson <audiohacked@gmail.com>
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

#define MAX_REFLASH_TRIES 0x10
#define MASK_FULL 0xffff
#define MASK_2AA 0x7ff
#define MASK_AAA 0xfff

/* Check one byte for odd parity */
uint8_t oddparity(uint8_t val)
{
	val = (val ^ (val >> 4)) & 0xf;
	val = (val ^ (val >> 2)) & 0x3;
	return (val ^ (val >> 1)) & 0x1;
}

/* Looks for values in a different from all zeroes and all ones. If b is non-null test_for_valid_ids
 * additionally compares bytes in a and b respectively. Can be be used on values read before and after exiting
 * ID mode: Unequal values indicate with high certainty that the write commands enabling and disabling ID mode
 * were received and understood by the chip. */
bool test_for_valid_ids(uint8_t *a, uint8_t *b, unsigned int len)
{
	unsigned int i;
	bool equal = true;
	bool valid = false;
	for (i = 0; i < len; i++) {
		if (b != NULL) {
			if (a[i] != b[i])
				equal = false;
			else
				msg_cspew("Byte #%d is equal (0x%02x).\n", i, a[i]);
		}
		if (a[i] != 0x00 && a[i] != 0xFF)
			valid = true;
	}
	if (b != NULL && equal && valid)
		msg_cdbg("IDs are equal to normal flash content.");
	return valid;
}

static void toggle_ready_jedec_common(const struct flashctx *flash, chipaddr dst, unsigned int delay)
{
	unsigned int i = 0;
	uint8_t tmp1, tmp2;

	tmp1 = chip_readb(flash, dst) & 0x40;

	while (i++ < 0xFFFFFFF) {
		if (delay)
			programmer_delay(delay);
		tmp2 = chip_readb(flash, dst) & 0x40;
		if (tmp1 == tmp2) {
			break;
		}
		tmp1 = tmp2;
	}
	if (i > 0x100000)
		msg_cdbg("%s: excessive loops, i=0x%x\n", __func__, i);
}

void toggle_ready_jedec(const struct flashctx *flash, chipaddr dst)
{
	toggle_ready_jedec_common(flash, dst, 0);
}

/* Some chips require a minimum delay between toggle bit reads.
 * The Winbond W39V040C wants 50 ms between reads on sector erase toggle,
 * but experiments show that 2 ms are already enough. Pick a safety factor
 * of 4 and use an 8 ms delay.
 * Given that erase is slow on all chips, it is recommended to use 
 * toggle_ready_jedec_slow in erase functions.
 */
static void toggle_ready_jedec_slow(const struct flashctx *flash, chipaddr dst)
{
	toggle_ready_jedec_common(flash, dst, 8 * 1000);
}

void data_polling_jedec(const struct flashctx *flash, chipaddr dst,
			uint8_t data)
{
	unsigned int i = 0;
	uint8_t tmp;

	data &= 0x80;

	while (i++ < 0xFFFFFFF) {
		tmp = chip_readb(flash, dst) & 0x80;
		if (tmp == data) {
			break;
		}
	}
	if (i > 0x100000)
		msg_cdbg("%s: excessive loops, i=0x%x\n", __func__, i);
}

static unsigned int getaddrmask(const struct flashchip *chip)
{
	switch (chip->feature_bits & FEATURE_ADDR_MASK) {
	case FEATURE_ADDR_FULL:
		return MASK_FULL;
		break;
	case FEATURE_ADDR_2AA:
		return MASK_2AA;
		break;
	case FEATURE_ADDR_AAA:
		return MASK_AAA;
		break;
	default:
		msg_cerr("%s called with unknown mask\n", __func__);
		return 0;
		break;
	}
}

static void start_program_jedec_common(const struct flashctx *flash, unsigned int mask)
{
	chipaddr bios = flash->virtual_memory;
	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	chip_writeb(flash, 0xA0, bios + (0x5555 & mask));
}

static int probe_jedec(struct flashctx *flash, struct probe_res *res, unsigned int mask, bool long_reset)
{
	unsigned int delay = 10000;
	chipaddr bios = flash->virtual_memory;

	/* Earlier probes might have been too fast for the chip to enter ID
	 * mode completely. Allow the chip to finish this before seeing a
	 * reset command.
	 */
	if (delay)
		programmer_delay(delay);
	/* Reset chip to a clean slate */
	if (long_reset)
	{
		chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
		if (delay)
			programmer_delay(10);
		chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
		if (delay)
			programmer_delay(10);
	}
	chip_writeb(flash, 0xF0, bios + (0x5555 & mask));
	if (delay)
		programmer_delay(delay);

	/* Issue JEDEC Product ID Entry command */
	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	if (delay)
		programmer_delay(10);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	if (delay)
		programmer_delay(10);
	chip_writeb(flash, 0x90, bios + (0x5555 & mask));
	if (delay)
		programmer_delay(delay);

#if (NUM_PROBE_BYTES < 4)
#error probe_jedec requires NUM_PROBE_BYTES to be at least 4.
#endif
	unsigned int idx = 0;
	/* Read manufacturer ID */
	res->vals[idx++] = chip_readb(flash, bios);
	/* Check if it is a continuation ID, this (and the one below) should be a while loop. */
	if (res->vals[idx] == 0x7F) {
		res->vals[idx++] = chip_readb(flash, bios + 0x100);
	}

	/* Read model ID */
	res->vals[idx++] = chip_readb(flash, bios + 0x01);
	if (res->vals[idx] == 0x7F) {
		res->vals[idx++] = chip_readb(flash, bios + 0x101);
	}

	/* Issue JEDEC Product ID Exit command */
	if (long_reset)
	{
		chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
		if (delay)
			programmer_delay(10);
		chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
		if (delay)
			programmer_delay(10);
	}
	chip_writeb(flash, 0xF0, bios + (0x5555 & mask));
	if (delay)
		programmer_delay(delay);

	uint8_t cont[idx];
	unsigned cont_idx = 0;
	/* Read the product ID location again. We should now see normal flash contents. */
	cont[cont_idx] = chip_readb(flash, bios);

	/* Check if it is a continuation ID, this should be a while loop. */
	if (cont[cont_idx++] == 0x7F) {
		cont[cont_idx++] = chip_readb(flash, bios + 0x100);
	}
	cont[cont_idx] = chip_readb(flash, bios + 0x01);
	if (cont[cont_idx++] == 0x7F) {
		cont[cont_idx++] = chip_readb(flash, bios + 0x101);
	}

	if (test_for_valid_ids(res->vals, cont, idx)) {
		res->len = idx;
		return 1;
	} else {
		res->len = 0;
		return 0;
	}

	return 1;
}

int probe_jedec_longreset(struct flashctx *flash, struct probe_res *res, unsigned int res_len, const struct probe *p)
{
	return probe_jedec(flash, res, MASK_FULL, true);
}

int probe_jedec_shortreset_full(struct flashctx *flash, struct probe_res *res, unsigned int res_len, const struct probe *p)
{
	return probe_jedec(flash, res, MASK_FULL, false);
}

int probe_jedec_shortreset_full_384(struct flashctx *flash, struct probe_res *res, unsigned int res_len, const struct probe *p)
{
	return probe_jedec(flash, res, MASK_FULL, false);
}

int probe_jedec_shortreset_2aa(struct flashctx *flash, struct probe_res *res, unsigned int res_len, const struct probe *p)
{
	return probe_jedec(flash, res, MASK_2AA, false);
}

int probe_jedec_shortreset_aaa(struct flashctx *flash, struct probe_res *res, unsigned int res_len, const struct probe *p)
{
	return probe_jedec(flash, res, MASK_AAA, false);
}

static int erase_sector_jedec_common(struct flashctx *flash, unsigned int page,
				     unsigned int pagesize, unsigned int mask)
{
	chipaddr bios = flash->virtual_memory;
	unsigned int delay_us = 0;
	if (flash->chip->feature_bits & FEATURE_SLOW_ERASE_CMDS)
		delay_us = 10;

	/*  Issue the Sector Erase command   */
	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x80, bios + (0x5555 & mask));
	programmer_delay(delay_us);

	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x30, bios + page);
	programmer_delay(delay_us);

	/* wait for Toggle bit ready         */
	toggle_ready_jedec_slow(flash, bios);

	/* FIXME: Check the status register for errors. */
	return 0;
}

static int erase_block_jedec_common(struct flashctx *flash, unsigned int block,
				    unsigned int blocksize, unsigned int mask)
{
	chipaddr bios = flash->virtual_memory;
	unsigned int delay_us = 0;
	if (flash->chip->feature_bits & FEATURE_SLOW_ERASE_CMDS)
		delay_us = 10;

	/*  Issue the Sector Erase command   */
	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x80, bios + (0x5555 & mask));
	programmer_delay(delay_us);

	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x50, bios + block);
	programmer_delay(delay_us);

	/* wait for Toggle bit ready         */
	toggle_ready_jedec_slow(flash, bios);

	/* FIXME: Check the status register for errors. */
	return 0;
}

static int erase_chip_jedec_common(struct flashctx *flash, unsigned int mask)
{
	chipaddr bios = flash->virtual_memory;
	unsigned int delay_us = 0;
	if (flash->chip->feature_bits & FEATURE_SLOW_ERASE_CMDS)
		delay_us = 10;

	/*  Issue the JEDEC Chip Erase command   */
	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x80, bios + (0x5555 & mask));
	programmer_delay(delay_us);

	chip_writeb(flash, 0xAA, bios + (0x5555 & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x55, bios + (0x2AAA & mask));
	programmer_delay(delay_us);
	chip_writeb(flash, 0x10, bios + (0x5555 & mask));
	programmer_delay(delay_us);

	toggle_ready_jedec_slow(flash, bios);

	/* FIXME: Check the status register for errors. */
	return 0;
}

static int write_byte_program_jedec_common(const struct flashctx *flash, const uint8_t *src,
					   chipaddr dst, unsigned int mask)
{
	int tried = 0, failed = 0;
	chipaddr bios = flash->virtual_memory;

	/* If the data is 0xFF, don't program it and don't complain. */
	if (*src == 0xFF) {
		return 0;
	}

retry:
	/* Issue JEDEC Byte Program command */
	start_program_jedec_common(flash, mask);

	/* transfer data from source to destination */
	chip_writeb(flash, *src, dst);
	toggle_ready_jedec(flash, bios);

	if (chip_readb(flash, dst) != *src && tried++ < MAX_REFLASH_TRIES) {
		goto retry;
	}

	if (tried >= MAX_REFLASH_TRIES)
		failed = 1;

	return failed;
}

/* chunksize is 1 */
int write_jedec_1(struct flashctx *flash, const uint8_t *src, unsigned int start,
		  unsigned int len)
{
	int i, failed = 0;
	chipaddr dst = flash->virtual_memory + start;
	chipaddr olddst;
	unsigned int mask;

	mask = getaddrmask(flash->chip);

	olddst = dst;
	for (i = 0; i < len; i++) {
		if (write_byte_program_jedec_common(flash, src, dst, mask))
			failed = 1;
		dst++, src++;
	}
	if (failed)
		msg_cerr(" writing sector at 0x%" PRIxPTR " failed!\n", olddst);

	return failed;
}

static int write_page_write_jedec_common(struct flashctx *flash, const uint8_t *src,
					 unsigned int start, unsigned int page_size)
{
	int i, tried = 0, failed;
	const uint8_t *s = src;
	chipaddr bios = flash->virtual_memory;
	chipaddr dst = bios + start;
	chipaddr d = dst;
	unsigned int mask;

	mask = getaddrmask(flash->chip);

retry:
	/* Issue JEDEC Start Program command */
	start_program_jedec_common(flash, mask);

	/* transfer data from source to destination */
	for (i = 0; i < page_size; i++) {
		/* If the data is 0xFF, don't program it */
		if (*src != 0xFF)
			chip_writeb(flash, *src, dst);
		dst++;
		src++;
	}

	toggle_ready_jedec(flash, dst - 1);

	dst = d;
	src = s;
	failed = verify_range(flash, src, start, page_size);

	if (failed && tried++ < MAX_REFLASH_TRIES) {
		msg_cerr("retrying.\n");
		goto retry;
	}
	if (failed) {
		msg_cerr(" page 0x%" PRIxPTR " failed!\n", (d - bios) / page_size);
	}
	return failed;
}

/* chunksize is page_size */
/*
 * Write a part of the flash chip.
 * FIXME: Use the chunk code from Michael Karcher instead.
 * This function is a slightly modified copy of spi_write_chunked.
 * Each page is written separately in chunks with a maximum size of chunksize.
 */
int write_jedec(struct flashctx *flash, const uint8_t *buf, unsigned int start,
		int unsigned len)
{
	unsigned int i, starthere, lenhere;
	/* FIXME: page_size is the wrong variable. We need max_writechunk_size
	 * in struct flashctx to do this properly. All chips using
	 * write_jedec have page_size set to max_writechunk_size, so
	 * we're OK for now.
	 */
	unsigned int page_size = flash->chip->page_size;

	/* Warning: This loop has a very unusual condition and body.
	 * The loop needs to go through each page with at least one affected
	 * byte. The lowest page number is (start / page_size) since that
	 * division rounds down. The highest page number we want is the page
	 * where the last byte of the range lives. That last byte has the
	 * address (start + len - 1), thus the highest page number is
	 * (start + len - 1) / page_size. Since we want to include that last
	 * page as well, the loop condition uses <=.
	 */
	for (i = start / page_size; i <= (start + len - 1) / page_size; i++) {
		/* Byte position of the first byte in the range in this page. */
		/* starthere is an offset to the base address of the chip. */
		starthere = max(start, i * page_size);
		/* Length of bytes in the range in this page. */
		lenhere = min(start + len, (i + 1) * page_size) - starthere;

		if (write_page_write_jedec_common(flash, buf + starthere - start, starthere, lenhere))
			return 1;
	}

	return 0;
}

/* erase chip with block_erase() prototype */
int erase_chip_block_jedec(struct flashctx *flash, unsigned int addr,
			   unsigned int blocksize)
{
	unsigned int mask;

	mask = getaddrmask(flash->chip);
	if ((addr != 0) || (blocksize != flash->chip->total_size * 1024)) {
		msg_cerr("%s called with incorrect arguments\n",
			__func__);
		return -1;
	}
	return erase_chip_jedec_common(flash, mask);
}

int erase_sector_jedec(struct flashctx *flash, unsigned int page,
		       unsigned int size)
{
	unsigned int mask;

	mask = getaddrmask(flash->chip);
	return erase_sector_jedec_common(flash, page, size, mask);
}

int erase_block_jedec(struct flashctx *flash, unsigned int page,
		      unsigned int size)
{
	unsigned int mask;

	mask = getaddrmask(flash->chip);
	return erase_block_jedec_common(flash, page, size, mask);
}

int erase_chip_jedec(struct flashctx *flash)
{
	unsigned int mask;

	mask = getaddrmask(flash->chip);
	return erase_chip_jedec_common(flash, mask);
}
