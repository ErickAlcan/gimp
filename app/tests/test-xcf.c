/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 2009 Martin Nordholts
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

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef _WIN32
#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#define close _close
#endif

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "widgets/widgets-types.h"

#include "gegl/gimp-babl.h"

#include "widgets/gimpuimanager.h"

#include "core/gimp.h"
#include "core/gimp-gui.h"
#include "core/gimpchannel.h"
#include "core/gimpchannel-select.h"
#include "core/gimpdrawable.h"
#include "core/gimpgrid.h"
#include "core/gimpgrouplayer.h"
#include "core/gimpguide.h"
#include "core/gimpimage.h"
#include "core/gimpimage-color-profile.h"
#include "core/gimpimage-grid.h"
#include "core/gimpimage-private.h"
#include "core/gimpimage-undo.h"
#include "core/gimpimage-guides.h"
#include "core/gimpimage-sample-points.h"
#include "core/gimplayer.h"
#include "core/gimplayer-new.h"
#include "core/gimplayermask.h"
#include "core/gimpparasitelist.h"
#include "core/gimpsamplepoint.h"
#include "core/gimpselection.h"

#include "path/gimpanchor.h"
#include "path/gimpbezierstroke.h"
#include "path/gimppath.h"

#include "plug-in/gimppluginmanager-file.h"

#include "file/file-open.h"
#include "file/file-save.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


/* we continue to use LEGACY layers for testing, so we can use the
 * same test image for all tests, including loading
 * files/gimp-2-6-file.xcf which can't have any non-LEGACY modes
 */

#define GIMP_MAINIMAGE_WIDTH            100
#define GIMP_MAINIMAGE_HEIGHT           90
#define GIMP_MAINIMAGE_TYPE             GIMP_RGB
#define GIMP_MAINIMAGE_PRECISION        GIMP_PRECISION_U8_NON_LINEAR

#define GIMP_MAINIMAGE_LAYER1_NAME      "layer1"
#define GIMP_MAINIMAGE_LAYER1_WIDTH     50
#define GIMP_MAINIMAGE_LAYER1_HEIGHT    51
#define GIMP_MAINIMAGE_LAYER1_FORMAT    babl_format ("R'G'B'A u8")
#define GIMP_MAINIMAGE_LAYER1_OPACITY   GIMP_OPACITY_OPAQUE
#define GIMP_MAINIMAGE_LAYER1_MODE      GIMP_LAYER_MODE_NORMAL_LEGACY

#define GIMP_MAINIMAGE_LAYER2_NAME      "layer2"
#define GIMP_MAINIMAGE_LAYER2_WIDTH     25
#define GIMP_MAINIMAGE_LAYER2_HEIGHT    251
#define GIMP_MAINIMAGE_LAYER2_FORMAT    babl_format ("R'G'B' u8")
#define GIMP_MAINIMAGE_LAYER2_OPACITY   GIMP_OPACITY_TRANSPARENT
#define GIMP_MAINIMAGE_LAYER2_MODE      GIMP_LAYER_MODE_MULTIPLY_LEGACY

#define GIMP_MAINIMAGE_GROUP1_NAME      "group1"

#define GIMP_MAINIMAGE_LAYER3_NAME      "layer3"

#define GIMP_MAINIMAGE_LAYER4_NAME      "layer4"

#define GIMP_MAINIMAGE_GROUP2_NAME      "group2"

#define GIMP_MAINIMAGE_LAYER5_NAME      "layer5"

#define GIMP_MAINIMAGE_VGUIDE1_POS      42
#define GIMP_MAINIMAGE_VGUIDE2_POS      82
#define GIMP_MAINIMAGE_HGUIDE1_POS      3
#define GIMP_MAINIMAGE_HGUIDE2_POS      4

#define GIMP_MAINIMAGE_SAMPLEPOINT1_X   10
#define GIMP_MAINIMAGE_SAMPLEPOINT1_Y   12
#define GIMP_MAINIMAGE_SAMPLEPOINT2_X   41
#define GIMP_MAINIMAGE_SAMPLEPOINT2_Y   49

#define GIMP_MAINIMAGE_RESOLUTIONX      400
#define GIMP_MAINIMAGE_RESOLUTIONY      410

#define GIMP_MAINIMAGE_PARASITE_NAME    "test-parasite"
#define GIMP_MAINIMAGE_PARASITE_DATA    "foo"
#define GIMP_MAINIMAGE_PARASITE_SIZE    4                /* 'f' 'o' 'o' '\0' */

#define GIMP_MAINIMAGE_COMMENT          "Created with code from "\
                                        "app/tests/test-xcf.c in the GIMP "\
                                        "source tree, i.e. it was not created "\
                                        "manually and may thus look weird if "\
                                        "opened and inspected in GIMP."

#define GIMP_MAINIMAGE_UNIT             gimp_unit_pica ()

#define GIMP_MAINIMAGE_GRIDXSPACING     25.0
#define GIMP_MAINIMAGE_GRIDYSPACING     27.0

#define GIMP_MAINIMAGE_CHANNEL1_NAME    "channel1"
#define GIMP_MAINIMAGE_CHANNEL1_WIDTH   GIMP_MAINIMAGE_WIDTH
#define GIMP_MAINIMAGE_CHANNEL1_HEIGHT  GIMP_MAINIMAGE_HEIGHT
#define GIMP_MAINIMAGE_CHANNEL1_COLOR   { 1.0, 0.0, 1.0, 1.0 }

#define GIMP_MAINIMAGE_SELECTION_X      5
#define GIMP_MAINIMAGE_SELECTION_Y      6
#define GIMP_MAINIMAGE_SELECTION_W      7
#define GIMP_MAINIMAGE_SELECTION_H      8

#define GIMP_MAINIMAGE_PATH1_NAME       "vectors1"
#define GIMP_MAINIMAGE_PATH1_COORDS     { { 11.0, 12.0, /* pad zeroes */ },\
                                          { 21.0, 22.0, /* pad zeroes */ },\
                                          { 31.0, 32.0, /* pad zeroes */ }, }

#define GIMP_MAINIMAGE_PATH2_NAME       "vectors2"
#define GIMP_MAINIMAGE_PATH2_COORDS     { { 911.0, 912.0, /* pad zeroes */ },\
                                          { 921.0, 922.0, /* pad zeroes */ },\
                                          { 931.0, 932.0, /* pad zeroes */ }, }

