/*
 * Copyright (C) 2017 bouncyball
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#ifndef _dng_h
#define _dng_h

#include <sys/types.h>
#include "../mlv/mlv_object.h"

/* raw state definitions */
#define UNCOMPRESSED_RAW 0
#define COMPRESSED_RAW 1
#define UNCOMPRESSED_ORIG 2
#define COMPRESSED_ORIG 3

/* dngObject struct consists of DNG header and image buffers and their sizes */
typedef struct
{
    double fps_float;               // User defined fps
    int32_t par[4];                 // User defined verical and horizontal stretch (AR)

    int raw_input_state;            // 0 - uncompressed, 1 - losless
    int raw_output_state;           // 0 - uncompressed, 1 - losless, 2 - pass uncompressed, 3 - pass losless

    size_t header_size;             // dng header size
    size_t image_size;              // raw image buffer size
    size_t image_size_unpacked;     // bit unpacked working buffer size

    uint8_t * header_buf;           // pointer to header buffer
    uint16_t * image_buf;           // pointer to image buffer
    uint16_t * image_buf2;          // pointer to image buffer for temporary decompression
    uint16_t * image_buf_unpacked;  // pointer to bit packed image buffer

    /* Optional JPEG preview (thumbnail) embedded into the written DNG as a
       NewSubFileType = 1 SubIFD of IFD0. preview_width / preview_height > 0
       declares that a preview is wanted (see dngPlanPreview()), the JPEG bytes
       themselves are handed over in the write step (dngWriteFrame()) - the
       preview has to be rendered *after* the raw frame has been processed,
       otherwise rendering changes the exported raw data. */

    uint8_t * preview_buf;          // baseline JPEG stream, owned by this struct
    size_t    preview_size;         // JPEG byte count (0 = none attached yet)
    int       preview_width;
    int       preview_height;

    size_t preview_bytecount_offset;// header slot of the preview IFD's StripByteCounts
    size_t subifds_value_offset;    // header slot of IFD0's SubIFDs entry value

} dngObject_t;

/* routines to unpack, pack, decompress or compress raw data */
void dng_unpack_image_bits(uint16_t * input_buffer, uint16_t * output_buffer, int width, int height, uint32_t bpp);
void dng_pack_image_bits(uint16_t * input_buffer, uint16_t * output_buffer, int width, int height, uint32_t bpp, int big_endian);
int dng_compress_image(uint16_t * output_buffer, uint16_t * input_buffer, size_t * output_buffer_size, int width, int height, uint32_t bpp);
int dng_decompress_image(uint16_t * output_buffer, uint16_t * input_buffer, size_t input_buffer_size, int width, int height, uint32_t bpp);

/* routines to initialize, save and free DNG exporting struct */
dngObject_t * initDngObject(mlvObject_t * mlv_data, int raw_state, double fps, int32_t par[4]);
void freeDngObject(dngObject_t * dng_data);

/* ---- DNG export, two phase (E2) -------------------------------------------
   Rendering an image preview runs MLVApp's processing pipeline, which writes
   back state into the clip (vertical stripe correction coefficients, bad /
   focus pixel maps, dual ISO detection results ...). The DNG raw export reads
   that same state, so rendering a preview *before* exporting changes the raw
   data that ends up in the DNG. The export is therefore split in two:

       dngPlanPreview()   -> declare the preview dimensions (known without rendering)
       dngPrepareFrame()  -> read + low level process the raw frame, build the header
                             (nothing is written to disk yet)
       ... render the JPEG preview here - safe, the raw data is already final ...
       dngWriteFrame()    -> write header + raw + preview, patch the byte count in

   dngSetPreview() + saveDngFrame() are the one shot variants for callers that
   already hold the preview bytes (kept for compatibility).                */

/* declare that a preview of this pixel size will be embedded; pass 0/0 for none */
void dngPlanPreview(dngObject_t * dng_data, int preview_width, int preview_height);

/* phase 1: read the raw frame, apply low level raw processing, build the header */
int dngPrepareFrame(mlvObject_t * mlv_data, dngObject_t * dng_data, uint32_t frame_index, const char *props_filename);

/* phase 2: write header + raw image + optional JPEG preview to disk */
int dngWriteFrame(dngObject_t * dng_data, char * dng_filename, const uint8_t * preview_jpeg, size_t preview_size);

/* one shot export: prepare + write, using the bytes attached with dngSetPreview() */
int saveDngFrame(mlvObject_t * mlv_data, dngObject_t * dng_data, uint32_t frame_index, char * dng_filename, const char *props_filename);

/* attach (jpeg_data != NULL) or clear (jpeg_data == NULL) preview bytes; also sets
   the preview dimensions, i.e. it implies dngPlanPreview() */
void dngSetPreview(dngObject_t * dng_data, const uint8_t * jpeg_data, size_t jpeg_size, int preview_width, int preview_height);

#endif
