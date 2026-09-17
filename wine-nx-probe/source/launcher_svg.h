/*
 * The launcher's icons come from SVG files (assets/). This fills one path, its
 * "d" attribute, into a coverage mask: the commands M L H V C S Q T A Z in both
 * absolute and relative forms, filled by the nonzero rule as SVG does, and
 * anti-aliased by sampling each pixel 4x4.
 */
#ifndef WINE_NX_LAUNCHER_SVG_H
#define WINE_NX_LAUNCHER_SVG_H

/* Fills mask, width x height with a byte a pixel, with how much of each pixel
 * the path covers (0-255). The view box (view_x, view_y, view_w, view_h) is
 * scaled to fit and centred. Returns 0 when the path does not parse or memory
 * runs out, leaving the mask clear. */
int svg_path_mask( const char *d, float view_x, float view_y, float view_w, float view_h,
                   int width, int height, unsigned char *mask );

#endif