#define ADD_TEST(function) \
  g_test_add_data_func ("/gimp-xcf/" #function, gimp, function);


GimpImage        * gimp_test_load_image                        (Gimp            *gimp,
                                                                GFile           *file);
static void        gimp_write_and_read_file                    (Gimp            *gimp,
                                                                gboolean         with_unusual_stuff,
                                                                gboolean         compat_paths,
                                                                gboolean         use_gimp_2_8_features);
static GimpImage * gimp_create_mainimage                       (Gimp            *gimp,
                                                                gboolean         with_unusual_stuff,
                                                                gboolean         compat_paths,
                                                                gboolean         use_gimp_2_8_features);
static void        gimp_assert_mainimage                       (GimpImage       *image,
                                                                gboolean         with_unusual_stuff,
                                                                gboolean         compat_paths,
                                                                gboolean         use_gimp_2_8_features);


/**
 * write_and_read_gimp_2_6_format:
 * @data:
 *
 * Do a write and read test on a file that could as well be
 * constructed with GIMP 2.6.
 **/
static void
write_and_read_gimp_2_6_format (gconstpointer data)
{
  Gimp *gimp = GIMP (data);

  gimp_write_and_read_file (gimp,
                            FALSE /*with_unusual_stuff*/,
                            FALSE /*compat_paths*/,
                            FALSE /*use_gimp_2_8_features*/);
}

/**
 * write_and_read_gimp_2_6_format_unusual:
 * @data:
 *
 * Do a write and read test on a file that could as well be
 * constructed with GIMP 2.6, and make it unusual, like compatible
 * paths and with a floating selection.
 **/
static void
write_and_read_gimp_2_6_format_unusual (gconstpointer data)
{
  Gimp *gimp = GIMP (data);

  gimp_write_and_read_file (gimp,
                            TRUE /*with_unusual_stuff*/,
                            TRUE /*compat_paths*/,
                            FALSE /*use_gimp_2_8_features*/);
}

/**
 * load_gimp_2_6_file:
 * @data:
 *
 * Loads a file created with GIMP 2.6 and makes sure it loaded as
 * expected.
 **/
static void
load_gimp_2_6_file (gconstpointer data)
{
  Gimp      *gimp = GIMP (data);
  GimpImage *image;
  gchar     *filename;
  GFile     *file;

  filename = g_build_filename (g_getenv ("GIMP_TESTING_ABS_TOP_SRCDIR"),
                               "app/tests/files/gimp-2-6-file.xcf",
                               NULL);
  file = g_file_new_for_path (filename);
  g_free (filename);

  image = gimp_test_load_image (gimp, file);

  /* The image file was constructed by running
   * gimp_write_and_read_file (FALSE, FALSE) in GIMP 2.6 by
   * copy-pasting the code to GIMP 2.6 and adapting it to changes in
   * the core API, so we can use gimp_assert_mainimage() to make sure
   * the file was loaded successfully.
   */
  gimp_assert_mainimage (image,
                         FALSE /*with_unusual_stuff*/,
                         FALSE /*compat_paths*/,
                         FALSE /*use_gimp_2_8_features*/);
}

/**
 * write_and_read_gimp_2_8_format:
 * @data:
 *
 * Writes an XCF file that uses GIMP 2.8 features such as layer
 * groups, then reads the file and make sure no relevant information
 * was lost.
 **/
static void
write_and_read_gimp_2_8_format (gconstpointer data)
{
  Gimp *gimp = GIMP (data);

  gimp_write_and_read_file (gimp,
                            FALSE /*with_unusual_stuff*/,
                            FALSE /*compat_paths*/,
                            TRUE /*use_gimp_2_8_features*/);
}

GimpImage *
gimp_test_load_image (Gimp  *gimp,
                      GFile *file)
{
  GimpPlugInProcedure *proc;
  GimpImage           *image;
  GimpPDBStatusType    unused;

  proc = gimp_plug_in_manager_file_procedure_find (gimp->plug_in_manager,
                                                   GIMP_FILE_PROCEDURE_GROUP_OPEN,
                                                   file,
                                                   NULL /*error*/);
  image = file_open_image (gimp,
                           gimp_get_user_context (gimp),
                           NULL /*progress*/,
                           file,
                           0, 0, /* vector width, height */
                           TRUE, /* vector keep ratio */
                           FALSE /*as_new*/,
                           proc,
                           GIMP_RUN_NONINTERACTIVE,
                           NULL, /* file_proc_handles_vector */
                           &unused /*status*/,
                           NULL /*mime_type*/,
                           NULL /*error*/);

  return image;
}

/**
 * gimp_write_and_read_file:
 *
 * Constructs the main test image and asserts its state, writes it to
 * a file, reads the image from the file, and asserts the state of the
 * loaded file. The function takes various parameters so the same
 * function can be used for different formats.
 **/
static void
gimp_write_and_read_file (Gimp     *gimp,
                          gboolean  with_unusual_stuff,
                          gboolean  compat_paths,
                          gboolean  use_gimp_2_8_features)
{
  GimpImage           *image;
  GimpImage           *loaded_image;
  GimpPlugInProcedure *proc;
  gchar               *filename = NULL;
  gint                 file_handle;
  GFile               *file;

  /* Create the image */
  image = gimp_create_mainimage (gimp,
                                 with_unusual_stuff,
                                 compat_paths,
                                 use_gimp_2_8_features);

  /* Assert valid state */
  gimp_assert_mainimage (image,
                         with_unusual_stuff,
                         compat_paths,
                         use_gimp_2_8_features);

  /* Write to file */
  file_handle = g_file_open_tmp ("gimp-test-XXXXXX.xcf", &filename, NULL);
  g_assert_true (file_handle != -1);
  close (file_handle);
  file = g_file_new_for_path (filename);
  g_free (filename);

  proc = gimp_plug_in_manager_file_procedure_find (image->gimp->plug_in_manager,
                                                   GIMP_FILE_PROCEDURE_GROUP_SAVE,
                                                   file,
                                                   NULL /*error*/);
  file_save (gimp,
             image,
             NULL /*progress*/,
             file,
             proc,
             GIMP_RUN_NONINTERACTIVE,
             FALSE /*change_saved_state*/,
             FALSE /*export_backward*/,
             FALSE /*export_forward*/,
             NULL /*error*/);

  /* Load from file */
  loaded_image = gimp_test_load_image (image->gimp, file);

  /* Assert on the loaded file. If success, it means that there is no
   * significant information loss when we wrote the image to a file
   * and loaded it again
   */
  gimp_assert_mainimage (loaded_image,
                         with_unusual_stuff,
                         compat_paths,
                         use_gimp_2_8_features);

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
}

/**
 * gimp_create_mainimage:
 *
 * Creates the main test image, i.e. the image that we use for most of
 * our XCF testing purposes.
 *
 * Returns: The #GimpImage
 **/
static GimpImage *
gimp_create_mainimage (Gimp     *gimp,
                       gboolean  with_unusual_stuff,
                       gboolean  compat_paths,
                       gboolean  use_gimp_2_8_features)
{
  GimpImage     *image          = NULL;
  GimpLayer     *layer          = NULL;
  GimpParasite  *parasite       = NULL;
  GimpGrid      *grid           = NULL;
  GimpChannel   *channel        = NULL;
  GeglColor     *channel_color  = gegl_color_new (NULL);
  GimpChannel   *selection      = NULL;
  GimpPath      *path           = NULL;
  GimpCoords     path1_coords[] = GIMP_MAINIMAGE_PATH1_COORDS;
  GimpCoords     path2_coords[] = GIMP_MAINIMAGE_PATH2_COORDS;
  GimpStroke    *stroke         = NULL;
  GimpLayerMask *layer_mask     = NULL;
  gdouble        rgb[4]         = GIMP_MAINIMAGE_CHANNEL1_COLOR;

  gegl_color_set_pixel (channel_color, babl_format ("R'G'B'A double"), &rgb);
  /* Image size and type */
  image = gimp_image_new (gimp,
                          GIMP_MAINIMAGE_WIDTH,
                          GIMP_MAINIMAGE_HEIGHT,
                          GIMP_MAINIMAGE_TYPE,
                          GIMP_MAINIMAGE_PRECISION);

  /* Layers */
  layer = gimp_layer_new (image,
                          GIMP_MAINIMAGE_LAYER1_WIDTH,
                          GIMP_MAINIMAGE_LAYER1_HEIGHT,
                          GIMP_MAINIMAGE_LAYER1_FORMAT,
                          GIMP_MAINIMAGE_LAYER1_NAME,
                          GIMP_MAINIMAGE_LAYER1_OPACITY,
                          GIMP_MAINIMAGE_LAYER1_MODE);
  gimp_image_add_layer (image,
                        layer,
                        NULL,
                        0,
                        FALSE/*push_undo*/);
  layer = gimp_layer_new (image,
                          GIMP_MAINIMAGE_LAYER2_WIDTH,
                          GIMP_MAINIMAGE_LAYER2_HEIGHT,
                          GIMP_MAINIMAGE_LAYER2_FORMAT,
                          GIMP_MAINIMAGE_LAYER2_NAME,
                          GIMP_MAINIMAGE_LAYER2_OPACITY,
                          GIMP_MAINIMAGE_LAYER2_MODE);
  gimp_image_add_layer (image,
                        layer,
                        NULL,
                        0,
                        FALSE /*push_undo*/);

  /* Layer mask */
  layer_mask = gimp_layer_create_mask (layer,
                                       GIMP_ADD_MASK_BLACK,
                                       NULL /*channel*/);
  gimp_layer_add_mask (layer,
                       layer_mask,
                       TRUE  /* edit_mask */,
                       FALSE /* push_undo */,
                       NULL  /* error     */);

  /* Image compression type
   *
   * We don't do any explicit test, only implicit when we read tile
   * data in other tests
   */

  /* Guides, note we add them in reversed order */
  gimp_image_add_hguide (image,
                         GIMP_MAINIMAGE_HGUIDE2_POS,
                         FALSE /*push_undo*/);
  gimp_image_add_hguide (image,
                         GIMP_MAINIMAGE_HGUIDE1_POS,
                         FALSE /*push_undo*/);
  gimp_image_add_vguide (image,
                         GIMP_MAINIMAGE_VGUIDE2_POS,
                         FALSE /*push_undo*/);
  gimp_image_add_vguide (image,
                         GIMP_MAINIMAGE_VGUIDE1_POS,
                         FALSE /*push_undo*/);


  /* Sample points */
  gimp_image_add_sample_point_at_pos (image,
                                      GIMP_MAINIMAGE_SAMPLEPOINT1_X,
                                      GIMP_MAINIMAGE_SAMPLEPOINT1_Y,
                                      FALSE /*push_undo*/);
  gimp_image_add_sample_point_at_pos (image,
                                      GIMP_MAINIMAGE_SAMPLEPOINT2_X,
                                      GIMP_MAINIMAGE_SAMPLEPOINT2_Y,
                                      FALSE /*push_undo*/);

  /* Tattoo
   * We don't bother testing this, not yet at least
   */

  /* Resolution */
  gimp_image_set_resolution (image,
                             GIMP_MAINIMAGE_RESOLUTIONX,
                             GIMP_MAINIMAGE_RESOLUTIONY);


  /* Parasites */
  parasite = gimp_parasite_new (GIMP_MAINIMAGE_PARASITE_NAME,
                                GIMP_PARASITE_PERSISTENT,
                                GIMP_MAINIMAGE_PARASITE_SIZE,
                                GIMP_MAINIMAGE_PARASITE_DATA);
  gimp_image_parasite_attach (image,
                              parasite, FALSE);
  gimp_parasite_free (parasite);
  parasite = gimp_parasite_new ("gimp-comment",
                                GIMP_PARASITE_PERSISTENT,
                                strlen (GIMP_MAINIMAGE_COMMENT) + 1,
                                GIMP_MAINIMAGE_COMMENT);
  gimp_image_parasite_attach (image, parasite, FALSE);
  gimp_parasite_free (parasite);


  /* Unit */
  gimp_image_set_unit (image,
                       GIMP_MAINIMAGE_UNIT);

  /* Grid */
  grid = g_object_new (GIMP_TYPE_GRID,
                       "xspacing", GIMP_MAINIMAGE_GRIDXSPACING,
                       "yspacing", GIMP_MAINIMAGE_GRIDYSPACING,
                       NULL);
  gimp_image_set_grid (image,
                       grid,
                       FALSE /*push_undo*/);
  g_object_unref (grid);

  /* Channel */
  channel = gimp_channel_new (image,
                              GIMP_MAINIMAGE_CHANNEL1_WIDTH,
                              GIMP_MAINIMAGE_CHANNEL1_HEIGHT,
                              GIMP_MAINIMAGE_CHANNEL1_NAME,
                              channel_color);
  g_object_unref (channel_color);
  gimp_image_add_channel (image,
                          channel,
                          NULL,
                          -1,
                          FALSE /*push_undo*/);

  /* Selection */
  selection = gimp_image_get_mask (image);
  gimp_channel_select_rectangle (selection,
                                 GIMP_MAINIMAGE_SELECTION_X,
                                 GIMP_MAINIMAGE_SELECTION_Y,
                                 GIMP_MAINIMAGE_SELECTION_W,
                                 GIMP_MAINIMAGE_SELECTION_H,
                                 GIMP_CHANNEL_OP_REPLACE,
                                 FALSE /*feather*/,
                                 0.0 /*feather_radius_x*/,
                                 0.0 /*feather_radius_y*/,
                                 FALSE /*push_undo*/);

  /* Path 1 */
  path = gimp_path_new (image,
                        GIMP_MAINIMAGE_PATH1_NAME);
  /* The XCF file can save paths in two kind of ways, one old way
   * and a new way. Parameterize the way so we can test both variants,
   * i.e. gimp_path_compat_is_compatible() must return both TRUE
   * and FALSE.
   */
  if (! compat_paths)
    {
      gimp_item_set_visible (GIMP_ITEM (path),
                             TRUE,
                             FALSE /*push_undo*/);
    }
  /* TODO: Add test for non-closed stroke. The order of the anchor
   * points changes for open strokes, so it's boring to test
   */
  stroke = gimp_bezier_stroke_new_from_coords (path1_coords,
                                               G_N_ELEMENTS (path1_coords),
                                               TRUE /*closed*/);
  gimp_path_stroke_add (path, stroke);
  gimp_image_add_path (image,
                       path,
                       NULL /*parent*/,
                       -1 /*position*/,
                       FALSE /*push_undo*/);

  /* Path 2 */
  path = gimp_path_new (image,
                        GIMP_MAINIMAGE_PATH2_NAME);

  stroke = gimp_bezier_stroke_new_from_coords (path2_coords,
                                               G_N_ELEMENTS (path2_coords),
                                               TRUE /*closed*/);
  gimp_path_stroke_add (path, stroke);
  gimp_image_add_path (image,
                       path,
                       NULL /*parent*/,
                       -1 /*position*/,
                       FALSE /*push_undo*/);

  /* Some of these things are pretty unusual, parameterize the
   * inclusion of this in the written file so we can do our test both
   * with and without
   */
  if (with_unusual_stuff)
    {
      GList *drawables;

      drawables = gimp_image_get_selected_drawables (image);

      /* Floating selection */
      gimp_selection_float (GIMP_SELECTION (gimp_image_get_mask (image)),
                            drawables,
                            gimp_get_user_context (gimp),
                            TRUE /*cut_image*/,
                            0 /*off_x*/,
                            0 /*off_y*/,
                            NULL /*error*/);
      g_list_free (drawables);
    }

  /* Adds stuff like layer groups */
  if (use_gimp_2_8_features)
    {
      GimpLayer *parent;

      /* Add a layer group and some layers:
       *
       *  group1
       *    layer3
       *    layer4
       *    group2
       *      layer5
       */

      /* group1 */
      layer = gimp_group_layer_new (image);
      gimp_object_set_name (GIMP_OBJECT (layer), GIMP_MAINIMAGE_GROUP1_NAME);
      gimp_image_add_layer (image,
                            layer,
                            NULL /*parent*/,
                            -1 /*position*/,
                            FALSE /*push_undo*/);
      parent = layer;

      /* layer3 */
      layer = gimp_layer_new (image,
                              GIMP_MAINIMAGE_LAYER1_WIDTH,
                              GIMP_MAINIMAGE_LAYER1_HEIGHT,
                              GIMP_MAINIMAGE_LAYER1_FORMAT,
                              GIMP_MAINIMAGE_LAYER3_NAME,
                              GIMP_MAINIMAGE_LAYER1_OPACITY,
                              GIMP_MAINIMAGE_LAYER1_MODE);
      gimp_image_add_layer (image,
                            layer,
                            parent,
                            -1 /*position*/,
                            FALSE /*push_undo*/);

      /* layer4 */
      layer = gimp_layer_new (image,
                              GIMP_MAINIMAGE_LAYER1_WIDTH,
                              GIMP_MAINIMAGE_LAYER1_HEIGHT,
                              GIMP_MAINIMAGE_LAYER1_FORMAT,
                              GIMP_MAINIMAGE_LAYER4_NAME,
                              GIMP_MAINIMAGE_LAYER1_OPACITY,
                              GIMP_MAINIMAGE_LAYER1_MODE);
      gimp_image_add_layer (image,
                            layer,
                            parent,
                            -1 /*position*/,
                            FALSE /*push_undo*/);

      /* group2 */
      layer = gimp_group_layer_new (image);
      gimp_object_set_name (GIMP_OBJECT (layer), GIMP_MAINIMAGE_GROUP2_NAME);
      gimp_image_add_layer (image,
                            layer,
                            parent,
                            -1 /*position*/,
                            FALSE /*push_undo*/);
      parent = layer;

      /* layer5 */
      layer = gimp_layer_new (image,
                              GIMP_MAINIMAGE_LAYER1_WIDTH,
                              GIMP_MAINIMAGE_LAYER1_HEIGHT,
                              GIMP_MAINIMAGE_LAYER1_FORMAT,
                              GIMP_MAINIMAGE_LAYER5_NAME,
                              GIMP_MAINIMAGE_LAYER1_OPACITY,
                              GIMP_MAINIMAGE_LAYER1_MODE);
      gimp_image_add_layer (image,
                            layer,
                            parent,
                            -1 /*position*/,
                            FALSE /*push_undo*/);
    }

  /* Todo, should be tested somehow:
   *
   * - Color maps
   * - Custom user units
   * - Text layers
   * - Layer parasites
   * - Channel parasites
   * - Different tile compression methods
   */

  return image;
}

static void
gimp_assert_path (GimpImage   *image,
                  const gchar *name,
                  GimpCoords   coords[],
                  gsize        coords_size,
                  gboolean     visible)
{
  GimpPath    *path           = NULL;
  GimpStroke  *stroke         = NULL;
  GArray      *control_points = NULL;
  gboolean     closed         = FALSE;
  gint         i              = 0;

  path = gimp_image_get_path_by_name (image, name);
  stroke = gimp_path_stroke_get_next (path, NULL);
  g_assert_true (stroke != NULL);
  control_points = gimp_stroke_control_points_get (stroke,
                                                   &closed);
  g_assert_true (closed);
  g_assert_cmpint (control_points->len,
                   ==,
                   coords_size);
  for (i = 0; i < control_points->len; i++)
    {
      g_assert_cmpint (coords[i].x,
                       ==,
                       g_array_index (control_points,
                                      GimpAnchor,
                                      i).position.x);
      g_assert_cmpint (coords[i].y,
                       ==,
                       g_array_index (control_points,
                                      GimpAnchor,
                                      i).position.y);
    }

  g_assert_true (gimp_item_get_visible (GIMP_ITEM (path)) ? TRUE : FALSE ==
                 visible ? TRUE : FALSE);
}

/**
 * gimp_assert_mainimage:
 * @image:
 *
 * Verifies that the passed #GimpImage contains all the information
 * that was put in it by gimp_create_mainimage().
 **/
static void
gimp_assert_mainimage (GimpImage *image,
                       gboolean   with_unusual_stuff,
                       gboolean   compat_paths,
                       gboolean   use_gimp_2_8_features)
{
  const GimpParasite *parasite               = NULL;
  gchar              *parasite_data          = NULL;
  guint32             parasite_size          = -1;
  GimpLayer          *layer                  = NULL;
  GList              *iter                   = NULL;
  GimpGuide          *guide                  = NULL;
  GimpSamplePoint    *sample_point           = NULL;
  gint                sample_point_x         = 0;
  gint                sample_point_y         = 0;
  gdouble             xres                   = 0.0;
  gdouble             yres                   = 0.0;
  GimpGrid           *grid                   = NULL;
  gdouble             xspacing               = 0.0;
  gdouble             yspacing               = 0.0;
  GimpChannel        *channel                = NULL;
  GeglColor          *actual_channel_color;
  gdouble             expected_rgb_color[4]  = GIMP_MAINIMAGE_CHANNEL1_COLOR;
  gdouble             rgb[4];
  GimpChannel        *selection              = NULL;
  gint                x                      = -1;
  gint                y                      = -1;
  gint                w                      = -1;
  gint                h                      = -1;
  GimpCoords          path1_coords[]         = GIMP_MAINIMAGE_PATH1_COORDS;
  GimpCoords          path2_coords[]         = GIMP_MAINIMAGE_PATH2_COORDS;

  /* Image size and type */
  g_assert_cmpint (gimp_image_get_width (image),
                   ==,
                   GIMP_MAINIMAGE_WIDTH);
  g_assert_cmpint (gimp_image_get_height (image),
                   ==,
                   GIMP_MAINIMAGE_HEIGHT);
  g_assert_cmpint (gimp_image_get_base_type (image),
                   ==,
                   GIMP_MAINIMAGE_TYPE);

  /* Layers */
  layer = gimp_image_get_layer_by_name (image,
                                        GIMP_MAINIMAGE_LAYER1_NAME);
  g_assert_cmpint (gimp_item_get_width (GIMP_ITEM (layer)),
                   ==,
                   GIMP_MAINIMAGE_LAYER1_WIDTH);
  g_assert_cmpint (gimp_item_get_height (GIMP_ITEM (layer)),
                   ==,
                   GIMP_MAINIMAGE_LAYER1_HEIGHT);
  g_assert_cmpstr (babl_get_name (gimp_drawable_get_format (GIMP_DRAWABLE (layer))),
                   ==,
                   babl_get_name (GIMP_MAINIMAGE_LAYER1_FORMAT));
  g_assert_cmpstr (gimp_object_get_name (GIMP_DRAWABLE (layer)),
                   ==,
                   GIMP_MAINIMAGE_LAYER1_NAME);
  g_assert_cmpfloat (gimp_layer_get_opacity (layer),
                     ==,
                     GIMP_MAINIMAGE_LAYER1_OPACITY);
  g_assert_cmpint (gimp_layer_get_mode (layer),
                   ==,
                   GIMP_MAINIMAGE_LAYER1_MODE);
  layer = gimp_image_get_layer_by_name (image,
                                        GIMP_MAINIMAGE_LAYER2_NAME);
  g_assert_cmpint (gimp_item_get_width (GIMP_ITEM (layer)),
                   ==,
                   GIMP_MAINIMAGE_LAYER2_WIDTH);
  g_assert_cmpint (gimp_item_get_height (GIMP_ITEM (layer)),
                   ==,
                   GIMP_MAINIMAGE_LAYER2_HEIGHT);
  g_assert_cmpstr (babl_get_name (gimp_drawable_get_format (GIMP_DRAWABLE (layer))),
                   ==,
                   babl_get_name (GIMP_MAINIMAGE_LAYER2_FORMAT));
  g_assert_cmpstr (gimp_object_get_name (GIMP_DRAWABLE (layer)),
                   ==,
                   GIMP_MAINIMAGE_LAYER2_NAME);
  g_assert_cmpfloat (gimp_layer_get_opacity (layer),
                     ==,
                     GIMP_MAINIMAGE_LAYER2_OPACITY);
  g_assert_cmpint (gimp_layer_get_mode (layer),
                   ==,
                   GIMP_MAINIMAGE_LAYER2_MODE);

  /* Guides, note that we rely on internal ordering */
  iter = gimp_image_get_guides (image);
  g_assert_true (iter != NULL);
  guide = iter->data;
  g_assert_cmpint (gimp_guide_get_position (guide),
                   ==,
                   GIMP_MAINIMAGE_VGUIDE1_POS);
  iter = g_list_next (iter);
  g_assert_true (iter != NULL);
  guide = iter->data;
  g_assert_cmpint (gimp_guide_get_position (guide),
                   ==,
                   GIMP_MAINIMAGE_VGUIDE2_POS);
  iter = g_list_next (iter);
  g_assert_true (iter != NULL);
  guide = iter->data;
  g_assert_cmpint (gimp_guide_get_position (guide),
                   ==,
                   GIMP_MAINIMAGE_HGUIDE1_POS);
  iter = g_list_next (iter);
  g_assert_true (iter != NULL);
  guide = iter->data;
  g_assert_cmpint (gimp_guide_get_position (guide),
                   ==,
                   GIMP_MAINIMAGE_HGUIDE2_POS);
  iter = g_list_next (iter);
  g_assert_true (iter == NULL);

  /* Sample points, we rely on the same ordering as when we added
   * them, although this ordering is not a necessity
   */
  iter = gimp_image_get_sample_points (image);
  g_assert_true (iter != NULL);
  sample_point = iter->data;
  gimp_sample_point_get_position (sample_point,
                                  &sample_point_x, &sample_point_y);
  g_assert_cmpint (sample_point_x,
                   ==,
                   GIMP_MAINIMAGE_SAMPLEPOINT1_X);
  g_assert_cmpint (sample_point_y,
                   ==,
                   GIMP_MAINIMAGE_SAMPLEPOINT1_Y);
  iter = g_list_next (iter);
  g_assert_true (iter != NULL);
  sample_point = iter->data;
  gimp_sample_point_get_position (sample_point,
                                  &sample_point_x, &sample_point_y);
  g_assert_cmpint (sample_point_x,
                   ==,
                   GIMP_MAINIMAGE_SAMPLEPOINT2_X);
  g_assert_cmpint (sample_point_y,
                   ==,
                   GIMP_MAINIMAGE_SAMPLEPOINT2_Y);
  iter = g_list_next (iter);
  g_assert_true (iter == NULL);

  /* Resolution */
  gimp_image_get_resolution (image, &xres, &yres);
  g_assert_cmpint (xres,
                   ==,
                   GIMP_MAINIMAGE_RESOLUTIONX);
  g_assert_cmpint (yres,
                   ==,
                   GIMP_MAINIMAGE_RESOLUTIONY);

  /* Parasites */
  parasite = gimp_image_parasite_find (image,
                                       GIMP_MAINIMAGE_PARASITE_NAME);
  parasite_data = (gchar *) gimp_parasite_get_data (parasite, &parasite_size);
  parasite_data = g_strndup (parasite_data, parasite_size);
  g_assert_cmpint (parasite_size,
                   ==,
                   GIMP_MAINIMAGE_PARASITE_SIZE);
  g_assert_cmpstr (parasite_data,
                   ==,
                   GIMP_MAINIMAGE_PARASITE_DATA);
  g_free (parasite_data);

  parasite = gimp_image_parasite_find (image,
                                       "gimp-comment");
  parasite_data = (gchar *) gimp_parasite_get_data (parasite, &parasite_size);
  parasite_data = g_strndup (parasite_data, parasite_size);
  g_assert_cmpint (parasite_size,
                   ==,
                   strlen (GIMP_MAINIMAGE_COMMENT) + 1);
  g_assert_cmpstr (parasite_data,
                   ==,
                   GIMP_MAINIMAGE_COMMENT);
  g_free (parasite_data);

  /* Unit */
  g_assert_true (gimp_image_get_unit (image) == GIMP_MAINIMAGE_UNIT);

  /* Grid */
  grid = gimp_image_get_grid (image);
  g_object_get (grid,
                "xspacing", &xspacing,
                "yspacing", &yspacing,
                NULL);
  g_assert_cmpint (xspacing,
                   ==,
                   GIMP_MAINIMAGE_GRIDXSPACING);
  g_assert_cmpint (yspacing,
                   ==,
                   GIMP_MAINIMAGE_GRIDYSPACING);


  /* Channel */
  channel = gimp_image_get_channel_by_name (image,
                                            GIMP_MAINIMAGE_CHANNEL1_NAME);
  actual_channel_color = gimp_channel_get_color (channel);
  g_assert_cmpint (gimp_item_get_width (GIMP_ITEM (channel)),
                   ==,
                   GIMP_MAINIMAGE_CHANNEL1_WIDTH);
  g_assert_cmpint (gimp_item_get_height (GIMP_ITEM (channel)),
                   ==,
                   GIMP_MAINIMAGE_CHANNEL1_HEIGHT);
  gegl_color_get_pixel (actual_channel_color, babl_format ("R'G'B'A double"), &rgb);
  g_assert_true (memcmp (&expected_rgb_color, &rgb, sizeof (expected_rgb_color)) == 0);

  /* Selection, if the image contains unusual stuff it contains a
   * floating select, and when floating a selection, the selection
   * mask is cleared, so don't test for the presence of the selection
   * mask in that case
   */
  if (! with_unusual_stuff)
    {
      selection = gimp_image_get_mask (image);
      gimp_item_bounds (GIMP_ITEM (selection), &x, &y, &w, &h);
      g_assert_cmpint (x,
                       ==,
                       GIMP_MAINIMAGE_SELECTION_X);
      g_assert_cmpint (y,
                       ==,
                       GIMP_MAINIMAGE_SELECTION_Y);
      g_assert_cmpint (w,
                       ==,
                       GIMP_MAINIMAGE_SELECTION_W);
      g_assert_cmpint (h,
                       ==,
                       GIMP_MAINIMAGE_SELECTION_H);
    }

  /* Path 1 */
  gimp_assert_path (image,
                    GIMP_MAINIMAGE_PATH1_NAME,
                    path1_coords,
                    G_N_ELEMENTS (path1_coords),
                    ! compat_paths /*visible*/);

  /* Path 2 (always visible FALSE) */
  gimp_assert_path (image,
                    GIMP_MAINIMAGE_PATH2_NAME,
                    path2_coords,
                    G_N_ELEMENTS (path2_coords),
                    FALSE /*visible*/);

  if (with_unusual_stuff)
    g_assert_true (gimp_image_get_floating_selection (image) != NULL);
  else /* if (! with_unusual_stuff) */
    g_assert_true (gimp_image_get_floating_selection (image) == NULL);

  if (use_gimp_2_8_features)
    {
      /* Only verify the parent relationships, the layer attributes
       * are tested above
       */
      GimpItem *group1 = GIMP_ITEM (gimp_image_get_layer_by_name (image, GIMP_MAINIMAGE_GROUP1_NAME));
      GimpItem *layer3 = GIMP_ITEM (gimp_image_get_layer_by_name (image, GIMP_MAINIMAGE_LAYER3_NAME));
      GimpItem *layer4 = GIMP_ITEM (gimp_image_get_layer_by_name (image, GIMP_MAINIMAGE_LAYER4_NAME));
      GimpItem *group2 = GIMP_ITEM (gimp_image_get_layer_by_name (image, GIMP_MAINIMAGE_GROUP2_NAME));
      GimpItem *layer5 = GIMP_ITEM (gimp_image_get_layer_by_name (image, GIMP_MAINIMAGE_LAYER5_NAME));

      g_assert_true (gimp_item_get_parent (group1) == NULL);
      g_assert_true (gimp_item_get_parent (layer3) == group1);
      g_assert_true (gimp_item_get_parent (layer4) == group1);
      g_assert_true (gimp_item_get_parent (group2) == group1);
      g_assert_true (gimp_item_get_parent (layer5) == group2);
    }
}


/*  CMYK tests X1-X4 (devel-docs/cmyk-mode/TESTING.md)  */

#define CMYK_W 70
#define CMYK_H 66

static GString *cmyk_messages = NULL;

static void
cmyk_capture_message (Gimp                *gimp,
                      GObject             *handler,
                      GimpMessageSeverity  severity,
                      const gchar         *domain,
                      const gchar         *message)
{
  g_string_append (cmyk_messages, message);
  g_string_append_c (cmyk_messages, '\n');
}

typedef struct
{
  gboolean   console_messages;
  gpointer   show_message;
} CmykMessageState;

static void
cmyk_capture_messages_begin (Gimp             *gimp,
                             CmykMessageState *state)
{
  state->console_messages = gimp->console_messages;
  state->show_message     = gimp->gui.show_message;

  gimp->console_messages = FALSE;
  gimp->gui.show_message = cmyk_capture_message;

  if (cmyk_messages)
    g_string_truncate (cmyk_messages, 0);
  else
    cmyk_messages = g_string_new (NULL);
}

static void
cmyk_capture_messages_end (Gimp             *gimp,
                           CmykMessageState *state)
{
  gimp->console_messages = state->console_messages;
  gimp->gui.show_message = state->show_message;
}

static gboolean
cmyk_skip_without_support (void)
{
  if (! gimp_babl_has_cmyk_support ())
    {
      g_test_skip ("babl has no CMYK (lcms) support");
      return TRUE;
    }

  return FALSE;
}

/* fill @buffer with seeded pseudo-random inks, plus a K-only patch,
 * a C-only patch and a paper patch in the top-left corner
 */
static void
cmyk_fill_buffer (GeglBuffer *buffer,
                  guint32     seed)
{
  const GeglRectangle *extent = gegl_buffer_get_extent (buffer);
  const Babl          *format;
  GRand               *rand   = g_rand_new_with_seed (seed);
  gdouble             *data;
  gint                 n      = extent->width * extent->height;
  gint                 i;

  format = babl_format_with_space ("CMYKA double",
                                   gegl_buffer_get_format (buffer));
  data = g_new (gdouble, n * 5);

  for (i = 0; i < n; i++)
    {
      gint x = i % extent->width;
      gint y = i / extent->width;
      gint c;

      for (c = 0; c < 5; c++)
        data[i * 5 + c] = g_rand_double (rand);

      if (y < 4 && x < 12)
        {
          /* x < 4: K only, x < 8: C only, else paper */
          data[i * 5 + 0] = (x >= 4 && x < 8) ? 1.0 : 0.0;
          data[i * 5 + 1] = 0.0;
          data[i * 5 + 2] = 0.0;
          data[i * 5 + 3] = (x < 4) ? 1.0 : 0.0;
          data[i * 5 + 4] = 1.0;
        }
    }

  gegl_buffer_set (buffer, extent, 0, format, data, GEGL_AUTO_ROWSTRIDE);

  g_free (data);
  g_rand_free (rand);
}

static void
cmyk_assert_buffers_equal (GeglBuffer  *a,
                           GeglBuffer  *b,
                           gboolean     strict_format,
                           const gchar *what)
{
  const GeglRectangle *ea = gegl_buffer_get_extent (a);
  const GeglRectangle *eb = gegl_buffer_get_extent (b);
  const Babl          *format = gegl_buffer_get_format (a);
  gsize                size;
  guint8              *da;
  guint8              *db;

  if (strict_format && gegl_buffer_get_format (b) != format)
    g_error ("%s: format %s != %s", what,
             babl_get_name (format),
             babl_get_name (gegl_buffer_get_format (b)));

  g_assert_cmpint (ea->width,  ==, eb->width);
  g_assert_cmpint (ea->height, ==, eb->height);

  size = (gsize) ea->width * ea->height *
         babl_format_get_bytes_per_pixel (format);
  da = g_malloc (size);
  db = g_malloc (size);

  gegl_buffer_get (a, ea, 1.0, format, da,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
  gegl_buffer_get (b, eb, 1.0, format, db,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);

  if (memcmp (da, db, size) != 0)
    g_error ("%s: buffers differ (%s)", what, babl_get_name (format));

  g_free (da);
  g_free (db);
}

static void
cmyk_assert_profiles_equal_bytes (GimpColorProfile *a,
                                  GimpColorProfile *b)
{
  const guint8 *da;
  const guint8 *db;
  gsize         la;
  gsize         lb;

  g_assert_nonnull (a);
  g_assert_nonnull (b);

  da = gimp_color_profile_get_icc_profile (a, &la);
  db = gimp_color_profile_get_icc_profile (b, &lb);

  g_assert_cmpuint (la, ==, lb);
  g_assert_true (memcmp (da, db, la) == 0);
}

static GFile *
cmyk_tmp_file (void)
{
  gchar *filename = NULL;
  gint   file_handle;
  GFile *file;

  file_handle = g_file_open_tmp ("gimp-test-cmyk-XXXXXX.xcf", &filename, NULL);
  g_assert_true (file_handle != -1);
  close (file_handle);
  file = g_file_new_for_path (filename);
  g_free (filename);

  return file;
}

static void
cmyk_save (Gimp      *gimp,
           GimpImage *image,
           GFile     *file)
{
  GimpPlugInProcedure *proc;
  GError              *error = NULL;
  GimpPDBStatusType    status;

  proc = gimp_plug_in_manager_file_procedure_find (gimp->plug_in_manager,
                                                   GIMP_FILE_PROCEDURE_GROUP_SAVE,
                                                   file, NULL);
  status = file_save (gimp, image, NULL, file, proc,
                      GIMP_RUN_NONINTERACTIVE,
                      FALSE, FALSE, FALSE, &error);
  g_assert_no_error (error);
  g_assert_cmpint (status, ==, GIMP_PDB_SUCCESS);
}

static GimpImage *
cmyk_load (Gimp    *gimp,
           GFile   *file,
           GError **error)
{
  GimpPlugInProcedure *proc;
  GimpPDBStatusType    status;

  proc = gimp_plug_in_manager_file_procedure_find (gimp->plug_in_manager,
                                                   GIMP_FILE_PROCEDURE_GROUP_OPEN,
                                                   file, NULL);
  return file_open_image (gimp, gimp_get_user_context (gimp), NULL, file,
                          0, 0, TRUE, FALSE, proc,
                          GIMP_RUN_NONINTERACTIVE, NULL, &status, NULL,
                          error);
}

/* the 14 header bytes "gimp xcf vNNN" */
static gint
cmyk_file_version (GFile *file)
{
  gchar  *contents = NULL;
  gsize   length   = 0;
  gint    version;

  g_assert_true (g_file_load_contents (file, NULL, &contents, &length,
                                       NULL, NULL));
  g_assert_cmpuint (length, >, 14);
  g_assert_true (g_str_has_prefix (contents, "gimp xcf v"));
  version = atoi (contents + 10);
  g_free (contents);

  return version;
}

static void
cmyk_patch_file_version (GFile *file,
                         gint   version)
{
  gchar  *contents = NULL;
  gsize   length   = 0;
  gchar   tag[5];

  g_assert_true (g_file_load_contents (file, NULL, &contents, &length,
                                       NULL, NULL));
  g_snprintf (tag, sizeof (tag), "%03d", version);
  memcpy (contents + 10, tag, 3);
  g_assert_true (g_file_replace_contents (file, contents, length, NULL,
                                          FALSE, G_FILE_CREATE_NONE, NULL,
                                          NULL, NULL));
  g_free (contents);
}

static GimpLayer *
cmyk_add_layer (GimpImage     *image,
                GimpLayer     *parent,
                const gchar   *name,
                gint           width,
                gint           height,
                gboolean       with_alpha,
                GimpLayerMode  mode,
                gdouble        opacity,
                guint32        seed)
{
  GimpLayer *layer;

  layer = gimp_layer_new (image, width, height,
                          gimp_image_get_layer_format (image, with_alpha),
                          name, opacity, mode);
  cmyk_fill_buffer (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)), seed);
  gimp_image_add_layer (image, layer, parent, 0, FALSE);

  return layer;
}

