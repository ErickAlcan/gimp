/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 2009 Martin Nordholts <martinn@src.gnome.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <lcms2.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "display/display-types.h"

#include "display/gimpdisplay.h"
#include "display/gimpdisplayshell.h"
#include "display/gimpimagewindow.h"

#include "menus/menus.h"

#include "widgets/gimpuimanager.h"
#include "widgets/gimpdialogfactory.h"

#include "gegl/gimp-babl.h"

#include "core/gimp.h"
#include "core/gimpdrawable.h"
#include "core/gimpimage.h"
#include "core/gimpimage-color-profile.h"
#include "core/gimpimage-undo.h"
#include "core/gimplayer.h"
#include "core/gimplayer-new.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


#ifdef GDK_WINDOWING_QUARTZ
// only to get keycode definitions from HIToolbox/Events.h
#include <Carbon/Carbon.h>
#include <Cocoa/Cocoa.h>
#endif /* GDK_WINDOWING_QUARTZ */

void
gimp_test_utils_set_env_to_subdir (const gchar *root_env_var,
                                   const gchar *subdir,
                                   const gchar *target_env_var)
{
  const gchar *root_dir   = NULL;
  gchar       *target_dir = NULL;

  /* Get root dir */
  root_dir = g_getenv (root_env_var);
  if (! root_dir)
    g_printerr ("*\n"
                "*  The env var %s is not set, you are probably running\n"
                "*  in a debugger. Set it manually, e.g.:\n"
                "*\n"
                "*    set env %s=%s/source/gimp\n"
                "*\n",
                root_env_var,
                root_env_var, g_get_home_dir ());

  /* Construct path and setup target env var */
  target_dir = g_build_filename (root_dir, subdir, NULL);
  g_setenv (target_env_var, target_dir, TRUE);
  g_free (target_dir);
}

void
gimp_test_utils_set_env_to_subpath (const gchar *root_env_var1,
                                    const gchar *root_env_var2,
                                    const gchar *subdir,
                                    const gchar *target_env_var)
{
  const gchar *root_dir1   = NULL;
  const gchar *root_dir2   = NULL;
  gchar       *target_dir1 = NULL;
  gchar       *target_dir2 = NULL;
  gchar       *target_path = NULL;

  /* Get root dir */
  root_dir1 = g_getenv (root_env_var1);
  if (! root_dir1)
    g_printerr ("*\n"
                "*  The env var %s is not set, you are probably running\n"
                "*  in a debugger. Set it manually, e.g.:\n"
                "*\n"
                "*    set env %s=%s/source/gimp\n"
                "*\n",
                root_env_var1,
                root_env_var1, g_get_home_dir ());

  root_dir2 = g_getenv (root_env_var2);
  if (! root_dir2)
    g_printerr ("*\n"
                "*  The env var %s is not set, you are probably running\n"
                "*  in a debugger. Set it manually, e.g.:\n"
                "*\n"
                "*    set env %s=%s/source/gimp\n"
                "*\n",
                root_env_var2,
                root_env_var2, g_get_home_dir ());

  /* Construct path and setup target env var */
  target_dir1 = g_build_filename (root_dir1, subdir, NULL);
  target_dir2 = g_build_filename (root_dir2, subdir, NULL);

  target_path = g_strconcat (target_dir1, G_SEARCHPATH_SEPARATOR_S,
                             target_dir2, NULL);

  g_free (target_dir1);
  g_free (target_dir2);

  g_setenv (target_env_var, target_path, TRUE);
  g_free (target_path);
}


/**
 * gimp_test_utils_set_gimp3_directory:
 * @root_env_var: Either "GIMP_TESTING_ABS_TOP_SRCDIR" or
 *                "GIMP_TESTING_ABS_TOP_BUILDDIR"
 * @subdir:       Subdir, may be %NULL
 *
 * Sets GIMP3_DIRECTORY to the source dir @root_env_var/@subdir. The
 * environment variables is set up by the test runner, see Makefile.am
 **/
void
gimp_test_utils_set_gimp3_directory (const gchar *root_env_var,
                                     const gchar *subdir)
{
  gimp_test_utils_set_env_to_subdir (root_env_var,
                                     subdir,
                                     "GIMP3_DIRECTORY" /*target_env_var*/);
}

