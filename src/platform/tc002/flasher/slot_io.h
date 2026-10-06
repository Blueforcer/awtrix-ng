#pragma once

#include "../contract/release_slot.h"
#include "flash_write.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The slot of the res partition (release_slot.h): its layout and header, and with verify the
 * image checked against the header's length and SHA-256. 0; -EINVAL when res does not start with
 * a squashfs, -ENOSPC when it leaves no slot, -ENOENT without a valid header, -EBADMSG when the
 * image differs from its header, or a read error. */
int slot_read(const struct flash_device* res, int verify, struct release_slot_layout* layout,
              struct release_slot_header* header);

/* Writes image as the slot's release: checks that it is a slot image (release_slot_image_valid)
 * and its length and SHA-256 against header before any erase, erases the header block, writes
 * and verifies the image, then writes and verifies the header. The report describes the image
 * write; stage "source" means the flash was not touched. */
int slot_write(const struct flash_device* res, const struct flash_source* image,
               const struct release_slot_header* header, struct flash_report* report);

#ifdef __cplusplus
}
#endif