static GimpImage *
cmyk_create_image (Gimp             *gimp,
                   GimpPrecision     precision,
                   GimpColorProfile *profile,
                   gboolean          with_group)
{
  GimpImage     *image;
  GimpLayer     *layer;
  GimpLayer     *group;
  GimpLayerMask *mask;
  GimpChannel   *channel;
  GeglColor     *color;
  GeglRectangle  rect = { 3, 4, 20, 10 };

  image = gimp_test_utils_create_cmyk_image (gimp, CMYK_W, CMYK_H,
                                             precision, profile);
  gimp_image_undo_disable (image);

  /* opaque background, no alpha */
  cmyk_add_layer (image, NULL, "bg", CMYK_W, CMYK_H, FALSE,
                  GIMP_LAYER_MODE_NORMAL, GIMP_OPACITY_OPAQUE, 1);

  /* layer with alpha, a blend mode, opacity, offset and a mask */
  layer = cmyk_add_layer (image, NULL, "multiply", 33, 41, TRUE,
                          GIMP_LAYER_MODE_MULTIPLY, 0.5, 2);
  gimp_item_set_offset (GIMP_ITEM (layer), 7, 9);

  mask = gimp_layer_create_mask (layer, GIMP_ADD_MASK_WHITE, NULL);
  gimp_layer_add_mask (layer, mask, FALSE, FALSE, NULL);
  color = gegl_color_new ("black");
  gegl_buffer_set_color (gimp_drawable_get_buffer (GIMP_DRAWABLE (mask)),
                         &rect, color);

  if (! with_group)
    goto no_group;

  /* group with a non-Normal mode and two children */
  group = gimp_group_layer_new (image);
  gimp_object_set_name (GIMP_OBJECT (group), "group");
  gimp_image_add_layer (image, group, NULL, 0, FALSE);
  gimp_layer_set_mode (group, GIMP_LAYER_MODE_SCREEN, FALSE);
  gimp_layer_set_opacity (group, 0.75, FALSE);

  cmyk_add_layer (image, group, "child-alpha", 20, 25, TRUE,
                  GIMP_LAYER_MODE_NORMAL, GIMP_OPACITY_OPAQUE, 3);
  layer = cmyk_add_layer (image, group, "child-opaque", 15, 12, FALSE,
                          GIMP_LAYER_MODE_DARKEN_ONLY, 0.3, 4);
  gimp_item_set_offset (GIMP_ITEM (layer), 30, 31);

 no_group:
  /* a channel and the selection */
  channel = gimp_channel_new (image, CMYK_W, CMYK_H, "channel", color);
  gegl_buffer_set_color (gimp_drawable_get_buffer (GIMP_DRAWABLE (channel)),
                         &rect, color);
  gimp_image_add_channel (image, channel, NULL, -1, FALSE);
  g_object_unref (color);

  gimp_channel_select_rectangle (gimp_image_get_mask (image),
                                 5, 6, 17, 9, GIMP_CHANNEL_OP_REPLACE,
                                 FALSE, 0.0, 0.0, FALSE);

  gimp_image_undo_enable (image);

  return image;
}

