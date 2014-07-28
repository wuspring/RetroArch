/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __RARCH_FONTS_H
#define __RARCH_FONTS_H

#include <stdint.h>
#include "../../boolean.h"

// All coordinates and offsets are top-left oriented.
//
// This is a texture-atlas approach which allows text to be drawn in a single draw call.
// It is up to the code using this interface to actually generate proper vertex buffers and upload the atlas texture to GPU.

struct font_glyph
{
   unsigned width;
   unsigned height;

   // Texel coordiate offset for top-left pixel of this glyph.
   unsigned atlas_offset_x;
   unsigned atlas_offset_y;

   // When drawing this glyph, apply an offset to current X/Y draw coordinate.
   int draw_offset_x;
   int draw_offset_y;

   // Advance X/Y draw coordinates after drawing this glyph.
   int advance_x;
   int advance_y;
};

struct font_atlas
{
   uint8_t *buffer; // Alpha channel.
   unsigned width;
   unsigned height;
};

typedef struct font_renderer_driver
{
   void *(*init)(const char *font_path, float font_size);
   const struct font_atlas *(*get_atlas)(void *data);
   const struct font_glyph *(*get_glyph)(void *data, uint32_t code); // Returns NULL if no glyph for this code is found.
   void (*free)(void *data);

   const char *(*get_default_font)(void);
   const char *ident;
} font_renderer_driver_t;

extern const font_renderer_driver_t ft_font_renderer;
extern const font_renderer_driver_t bitmap_font_renderer;

// font_path can be NULL for default font.
bool font_renderer_create_default(const font_renderer_driver_t **driver, void **handle, const char *font_path, unsigned font_size);

#endif

