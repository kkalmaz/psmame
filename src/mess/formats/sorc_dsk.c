/*********************************************************************

    formats/exi_dsk.c

    Exidy Sorcerer floppy-disk images

*********************************************************************/

#include <string.h>

#include "formats/sorc_dsk.h"
#include "formats/basicdsk.h"
#include "emu.h"

static FLOPPY_IDENTIFY(exi_dsk_identify)
{
	*vote = (floppy_image_size(floppy) == 332640) ? 100 : 0;
	return FLOPPY_ERROR_SUCCESS;
}

static int exi_get_heads_per_disk(floppy_image *floppy)
{
	return 1;
}

static int exi_get_tracks_per_disk(floppy_image *floppy)
{
	return 77;
}

static UINT64 exi_translate_offset(floppy_image *floppy, int track, int head, int sector)
{
	return 13+270*(16*track+sector);
}

static floperr_t get_offset(floppy_image *floppy, int head, int track, int sector, int sector_is_index, UINT64 *offset)
{
	UINT64 offs;
	/* translate the sector to a raw sector */
	if (!sector_is_index)
	{
		sector -= 1;
	}
	/* check to see if we are out of range */
	if ((head != 0) || (track < 0) || (track >= 77) || (sector < 0) || (sector >= 16))
		return FLOPPY_ERROR_SEEKERROR;

	offs = exi_translate_offset(floppy, track, head, sector);
	if (offset)
		*offset = offs;
	return FLOPPY_ERROR_SUCCESS;
}



static floperr_t internal_exi_read_sector(floppy_image *floppy, int head, int track, int sector, int sector_is_index, void *buffer, size_t buflen)
{
	UINT64 offset;
	floperr_t err;
	err = get_offset(floppy, head, track, sector, sector_is_index, &offset);
	if (err)
		return err;

	floppy_image_read(floppy, buffer, offset, buflen);
	return FLOPPY_ERROR_SUCCESS;
}



static floperr_t internal_exi_write_sector(floppy_image *floppy, int head, int track, int sector, int sector_is_index, const void *buffer, size_t buflen, int ddam)
{
	UINT64 offset;
	floperr_t err;

	err = get_offset(floppy, head, track, sector, sector_is_index, &offset);
	if (err)
		return err;

	floppy_image_write(floppy, buffer, offset, buflen);
	return FLOPPY_ERROR_SUCCESS;
}



static floperr_t exi_read_sector(floppy_image *floppy, int head, int track, int sector, void *buffer, size_t buflen)
{
	return internal_exi_read_sector(floppy, head, track, sector, FALSE, buffer, buflen);
}

static floperr_t exi_write_sector(floppy_image *floppy, int head, int track, int sector, const void *buffer, size_t buflen, int ddam)
{
	return internal_exi_write_sector(floppy, head, track, sector, FALSE, buffer, buflen, ddam);
}

static floperr_t exi_read_indexed_sector(floppy_image *floppy, int head, int track, int sector, void *buffer, size_t buflen)
{
	return internal_exi_read_sector(floppy, head, track, sector, TRUE, buffer, buflen);
}

static floperr_t exi_write_indexed_sector(floppy_image *floppy, int head, int track, int sector, const void *buffer, size_t buflen, int ddam)
{
	return internal_exi_write_sector(floppy, head, track, sector, TRUE, buffer, buflen, ddam);
}

static floperr_t exi_get_sector_length(floppy_image *floppy, int head, int track, int sector, UINT32 *sector_length)
{
	floperr_t err;
	err = get_offset(floppy, head, track, sector, FALSE, NULL);
	if (err)
		return err;

	if (sector_length) {
		*sector_length = 256;
	}
	return FLOPPY_ERROR_SUCCESS;
}



static floperr_t exi_get_indexed_sector_info(floppy_image *floppy, int head, int track, int sector_index, int *cylinder, int *side, int *sector, UINT32 *sector_length, unsigned long *flags)
{
	sector_index += 1;
	if (cylinder)
		*cylinder = track;
	if (side)
		*side = head;
	if (sector)
		*sector = sector_index;
	if (flags)
		/* TODO: read DAM or DDAM and determine flags */
		*flags = 0;
	return exi_get_sector_length(floppy, head, track, sector_index, sector_length);
}


static FLOPPY_CONSTRUCT(exi_dsk_construct)
{
	struct FloppyCallbacks *callbacks;
	callbacks = floppy_callbacks(floppy);
	callbacks->read_sector = exi_read_sector;
	callbacks->write_sector = exi_write_sector;
	callbacks->read_indexed_sector = exi_read_indexed_sector;
	callbacks->write_indexed_sector = exi_write_indexed_sector;
	callbacks->get_sector_length = exi_get_sector_length;
	callbacks->get_heads_per_disk = exi_get_heads_per_disk;
	callbacks->get_tracks_per_disk = exi_get_tracks_per_disk;
	callbacks->get_indexed_sector_info = exi_get_indexed_sector_info;

	return FLOPPY_ERROR_SUCCESS;
}



/* ----------------------------------------------------------------------- */

FLOPPY_OPTIONS_START( exidy )
	FLOPPY_OPTION( exi_dsk, "dsk", "Exidy Sorcerer floppy disk image", exi_dsk_identify, exi_dsk_construct, NULL)
FLOPPY_OPTIONS_END