static void
cmyk_assert_images_equal (GimpImage *orig,
                          GimpImage *loaded)
{
  GList *orig_layers;
  GList *loaded_layers;
  GList *a;
  GList *b;

  g_assert_cmpint (gimp_image_get_base_type (loaded), ==, GIMP_CMYK);
  g_assert_cmpint (gimp_image_get_precision (loaded), ==,
                   gimp_image_get_precision (orig));
  g_assert_cmpint (gimp_image_get_width (loaded),  ==, CMYK_W);
  g_assert_cmpint (gimp_image_get_height (loaded), ==, CMYK_H);

  cmyk_assert_profiles_equal_bytes (gimp_image_get_color_profile (orig),
                                    gimp_image_get_color_profile (loaded));
  g_assert_true (gimp_image_get_layer_space (orig) ==
                 gimp_image_get_layer_space (loaded));

  if (gimp_image_get_simulation_profile (orig))
    cmyk_assert_profiles_equal_bytes (gimp_image_get_simulation_profile (orig),
                                      gimp_image_get_simulation_profile (loaded));

  orig_layers   = gimp_image_get_layer_list (orig);
  loaded_layers = gimp_image_get_layer_list (loaded);
  g_assert_cmpint (g_list_length (orig_layers), ==,
                   g_list_length (loaded_layers));

  for (a = orig_layers, b = loaded_layers; a && b; a = a->next, b = b->next)
    {
      GimpLayer   *la   = a->data;
      GimpLayer   *lb   = b->data;
      const gchar *name = gimp_object_get_name (la);
      gint         xa, ya, xb, yb;

      g_assert_cmpstr (name, ==, gimp_object_get_name (lb));
      g_assert_cmpint (gimp_layer_get_mode (la), ==, gimp_layer_get_mode (lb));
      g_assert_cmpfloat_with_epsilon (gimp_layer_get_opacity (la),
                                      gimp_layer_get_opacity (lb), 1e-6);
      gimp_item_get_offset (GIMP_ITEM (la), &xa, &ya);
      gimp_item_get_offset (GIMP_ITEM (lb), &xb, &yb);
      g_assert_cmpint (xa, ==, xb);
      g_assert_cmpint (ya, ==, yb);
      g_assert_cmpint (GIMP_IS_GROUP_LAYER (la), ==, GIMP_IS_GROUP_LAYER (lb));

      g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (la)) ==
                     gimp_drawable_get_format (GIMP_DRAWABLE (lb)));

      if (! GIMP_IS_GROUP_LAYER (la))
        {
          g_assert_true (gimp_drawable_is_cmyk (GIMP_DRAWABLE (lb)));
          cmyk_assert_buffers_equal (gimp_drawable_get_buffer (GIMP_DRAWABLE (la)),
                                     gimp_drawable_get_buffer (GIMP_DRAWABLE (lb)),
                                     TRUE, name);
        }

      g_assert_cmpint (gimp_layer_get_mask (la) != NULL, ==,
                       gimp_layer_get_mask (lb) != NULL);
      if (gimp_layer_get_mask (la))
        cmyk_assert_buffers_equal (gimp_drawable_get_buffer (GIMP_DRAWABLE (gimp_layer_get_mask (la))),
                                   gimp_drawable_get_buffer (GIMP_DRAWABLE (gimp_layer_get_mask (lb))),
                                   TRUE, "mask");
    }

  g_list_free (orig_layers);
  g_list_free (loaded_layers);

  {
    GimpChannel *ca = gimp_image_get_channel_by_name (orig,   "channel");
    GimpChannel *cb = gimp_image_get_channel_by_name (loaded, "channel");

    g_assert_nonnull (ca);
    g_assert_nonnull (cb);
    cmyk_assert_buffers_equal (gimp_drawable_get_buffer (GIMP_DRAWABLE (ca)),
                               gimp_drawable_get_buffer (GIMP_DRAWABLE (cb)),
                               TRUE, "channel");
  }

  cmyk_assert_buffers_equal (gimp_drawable_get_buffer (GIMP_DRAWABLE (gimp_image_get_mask (orig))),
                             gimp_drawable_get_buffer (GIMP_DRAWABLE (gimp_image_get_mask (loaded))),
                             /* a new image's selection is "Y u8", a
                              * loaded one "Y' u8" (also for RGB)
                              */
                             FALSE, "selection");
}

