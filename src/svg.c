/*
 * File: svg.c
 *
 * Copyright (C) 2024 Rodrigo Arias Mallo <rodarima@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include <config.h>

#ifdef ENABLE_SVG

#include <math.h>
#include "msg.h"
#include "image.hh"
#include "imgbuf.hh"
#include "cache.h"
#include "dicache.h"

#ifdef HAVE_RSVG
#include <librsvg/rsvg.h>
#else
#define NANOSVG_ALL_COLOR_KEYWORDS
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#endif

typedef struct {
   DilloImage *Image;
   DilloUrl *url;
   int version, fgcolor, decoded;
   float dpi;
} DilloSvg;

#ifdef HAVE_RSVG
typedef struct { RsvgHandle *handle; double width, height; } SvgSource;
static void Svg_source_free(void *data)
{
   SvgSource *source = data;
   g_object_unref(source->handle);
   dFree(source);
}
static int Svg_render(void *data, int width, int height,
                      unsigned char *dest, int stride)
{
   SvgSource *source = data;
   cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
   cairo_t *cr = cairo_create(surface);
   // Stretch the intrinsic viewport as an image. SVG's preserveAspectRatio
   // still applies inside that viewport when the file uses a viewBox.
   cairo_scale(cr, width / source->width, height / source->height);
   RsvgRectangle viewport = {0, 0, source->width, source->height};
   int ok = rsvg_handle_render_document(source->handle, cr, &viewport, NULL);
   cairo_destroy(cr);
   cairo_surface_flush(surface);
   if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) ok = 0;
   if (ok) {
      unsigned char *pixels = cairo_image_surface_get_data(surface);
      int src_stride = cairo_image_surface_get_stride(surface);
      for (int y = 0; y < height; ++y) {
         const uint32_t *src = (const uint32_t *)(pixels + y * src_stride);
         unsigned char *row = dest + y * stride;
         for (int x = 0; x < width; ++x) {
            unsigned a = src[x] >> 24;
            row[4*x] = a ? (((src[x] >> 16) & 255) * 255 + a/2) / a : 0;
            row[4*x+1] = a ? (((src[x] >> 8) & 255) * 255 + a/2) / a : 0;
            row[4*x+2] = a ? ((src[x] & 255) * 255 + a/2) / a : 0;
            row[4*x+3] = a;
         }
      }
   }
   cairo_surface_destroy(surface);
   return ok;
}
#else
typedef struct { NSVGimage *image; } SvgSource;
static void Svg_source_free(void *data)
{
   SvgSource *source = data;
   nsvgDelete(source->image);
   dFree(source);
}
static int Svg_render(void *data, int width, int height,
                      unsigned char *dest, int stride)
{
   SvgSource *source = data;
   NSVGrasterizer *rasterizer = nsvgCreateRasterizer();
   if (!rasterizer) return 0;
   nsvgRasterizeXY(rasterizer, source->image, 0, 0,
      width / source->image->width, height / source->image->height,
      dest, width, height, stride);
   nsvgDeleteRasterizer(rasterizer);
   return 1;
}
#endif

static void Svg_write(DilloSvg *svg, const void *buf, uint_t size, int final)
{
   if (svg->decoded || !buf || !size || size > 8 * 1024 * 1024) return;
   char *str = dStrndup(buf, size);
   // Accept whitespace in closing tags and self-closing SVG roots. Parsing
   // happens once, with a final attempt at EOF for other legal XML forms.
   if (!final && !strstr(str, "</svg")) {
      char *root = strstr(str, "<svg"), *end = root ? strchr(root, '>') : NULL;
      if (!end || end == root || end[-1] != '/') { dFree(str); return; }
   }
   SvgSource *source = dNew0(SvgSource, 1);
   double w = 0, h = 0;
#ifdef HAVE_RSVG
   source->handle = rsvg_handle_new_from_data((const guint8 *)str, size, NULL);
   if (source->handle) {
      rsvg_handle_set_dpi(source->handle, svg->dpi);
      char css[64];
      int len = snprintf(css, sizeof(css), ":root { color: #%06x; }", svg->fgcolor & 0xffffff);
      rsvg_handle_set_stylesheet(source->handle, (const guint8 *)css, len, NULL);
      if (!rsvg_handle_get_intrinsic_size_in_pixels(source->handle, &w, &h)) {
         gboolean has_viewbox;
         RsvgRectangle viewbox;
         rsvg_handle_get_intrinsic_dimensions(source->handle, NULL, NULL,
            NULL, NULL, &has_viewbox, &viewbox);
         w = has_viewbox ? viewbox.width : 300;
         h = has_viewbox ? viewbox.height : 150;
      }
   }
   source->width = w; source->height = h;
#else
   unsigned color = NSVG_RGB((svg->fgcolor >> 16) & 255,
                            (svg->fgcolor >> 8) & 255, svg->fgcolor & 255);
   source->image = nsvgParse(str, "px", svg->dpi, color);
   if (source->image) { w = source->image->width; h = source->image->height; }
#endif
   dFree(str);
   if (!isfinite(w) || !isfinite(h) || w <= 0 || h <= 0 ||
       w > 32767 || h > 32767 || ceil(w) > IMAGE_MAX_AREA / ceil(h)) {
#ifdef HAVE_RSVG
      if (source->handle) Svg_source_free(source); else dFree(source);
#else
      if (source->image) Svg_source_free(source); else dFree(source);
#endif
      return;
   }
   unsigned width = ceil(w), height = ceil(h);
   unsigned char *dest = dNew(unsigned char, width * height * 4);
   if (!Svg_render(source, width, height, dest, width * 4)) {
      dFree(dest); Svg_source_free(source); return;
   }
   a_Dicache_set_parms(svg->url, svg->version, svg->Image,
      width, height, DILLO_IMG_TYPE_RGBA, 1 / 2.2);
   DICacheEntry *entry = a_Dicache_get_entry(svg->url, svg->version);
   a_Imgbuf_set_rasterizer(entry->v_imgbuf, Svg_render, source, Svg_source_free);
   for (unsigned y = 0; y < height; ++y)
      a_Dicache_write(svg->url, svg->version, dest + y * width * 4, y);
   dFree(dest);
   svg->decoded = 1;
   svg->Image = NULL;
}

void a_Svg_callback(int op, void *data)
{
   if (op == CA_Send || op == CA_Close) {
      CacheClient_t *client = data;
      DilloSvg *svg = client->CbData;
      Svg_write(svg, client->Buf, client->BufSize, op == CA_Close);
      if (op == CA_Close) {
         a_Dicache_close(svg->url, svg->version, client);
         dFree(svg);
      }
   } else if (op == CA_Abort) dFree(data);
}

void *a_Svg_new(DilloImage *image, DilloUrl *url, int version)
{
   DilloSvg *svg = dNew0(DilloSvg, 1);
   svg->Image = image; svg->url = url; svg->version = version;
   svg->fgcolor = image->fg_color; svg->dpi = image->dpi;
   return svg;
}

#else
void *a_Svg_new() { return 0; }
void a_Svg_callback() { return; }
#endif