/**
 * gimp_test_utils_setup_menus_path:
 *
 * Sets GIMP_TESTING_MENUS_PATH to "$top_srcdir/menus:$top_builddir/menus".
 **/
void
gimp_test_utils_setup_menus_path (void)
{
  /* GIMP_TESTING_ABS_TOP_SRCDIR is set by the automake test runner,
   * see Makefile.am
   */
  gimp_test_utils_set_env_to_subpath ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                      "GIMP_TESTING_ABS_TOP_BUILDDIR",
                                      "menus",
                                      "GIMP_TESTING_MENUS_PATH");
}

/**
 * gimp_test_utils_create_image:
 * @gimp:   A #Gimp instance.
 * @width:  Width of image (and layer)
 * @height: Height of image (and layer)
 *
 * Creates a new image of a given size with one layer of same size and
 * a display.
 *
 * Returns: The new #GimpImage.
 **/
void
gimp_test_utils_create_image (Gimp *gimp,
                              gint  width,
                              gint  height)
{
  GimpImage *image;
  GimpLayer *layer;

  image = gimp_image_new (gimp, width, height,
                          GIMP_RGB, GIMP_PRECISION_U8_NON_LINEAR);

  layer = gimp_layer_new (image,
                          width,
                          height,
                          gimp_image_get_layer_format (image, TRUE),
                          "layer1",
                          1.0,
                          GIMP_LAYER_MODE_NORMAL);

  gimp_image_add_layer (image,
                        layer,
                        NULL /*parent*/,
                        0 /*position*/,
                        FALSE /*push_undo*/);

  gimp_create_display (gimp,
                       image,
                       gimp_unit_pixel (),
                       1.0 /*scale*/,
                       NULL);
}

/**
 * gimp_test_utils_synthesize_key_event:
 * @widget: Widget to target.
 * @keyval: Keyval, e.g. GDK_Return
 *
 * Simulates a keypress and release with gdk_test_simulate_key().
 **/
void
gimp_test_utils_synthesize_key_event (GtkWidget *widget,
                                      guint      keyval)
{
#if defined(GDK_WINDOWING_QUARTZ)

GdkKeymapKey *keys   = NULL;
gint          n_keys = 0;
gint          i;
CGEventRef    keyUp, keyDown;

if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_for_display (gdk_display_get_default ()), keyval, &keys, &n_keys))
  {
    /* XXX not in use yet */
    CGEventRef commandDown	=	CGEventCreateKeyboardEvent (NULL, (CGKeyCode)kVK_Command, true);
    CGEventRef commandUp	=	CGEventCreateKeyboardEvent (NULL, (CGKeyCode)kVK_Command, false);

    CGEventRef shiftDown	=	CGEventCreateKeyboardEvent (NULL, (CGKeyCode)kVK_Shift, true);
    CGEventRef shiftUp		=	CGEventCreateKeyboardEvent (NULL, (CGKeyCode)kVK_Shift, false);

    CGEventRef optionDown	=	CGEventCreateKeyboardEvent (NULL, (CGKeyCode)kVK_Option, true);
    CGEventRef optionUp		=	CGEventCreateKeyboardEvent (NULL, (CGKeyCode)kVK_Option, false);

    for (i = 0; i < n_keys; i++)
      {
        /* Option press. */
        if (keys[i].group)
          {
            CGEventPost (kCGHIDEventTap, optionDown);
          }
        /* Shift press. */
        if (keys[i].level)
          {
            CGEventPost(kCGHIDEventTap, shiftDown);
          }
        keyDown = CGEventCreateKeyboardEvent (NULL, (CGKeyCode)keys[i].keycode, true);
        keyUp = CGEventCreateKeyboardEvent (NULL, (CGKeyCode)keys[i].keycode, false);
        /* Key pressed. */
        CGEventPost (kCGHIDEventTap, keyDown);
        CFRelease (keyDown);
        usleep (100);
        /* key released */
        CGEventPost (kCGHIDEventTap, keyUp);
        CFRelease (keyUp);

        /* Shift release. */
        if (keys[i].level)
          {
            CGEventPost (kCGHIDEventTap, shiftDown);
          }

        /* Option release. */
        if (keys[i].group)
          {
            CGEventPost (kCGHIDEventTap, optionUp);
          }
        /* No need to loop for alternative keycodes. We want only one
         * key generated. */
        break;
      }
    CFRelease (commandDown);
    CFRelease (commandUp);
    CFRelease (shiftDown);
    CFRelease (shiftUp);
    CFRelease (optionDown);
    CFRelease (optionUp);
    g_free (keys);
  }