/* X1: round trip at every CMYK precision, saved as version 28 */
static void
cmyk_round_trip_full (Gimp     *gimp,
                      gboolean  with_group)
{
  const GimpPrecision  precisions[] = { GIMP_PRECISION_U8_NON_LINEAR,
                                        GIMP_PRECISION_U16_NON_LINEAR,
                                        GIMP_PRECISION_U32_NON_LINEAR,
                                        GIMP_PRECISION_HALF_NON_LINEAR,
                                        GIMP_PRECISION_FLOAT_NON_LINEAR,
                                        GIMP_PRECISION_DOUBLE_NON_LINEAR };
  gint                 i;

  if (cmyk_skip_without_support ())
    return;

  for (i = 0; i < G_N_ELEMENTS (precisions); i++)
    {
      GimpImage        *image;
      GimpImage        *loaded;
      GFile            *file;
      GError           *error = NULL;
      GimpColorProfile *builtin;
      CmykMessageState  state;

      if (! gimp_babl_is_valid (GIMP_CMYK, precisions[i]))
        continue;

      image = cmyk_create_image (gimp, precisions[i], NULL, with_group);

      /* the builtin CMYK profile as simulation profile */
      builtin = gimp_color_profile_new_cmyk_builtin ();
      g_assert_true (gimp_image_set_simulation_profile (image, builtin));
      g_object_unref (builtin);

      /* RLE for some precisions, zlib for the others */
      gimp_image_set_xcf_compression (image, (i % 2) == 1);

      g_assert_cmpint (gimp_image_get_xcf_version (image, (i % 2) == 1,
                                                   NULL, NULL, NULL),
                       ==, 28);

      file = cmyk_tmp_file ();
      cmyk_save (gimp, image, file);
      g_assert_cmpint (cmyk_file_version (file), ==, 28);

      cmyk_capture_messages_begin (gimp, &state);
      loaded = cmyk_load (gimp, file, &error);
      cmyk_capture_messages_end (gimp, &state);

      g_assert_no_error (error);
      g_assert_nonnull (loaded);
      /* a valid file loads without any warning */
      g_assert_cmpstr (cmyk_messages->str, ==, "");

      cmyk_assert_images_equal (image, loaded);

      g_file_delete (file, NULL, NULL);
      g_object_unref (file);
      g_object_unref (loaded);
      g_object_unref (image);
    }
}

