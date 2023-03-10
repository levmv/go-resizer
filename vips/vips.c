#include "vips.h"
#include <string.h>

void g_free_go(void **buf) {
  g_free(*buf);
}

void clear_image(VipsImage **image) {
  if (G_IS_OBJECT(*image)) g_clear_object(image); // g_object_unref(image);
}

void swap_and_clear(VipsImage **in, VipsImage *out) {
  clear_image(in);
  *in = out;
}


int vips_initialize() {
    return VIPS_INIT("levmv_vips");
}


int vips_jpegload_go(void *buf, size_t len, VipsImage **out) {
  return vips_jpegload_buffer(buf, len, out, "access", VIPS_ACCESS_SEQUENTIAL, NULL);
}

VipsImage* image_new_from_buffer(void *buf, size_t len) {
    return vips_image_new_from_buffer(buf, len, "", "access", VIPS_ACCESS_SEQUENTIAL, NULL);
}

int thumbnail_buffer(void *buf, size_t len, VipsImage **out, int width, int height, int crop, int size) {
    return vips_thumbnail_buffer(buf, len, out, width, "height", height, "crop", crop, "size", size, NULL);
}

int resize(VipsImage *in, VipsImage **out, double ratio) {
    if (!vips_image_hasalpha(in)) {
        return vips_resize(in, out, ratio);
    }

    VipsImage *base = vips_image_new();
    VipsImage **t = (VipsImage **) vips_object_local_array(VIPS_OBJECT(base), 3);

    int res =
        vips_premultiply(in, &t[0], NULL) ||
        vips_resize(t[0], &t[1], ratio) ||
        vips_unpremultiply(t[1], &t[2], NULL) ||
        vips_cast(t[2], out, in->BandFmt, NULL);

    clear_image(&base);
    return 0;
}

int crop(VipsImage *in, VipsImage **out, int x, int y, int width, int height) {
    return vips_extract_area(in, out, x, y, width, height, NULL);
}


int thumbnail (VipsImage *in, VipsImage **out, int width, int height, int crop, int size) {
    return vips_thumbnail_image (in, out, width,
        "height", height, "crop", crop, "size", size,
        NULL
        );
}


int jpegsave(VipsImage *in, void **buf, size_t *len, int quality) {
    return vips_jpegsave_buffer(
        in, buf, len,
        "Q", quality,
        "optimize_coding", TRUE,
        NULL
    );
}


int webpsave(VipsImage *in, void **buf, size_t *len, int quality) {
    return vips_webpsave_buffer(
        in, buf, len,
        "Q", quality,
        NULL
    );
}


int embed_image(VipsImage *in, VipsImage **out, int left, int top, int width, int height) {
  VipsImage *tmp = NULL;

  if (!vips_image_hasalpha(in)) {
    if (vips_bandjoin_const1(in, &tmp, 255, NULL))
      return 1;

    in = tmp;
  }

  int code = vips_embed(in, out, left, top, width, height, "extend", VIPS_EXTEND_BLACK,  NULL);

  if (tmp) clear_image(&tmp);

  return code;
}


int embed_image_background(VipsImage *in, VipsImage **out, int left, int top, int width,
                int height, double r, double g, double b, double a) {

  double background[3] = {r, g, b};
  double backgroundRGBA[4] = {r, g, b, a};

  VipsArrayDouble *vipsBackground;

  if (in->Bands <= 3) {
    vipsBackground = vips_array_double_new(background, 3);
  } else {
    vipsBackground = vips_array_double_new(backgroundRGBA, 4);
  }

  int code = vips_embed(in, out, left, top, width, height,
    "extend", VIPS_EXTEND_BACKGROUND, "background", vipsBackground, NULL);

  vips_area_unref(VIPS_AREA(vipsBackground));
  return code;
}


int flatten_image(VipsImage *in, VipsImage **out, double r, double g, double b) {

    if (!vips_image_hasalpha(in))
        return vips_copy(in, out, NULL);

    double background[3] = {r, g, b};
    VipsArrayDouble *vipsBackground = vips_array_double_new(background, 3);

    int code = vips_flatten(in, out, "background", vipsBackground);

    vips_area_unref(VIPS_AREA(vipsBackground));
    return code;
}

int composite_image(VipsImage *base, VipsImage *overlay, VipsImage **out) {
    int code = vips_composite2(base, overlay, out, VIPS_BLEND_MODE_OVER, "compositing_space", base->Type, NULL);

    return code;
}


// TODO: maybe use struct for params?
int label(VipsImage *in, VipsImage **out, const char *text, const char *font, const char *font_file, double r, double g, double b, int x, int y, int width, int height) {

    double color[3] = { r, g, b };
    static double ones[3] = { 1, 1, 1 };
    VipsObject *base = (VipsObject *) vips_image_new();

    VipsImage **t = (VipsImage **) vips_object_local_array( VIPS_OBJECT( base ), 10 );

    if (vips_text(&t[0], text,
     "font", font,
     "fontfile", font_file,
     "width", width,
            "height", height, NULL) ||
         vips_linear1(t[0], &t[1], /* opacity*/1, 0.0, NULL) ||
         vips_cast(t[1], &t[2], VIPS_FORMAT_UCHAR, NULL) ||
         vips_embed(t[2], &t[3], x, y, t[2]->Xsize + x,
                    t[2]->Ysize + y, NULL)) {
       g_object_unref(base);
       return 1;
     }

    /* Make the constant image to paint the text with. We make a 1x1
     * black image, then add the colour and cast it to match in. Then
     * expand it to match in in size.
     */
    if (vips_black(&t[4], 1, 1, NULL) ||
          vips_linear(t[4], &t[5], ones, color, 3, NULL) ||
          vips_cast(t[5], &t[6], VIPS_FORMAT_UCHAR, NULL) ||
          vips_copy(t[6], &t[7], "interpretation", in->Type, NULL) ||
          vips_embed(t[7], &t[8], 0, 0, in->Xsize, in->Ysize, "extend",
                     VIPS_EXTEND_COPY, NULL)) {
        g_object_unref(base);
        return 1;
      }

    if (vips_ifthenelse(t[3], t[8], in, out, "blend", TRUE, NULL)) {
        g_object_unref(base);
        return 1;
      }

    g_object_unref(base);
    return 0;
}


void vips_cleanup() {
    vips_error_clear();
    vips_thread_shutdown();
}