else
  {
    g_warning ("%s: no macOS key mapping found for keyval %d.", G_STRFUNC, keyval);
  }

#else /* ! GDK_WINDOWING_QUARTZ */

  gtk_test_widget_send_key(widget, keyval, 0);

#endif /* ! GDK_WINDOWING_QUARTZ */
}

/**
 * gimp_test_utils_get_ui_manager:
 * @gimp: The #Gimp instance.
 *
 * Returns the "best" #GimpUIManager to use when performing
 * actions. It gives the ui manager of the empty display if it exists,
 * otherwise it gives it the ui manager of the first display.
 *
 * Returns: The #GimpUIManager.
 **/
GimpUIManager *
gimp_test_utils_get_ui_manager (Gimp *gimp)
{
  return menus_get_image_manager_singleton (gimp);
}

/**
 * gimp_test_utils_create_image_from_dalog:
 * @gimp:
 *
 * Creates a new image using the "New image" dialog, and then returns
 * the #GimpImage created.
 *
 * Returns: The created #GimpImage.
 **/
GimpImage *
gimp_test_utils_create_image_from_dialog (Gimp *gimp)
{
  GimpImage     *image            = NULL;
  GtkWidget     *new_image_dialog = NULL;
  guint          n_initial_images = g_list_length (gimp_get_image_iter (gimp));
  guint          n_images         = -1;
  gint           tries_left       = 100;
  GimpUIManager *ui_manager       = gimp_test_utils_get_ui_manager (gimp);

  /* Bring up the new image dialog */
  gimp_ui_manager_activate_action (ui_manager,
                                   "image",
                                   "image-new");
  gimp_test_run_mainloop_until_idle ();

  /* Get the GtkWindow of the dialog */
  new_image_dialog =
    gimp_dialog_factory_dialog_raise (gimp_dialog_factory_get_singleton (),
                                      gdk_display_get_monitor (gdk_display_get_default (), 0),
                                      NULL,
                                      "gimp-image-new-dialog",
                                      -1 /*view_size*/);

  /* Press the OK button. It will take a while for the image to be
   * created so loop for a while
   */
  gtk_dialog_response (GTK_DIALOG (new_image_dialog), GTK_RESPONSE_OK);
  do
    {
      g_usleep (20 * 1000);
      gimp_test_run_mainloop_until_idle ();
      n_images = g_list_length (gimp_get_image_iter (gimp));
    }
  while (tries_left-- &&
         n_images != n_initial_images + 1);

  /* Make sure there now is one image more than initially */
  g_assert_cmpint (n_images,
                   ==,
                   n_initial_images + 1);

  image = GIMP_IMAGE (gimp_get_image_iter (gimp)->data);

  return image;
}



/*  CMYK helpers  */

/**
 * gimp_test_utils_cmyk_precision:
 *
 * Returns: the precision selected with GIMP_TESTING_CMYK_PRECISION
 *          (u8, u16, half, float, double), default u16.  Always a
 *          non-linear precision.
 **/
GimpPrecision
gimp_test_utils_cmyk_precision (void)
{
  const gchar *env = g_getenv ("GIMP_TESTING_CMYK_PRECISION");

  if (! env || ! *env || ! strcmp (env, "u16"))
    return GIMP_PRECISION_U16_NON_LINEAR;
  else if (! strcmp (env, "u8"))
    return GIMP_PRECISION_U8_NON_LINEAR;
  else if (! strcmp (env, "half"))
    return GIMP_PRECISION_HALF_NON_LINEAR;
  else if (! strcmp (env, "float"))
    return GIMP_PRECISION_FLOAT_NON_LINEAR;
  else if (! strcmp (env, "double"))
    return GIMP_PRECISION_DOUBLE_NON_LINEAR;

  g_printerr ("GIMP_TESTING_CMYK_PRECISION=%s is invalid, using u16\n", env);

  return GIMP_PRECISION_U16_NON_LINEAR;
}