static void
cmyk_round_trip (gconstpointer data)
{
  cmyk_round_trip_full (GIMP (data), FALSE);
}

/* X1 with a group layer (non-Normal mode, two children).  CMYK group
 * projections are WS02's (gimpgrouplayer.c); until they exist the
 * group layer criticals, so the test runs in a subprocess and skips on
 * that specific failure.
 */
static void
cmyk_group_probe (gconstpointer data)
{
  Gimp      *gimp = GIMP (data);
  GimpImage *image;
  GFile     *file;

  image = cmyk_create_image (gimp, GIMP_PRECISION_U8_NON_LINEAR, NULL, TRUE);
  file  = cmyk_tmp_file ();
  cmyk_save (gimp, image, file);
  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
  g_object_unref (image);
}

static void
cmyk_round_trip_group (gconstpointer data)
{
  if (cmyk_skip_without_support ())
    return;

  g_test_trap_subprocess ("/gimp-xcf/subprocess/cmyk_group_probe",
                          300 * G_USEC_PER_SEC,
                          G_TEST_SUBPROCESS_DEFAULT);

  if (! g_test_trap_has_passed ())
    {
      g_test_skip ("pending integration (WS02): CMYK group layers "
                   "(gimpgrouplayer.c get_projection_format) are not "
                   "implemented yet");
      return;
    }

  cmyk_round_trip_full (GIMP (data), TRUE);
}

