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

#include "fonts.h"
#include "../../file.h"
#include "../../general.h"
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define ATLAS_ROWS 8
#define ATLAS_COLS 16
#define ATLAS_SIZE (ATLAS_ROWS * ATLAS_COLS)

typedef struct ft_renderer
{
   FT_Library lib;
   FT_Face face;

   struct font_atlas atlas;
   struct font_glyph glyphs[ATLAS_SIZE];
} ft_renderer_t;

static const struct font_atlas *ft_renderer_get_atlas(void *data)
{
   ft_renderer_t *handle = (ft_renderer_t*)data;
   return &handle->atlas;
}

static const struct font_glyph *ft_renderer_get_glyph(void *data, uint32_t code)
{
   ft_renderer_t *handle = (ft_renderer_t*)data;
   return code < ATLAS_SIZE ? &handle->glyphs[code] : NULL;
}

static void ft_renderer_free(void *data)
{
   ft_renderer_t *handle = (ft_renderer_t*)data;
   if (!handle)
      return;

   free(handle->atlas.buffer);

   if (handle->face)
      FT_Done_Face(handle->face);
   if (handle->lib)
      FT_Done_FreeType(handle->lib);
   free(handle);
}

static bool ft_renderer_create_atlas(ft_renderer_t *handle)
{
   unsigned i;
   bool ret = true;

   uint8_t *buffer[ATLAS_SIZE] = {NULL};
   unsigned pitches[ATLAS_SIZE] = {0};

   unsigned max_width = 0;
   unsigned max_height = 0;

   for (i = 0; i < ATLAS_SIZE; i++)
   {
      struct font_glyph *glyph = &handle->glyphs[i];

      if (FT_Load_Char(handle->face, i, FT_LOAD_RENDER))
      {
         ret = false;
         goto end;
      }

      FT_Render_Glyph(handle->face->glyph, FT_RENDER_MODE_NORMAL);
      FT_GlyphSlot slot = handle->face->glyph;

      // Some glyphs can be blank.
      buffer[i] = (uint8_t*)calloc(slot->bitmap.rows * slot->bitmap.pitch, 1);

      glyph->width = slot->bitmap.width;
      glyph->height = slot->bitmap.rows;
      pitches[i] = slot->bitmap.pitch;

      glyph->advance_x = slot->advance.x >> 6;
      glyph->advance_y = slot->advance.y >> 6;
      glyph->draw_offset_x = slot->bitmap_left;
      glyph->draw_offset_y = -slot->bitmap_top;

      if (buffer[i])
         memcpy(buffer[i], slot->bitmap.buffer, slot->bitmap.rows * pitches[i]);
      max_width = max(max_width, (unsigned)slot->bitmap.width);
      max_height = max(max_height, (unsigned)slot->bitmap.rows);
   }

   handle->atlas.width = max_width * ATLAS_COLS;
   handle->atlas.height = max_height * ATLAS_ROWS;

   handle->atlas.buffer = (uint8_t*)calloc(handle->atlas.width * handle->atlas.height, 1);
   if (!handle->atlas.buffer)
   {
      ret = false;
      goto end;
   }

   // Blit our texture atlas.
   for (i = 0; i < ATLAS_SIZE; i++)
   {
      unsigned r, c;

      unsigned offset_x = (i % ATLAS_COLS) * max_width;
      unsigned offset_y = (i / ATLAS_COLS) * max_height;

      handle->glyphs[i].atlas_offset_x = offset_x;
      handle->glyphs[i].atlas_offset_y = offset_y;

      uint8_t *dst = handle->atlas.buffer;
      dst += offset_x + offset_y * handle->atlas.width;

      if (buffer[i])
      {
         const uint8_t *src = buffer[i];
         for (r = 0; r < handle->glyphs[i].height; r++, dst += handle->atlas.width, src += pitches[i])
            for (c = 0; c < handle->glyphs[i].width; c++)
               dst[c] = src[c];
      }
   }

end:
   for (i = 0; i < ATLAS_SIZE; i++)
      free(buffer[i]);
   return ret;
}

static void *ft_renderer_init(const char *font_path, float font_size)
{
   FT_Error err;

   ft_renderer_t *handle = (ft_renderer_t*)calloc(1, sizeof(*handle));
   if (!handle)
      goto error;

   err = FT_Init_FreeType(&handle->lib);
   if (err)
      goto error;

   err = FT_New_Face(handle->lib, font_path, 0, &handle->face);
   if (err)
      goto error;

   err = FT_Set_Pixel_Sizes(handle->face, 0, font_size);
   if (err)
      goto error;

   if (!ft_renderer_create_atlas(handle))
      goto error;

   return handle;

error:
   ft_renderer_free(handle);
   return NULL;
}

// Not the cleanest way to do things for sure, but should hopefully work ... :)

static const char *font_paths[] = {
#if defined(_WIN32)
   "C:\\Windows\\Fonts\\consola.ttf",
   "C:\\Windows\\Fonts\\verdana.ttf",
#elif defined(__APPLE__)
   "/Library/Fonts/Microsoft/Candara.ttf",
   "/Library/Fonts/Verdana.ttf",
   "/Library/Fonts/Tahoma.ttf",
#else
   "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
   "/usr/share/fonts/TTF/DejaVuSans.ttf",
   "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSansMono.ttf",
   "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf",
#endif
   "osd-font.ttf", // Magic font to search for, useful for distribution.
};

// Highly OS/platform dependent.
static const char *ft_renderer_get_default_font(void)
{
   size_t i;
   for (i = 0; i < ARRAY_SIZE(font_paths); i++)
   {
      if (path_file_exists(font_paths[i]))
         return font_paths[i];
   }

   return NULL;
}

const font_renderer_driver_t ft_font_renderer = {
   ft_renderer_init,
   ft_renderer_get_atlas,
   ft_renderer_get_glyph,
   ft_renderer_free,
   ft_renderer_get_default_font,
   "freetype",
};