/**
 * gimp_test_utils_create_cmyk_image:
 * @gimp:      a #Gimp
 * @width:     the width
 * @height:    the height
 * @precision: a non-linear precision
 * @profile:   (nullable): a bidirectional CMYK profile, or %NULL for the
 *             default CMYK profile
 *
 * Creates a CMYK image without layers.  The image is clean and has an
 * empty undo stack.
 *
 * Returns: (transfer full): the new image.
 **/
GimpImage *
gimp_test_utils_create_cmyk_image (Gimp             *gimp,
                                   gint              width,
                                   gint              height,
                                   GimpPrecision     precision,
                                   GimpColorProfile *profile)
{
  GimpImage *image;

  image = gimp_image_new (gimp, width, height, GIMP_CMYK, precision);
  g_assert_nonnull (image);

  if (profile)
    {
      GError *error = NULL;

      gimp_image_undo_disable (image);
      g_assert_true (gimp_image_set_color_profile (image, profile, &error));
      g_assert_no_error (error);
      gimp_image_undo_enable (image);
      gimp_image_clean_all (image);
    }

  return image;
}

/**
 * gimp_test_utils_add_cmyk_layer:
 * @image:      a CMYK #GimpImage
 * @name:       the layer name
 * @with_alpha: whether the layer has alpha
 * @cmyka:      (nullable): the fill value (C, M, Y, K, alpha) in 0..1,
 *              %NULL for transparent / no ink
 *
 * Adds an image-sized Normal layer on top of the stack (without undo).
 *
 * Returns: (transfer none): the new layer.
 **/
GimpLayer *
gimp_test_utils_add_cmyk_layer (GimpImage     *image,
                                const gchar   *name,
                                gboolean       with_alpha,
                                const gdouble  cmyka[5])
{
  GimpLayer *layer;

  layer = gimp_layer_new (image,
                          gimp_image_get_width  (image),
                          gimp_image_get_height (image),
                          gimp_image_get_layer_format (image, with_alpha),
                          name, GIMP_OPACITY_OPAQUE, GIMP_LAYER_MODE_NORMAL);

  if (cmyka)
    gimp_test_utils_write_cmyk_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                      NULL, cmyka);

  gimp_image_add_layer (image, layer, NULL, 0, FALSE);

  return layer;
}

/**
 * gimp_test_utils_write_cmyk_patch:
 * @buffer: a buffer with a CMYK format
 * @rect:   (nullable): the area, %NULL for the whole buffer
 * @cmyka:  C, M, Y, K, alpha in 0..1
 *
 * Fills @rect with @cmyka, written as "CMYKA double" in @buffer's space
 * (no color conversion, only re-encoding).
 **/
void
gimp_test_utils_write_cmyk_patch (GeglBuffer          *buffer,
                                  const GeglRectangle *rect,
                                  const gdouble        cmyka[5])
{
  const Babl *format;
  GeglColor  *color;

  g_return_if_fail (GEGL_IS_BUFFER (buffer));
  g_return_if_fail (gimp_babl_format_is_cmyk (gegl_buffer_get_format (buffer)));

  format = babl_format_with_space ("CMYKA double",
                                   gegl_buffer_get_format (buffer));

  color = gegl_color_new (NULL);
  gegl_color_set_pixel (color, format, cmyka);

  gegl_buffer_set_color (buffer, rect, color);

  g_object_unref (color);
}

/**
 * gimp_test_utils_read_cmyk_pixel:
 * @buffer: a buffer with a CMYK format
 * @x:      x
 * @y:      y
 * @cmyka:  (out caller-allocates): C, M, Y, K, alpha
 *
 * Reads one pixel as "CMYKA double" in @buffer's space.
 **/