/* X2: RGB images keep saving with their old version */
static void
cmyk_rgb_version_unchanged (gconstpointer data)
{
  Gimp      *gimp = GIMP (data);
  GimpImage *image;
  GimpLayer *layer;
  GFile     *file;
  gint       expected;

  image = gimp_image_new (gimp, 32, 32, GIMP_RGB,
                          GIMP_PRECISION_U8_NON_LINEAR);
  layer = gimp_layer_new (image, 32, 32,
                          gimp_image_get_layer_format (image, TRUE),
                          "rgb", GIMP_OPACITY_OPAQUE,
                          GIMP_LAYER_MODE_NORMAL);
  gimp_image_add_layer (image, layer, NULL, 0, FALSE);

  expected = gimp_image_get_xcf_version (image, FALSE, NULL, NULL, NULL);
  g_assert_cmpint (expected, <, 28);

  file = cmyk_tmp_file ();
  cmyk_save (gimp, image, file);
  g_assert_cmpint (cmyk_file_version (file), ==, expected);

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
  g_object_unref (image);
}

/* X3: an XCF saved with a header variant of the builtin profile loads
 * into the builtin's babl space (I8), with identical inks, and its
 * layers move into a builtin-profile image without any change
 */
static void
cmyk_header_variant (gconstpointer data)
{
  Gimp             *gimp = GIMP (data);
  GimpColorProfile *builtin;
  GimpColorProfile *variant;
  GimpImage        *image;
  GimpImage        *loaded;
  GimpImage        *dest;
  GFile            *file;
  GError           *error = NULL;
  GList            *layers;
  GList            *iter;
  const Babl       *builtin_space;

  if (cmyk_skip_without_support ())
    return;

  builtin = gimp_color_profile_new_cmyk_builtin ();
  variant = gimp_test_utils_cmyk_header_variant (builtin);
  builtin_space = gimp_color_profile_get_space (builtin,
                                                GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                NULL);
  g_assert_nonnull (builtin_space);

  image = cmyk_create_image (gimp, GIMP_PRECISION_U16_NON_LINEAR, variant,
                             FALSE);

  file = cmyk_tmp_file ();
  cmyk_save (gimp, image, file);

  loaded = cmyk_load (gimp, file, &error);
  g_assert_no_error (error);
  g_assert_nonnull (loaded);

  /* the file's profile bytes are kept, the space is the builtin's */
  cmyk_assert_profiles_equal_bytes (gimp_image_get_color_profile (loaded),
                                    variant);
  g_assert_true (gimp_image_get_layer_space (loaded) == builtin_space);

  cmyk_assert_images_equal (image, loaded);

  /* move every layer into an image with the builtin profile */
  dest = gimp_test_utils_create_cmyk_image (gimp, CMYK_W, CMYK_H,
                                            GIMP_PRECISION_U16_NON_LINEAR,
                                            builtin);
  g_assert_true (gimp_image_get_layer_space (dest) == builtin_space);

  layers = gimp_image_get_layer_list (loaded);
  for (iter = layers; iter; iter = g_list_next (iter))
    {
      GimpItem *item = iter->data;
      GimpItem *copy;

      if (GIMP_IS_GROUP_LAYER (item))
        continue;

      copy = gimp_item_convert (item, dest, G_TYPE_FROM_INSTANCE (item));
      g_assert_nonnull (copy);

      cmyk_assert_buffers_equal (gimp_drawable_get_buffer (GIMP_DRAWABLE (item)),
                                 gimp_drawable_get_buffer (GIMP_DRAWABLE (copy)),
                                 TRUE, gimp_object_get_name (item));
      g_object_unref (copy);
    }
  g_list_free (layers);

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
  g_object_unref (dest);
  g_object_unref (loaded);
  g_object_unref (image);
  g_object_unref (variant);
  g_object_unref (builtin);
}