void
gimp_test_utils_read_cmyk_pixel (GeglBuffer *buffer,
                                 gint        x,
                                 gint        y,
                                 gdouble     cmyka[5])
{
  g_return_if_fail (GEGL_IS_BUFFER (buffer));

  gegl_buffer_get (buffer, GEGL_RECTANGLE (x, y, 1, 1), 1.0,
                   babl_format_with_space ("CMYKA double",
                                           gegl_buffer_get_format (buffer)),
                   cmyka, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
}

/**
 * gimp_test_utils_read_patch:
 * @buffer: a #GeglBuffer
 * @rect:   (nullable): the area, %NULL for the whole buffer
 * @format: the format to read with (tag it with the right space!)
 *
 * Returns: (transfer full): the pixels of @rect in @format, free with
 *          g_free().
 **/
gpointer
gimp_test_utils_read_patch (GeglBuffer          *buffer,
                            const GeglRectangle *rect,
                            const Babl          *format)
{
  gpointer data;

  g_return_val_if_fail (GEGL_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (format != NULL, NULL);

  if (! rect)
    rect = gegl_buffer_get_extent (buffer);

  data = g_malloc ((gsize) rect->width * rect->height *
                   babl_format_get_bytes_per_pixel (format));

  gegl_buffer_get (buffer, rect, 1.0, format, data,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  return data;
}

/**
 * gimp_test_utils_cmyk_input_only_profile:
 *
 * Creates an input-only derivative of the builtin CMYK profile: all
 * BToA tags removed (they are linked, so all of them), saved and
 * reloaded.  Asserts that lcms no longer supports it as output profile.
 *
 * Returns: (transfer full): the input-only CMYK profile.
 **/
GimpColorProfile *
gimp_test_utils_cmyk_input_only_profile (void)
{
  static const cmsTagSignature tags[] =
  {
    cmsSigBToA0Tag, cmsSigBToA1Tag, cmsSigBToA2Tag,
    cmsSigBToD0Tag, cmsSigBToD1Tag, cmsSigBToD2Tag
  };

  GimpColorProfile *builtin;
  GimpColorProfile *profile;
  const guint8     *data;
  gsize             length;
  cmsHPROFILE       lcms;
  cmsUInt32Number   len = 0;
  guint8           *new_data;
  GError           *error = NULL;
  gint              i;

  builtin = gimp_color_profile_new_cmyk_builtin ();
  data    = gimp_color_profile_get_icc_profile (builtin, &length);

  lcms = cmsOpenProfileFromMem (data, length);
  g_assert_nonnull (lcms);

  for (i = 0; i < G_N_ELEMENTS (tags); i++)
    if (cmsIsTag (lcms, tags[i]))
      cmsWriteTag (lcms, tags[i], NULL);

  g_assert_true (cmsSaveProfileToMem (lcms, NULL, &len));
  new_data = g_malloc (len);
  g_assert_true (cmsSaveProfileToMem (lcms, new_data, &len));
  cmsCloseProfile (lcms);

  profile = gimp_color_profile_new_from_icc_profile (new_data, len, &error);
  g_assert_no_error (error);
  g_assert_nonnull (profile);
  g_free (new_data);

  g_assert_true (gimp_color_profile_is_cmyk (profile));
  g_assert_false (cmsIsIntentSupported (gimp_color_profile_get_lcms_profile (profile),
                                        INTENT_RELATIVE_COLORIMETRIC,
                                        LCMS_USED_AS_OUTPUT));

  g_object_unref (builtin);

  return profile;
}

/**
 * gimp_test_utils_cmyk_header_variant:
 * @profile: a #GimpColorProfile
 *
 * Returns: (transfer full): a copy of @profile whose header date (bytes
 *          24-35) differs; all other bytes are equal, so the result is
 *          gimp_color_profile_is_equal() to @profile.
 **/
GimpColorProfile *
gimp_test_utils_cmyk_header_variant (GimpColorProfile *profile)
{
  static const guint8  date[12] = { 0x07, 0xe5, 0x00, 0x03, 0x00, 0x0e,
                                    0x0c, 0x22, 0x38, 0x00, 0x00, 0x00 };
  GimpColorProfile    *variant;
  const guint8        *data;
  gsize                length;
  guint8              *copy;
  GError              *error = NULL;

  g_return_val_if_fail (GIMP_IS_COLOR_PROFILE (profile), NULL);

  data = gimp_color_profile_get_icc_profile (profile, &length);
  g_assert_cmpuint (length, >, 128);

  copy = g_memdup2 (data, length);
  memcpy (copy + 24, date, sizeof (date));
  g_assert_true (memcmp (copy, data, length) != 0);

  variant = gimp_color_profile_new_from_icc_profile (copy, length, &error);
  g_assert_no_error (error);
  g_free (copy);

  return variant;
}