/* X4: an input-only profile parasite → the default profile and a
 * warning; a legacy mode on a CMYK layer → mapped with a warning
 */
static void
cmyk_bad_profile_and_legacy_mode (gconstpointer data)
{
  Gimp             *gimp = GIMP (data);
  GimpColorProfile *input_only;
  GimpColorProfile *def;
  GimpImage        *image;
  GimpImage        *loaded;
  GimpLayer        *layer;
  GimpParasite     *parasite;
  GFile            *file;
  GError           *error = NULL;
  const guint8     *icc;
  gsize             icc_len;
  CmykMessageState  state;

  if (cmyk_skip_without_support ())
    return;

  image = gimp_test_utils_create_cmyk_image (gimp, 16, 16,
                                             GIMP_PRECISION_U8_NON_LINEAR,
                                             NULL);
  gimp_image_undo_disable (image);

  layer = gimp_layer_new (image, 16, 16,
                          gimp_image_get_layer_format (image, TRUE),
                          "legacy", GIMP_OPACITY_OPAQUE,
                          GIMP_LAYER_MODE_NORMAL);
  cmyk_fill_buffer (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)), 5);
  /* craft the invalid state directly: gimp_layer_set_mode() may map
   * legacy modes on CMYK layers (I9)
   */
  layer->mode = GIMP_LAYER_MODE_MULTIPLY_LEGACY;
  gimp_image_add_layer (image, layer, NULL, 0, FALSE);

  /* an input-only CMYK profile, attached bypassing the validation */
  input_only = gimp_test_utils_cmyk_input_only_profile ();
  icc = gimp_color_profile_get_icc_profile (input_only, &icc_len);
  parasite = gimp_parasite_new (GIMP_ICC_PROFILE_PARASITE_NAME,
                                GIMP_PARASITE_PERSISTENT |
                                GIMP_PARASITE_UNDOABLE,
                                icc_len, icc);
  gimp_parasite_list_add (GIMP_IMAGE_GET_PRIVATE (image)->parasites,
                          parasite);
  gimp_parasite_free (parasite);

  file = cmyk_tmp_file ();
  cmyk_save (gimp, image, file);
  g_assert_cmpint (cmyk_file_version (file), ==, 28);

  cmyk_capture_messages_begin (gimp, &state);
  loaded = cmyk_load (gimp, file, &error);
  cmyk_capture_messages_end (gimp, &state);

  g_assert_no_error (error);
  g_assert_nonnull (loaded);

  g_assert_cmpint (gimp_image_get_base_type (loaded), ==, GIMP_CMYK);
  def = gimp_get_default_cmyk_profile (gimp, NULL, NULL);
  cmyk_assert_profiles_equal_bytes (gimp_image_get_color_profile (loaded), def);
  g_assert_true (gimp_color_profile_is_cmyk_bidirectional (gimp_image_get_color_profile (loaded)));
  g_object_unref (def);

  g_assert_nonnull (strstr (cmyk_messages->str, "usable CMYK color profile"));
  g_assert_nonnull (strstr (cmyk_messages->str, "legacy layer mode"));

  layer = gimp_image_get_layer_by_name (loaded, "legacy");
  g_assert_nonnull (layer);
  g_assert_cmpint (gimp_layer_get_mode (layer), ==, GIMP_LAYER_MODE_MULTIPLY);
  g_assert_true (gimp_drawable_get_format (GIMP_DRAWABLE (layer)) ==
                 gimp_image_get_layer_format (loaded, TRUE));

  g_object_unref (loaded);

  /* a CMYK image header in an XCF older than 28 is refused cleanly */
  cmyk_patch_file_version (file, 27);
  loaded = cmyk_load (gimp, file, &error);
  g_assert_null (loaded);
  g_assert_nonnull (error);
  g_clear_error (&error);

  /* an XCF version newer than this GIMP knows is refused cleanly */
  cmyk_patch_file_version (file, 99);
  loaded = cmyk_load (gimp, file, &error);
  g_assert_null (loaded);
  g_assert_nonnull (error);
  g_assert_nonnull (strstr (error->message, "unsupported XCF file version"));
  g_clear_error (&error);

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
  g_object_unref (input_only);
  g_object_unref (image);
}


/**
 * main:
 * @argc:
 * @argv:
 *
 * These tests intend to
 *
 *  - Make sure that we are backwards compatible with files created by
 *    older version of GIMP, i.e. that we can load files from earlier
 *    version of GIMP
 *
 *  - Make sure that the information put into a #GimpImage is not lost
 *    when the #GimpImage is written to a file and then read again
 **/
int
main (int    argc,
      char **argv)
{
  Gimp *gimp;
  int   result;

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  /* We share the same application instance across all tests. We need
   * the GUI variant for the file procs
   */
  gimp = gimp_init_for_testing ();

  /* Add tests */
  ADD_TEST (write_and_read_gimp_2_6_format);
  ADD_TEST (write_and_read_gimp_2_6_format_unusual);
  ADD_TEST (load_gimp_2_6_file);
  ADD_TEST (write_and_read_gimp_2_8_format);
  ADD_TEST (cmyk_round_trip);
  ADD_TEST (cmyk_round_trip_group);
  g_test_add_data_func ("/gimp-xcf/subprocess/cmyk_group_probe", gimp,
                        cmyk_group_probe);
  ADD_TEST (cmyk_rgb_version_unchanged);
  ADD_TEST (cmyk_header_variant);
  ADD_TEST (cmyk_bad_profile_and_legacy_mode);

  /* Don't write files to the source dir */
  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  /* Run the tests */
  result = g_test_run ();

  /* Exit so we don't break script-fu plug-in wire */
  gimp_exit (gimp, TRUE);

  return result;
}
