/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 2026 The GIMP authors
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

/* CMYK no-support tests N (devel-docs/cmyk-mode/TESTING.md): GIMP
 * running with GIMP_TESTING_NO_CMYK, i.e. as if babl had no lcms.
 * Separate executable, because the support probe is cached per process.
 * Owner: WS09.
 *
 *   N1  gimp_babl_has_cmyk_support() is FALSE
 *   N2  PDB gimp-image-new / gimp-image-new-with-precision with GIMP_CMYK
 *       return a calling error with a message (RGB still works)
 *   N3  PDB gimp-image-convert-cmyk on an RGB image fails, the image stays
 *       RGB
 *   N4  XCF load of a CMYK file (v28 header, base type 3) fails with
 *       G_FILE_ERROR_NOSYS, without the "This XCF file is corrupt"
 *       prefix and without an image; the same file with base type RGB
 *       loads (so only CMYK is refused)
 *
 * Criticals and warnings are fatal (g_test_init()), so "no criticals" is
 * implied by every test passing.
 */

#include "config.h"

#include <string.h>
#include <unistd.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "gegl/gimp-babl.h"

#include "core/gimp.h"
#include "core/gimpcontainer.h"
#include "core/gimpcontext.h"
#include "core/gimpimage.h"
#include "core/gimpparamspecs.h"

#include "pdb/gimppdb.h"

#include "plug-in/gimppluginmanager-file.h"

#include "file/file-open.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


static Gimp *gimp = NULL;


static void
no_cmyk_support (void)
{
  g_assert_nonnull (gimp);
  g_assert_false (gimp_babl_has_cmyk_support ());
}

static GimpPDBStatusType
run_image_new (const gchar        *name,
               GimpImageBaseType   type,
               GimpImage         **image,
               GError            **error)
{
  GimpValueArray    *return_vals;
  GimpPDBStatusType  status;

  if (! strcmp (name, "gimp-image-new"))
    return_vals = gimp_pdb_execute_procedure_by_name (gimp->pdb,
                                                      gimp_get_user_context (gimp),
                                                      NULL, error, name,
                                                      G_TYPE_INT,                16,
                                                      G_TYPE_INT,                16,
                                                      GIMP_TYPE_IMAGE_BASE_TYPE, type,
                                                      G_TYPE_NONE);
  else
    return_vals = gimp_pdb_execute_procedure_by_name (gimp->pdb,
                                                      gimp_get_user_context (gimp),
                                                      NULL, error, name,
                                                      G_TYPE_INT,                16,
                                                      G_TYPE_INT,                16,
                                                      GIMP_TYPE_IMAGE_BASE_TYPE, type,
                                                      GIMP_TYPE_PRECISION,
                                                      GIMP_PRECISION_U16_NON_LINEAR,
                                                      G_TYPE_NONE);

  status = g_value_get_enum (gimp_value_array_index (return_vals, 0));

  *image = NULL;
  if (status == GIMP_PDB_SUCCESS)
    *image = g_value_get_object (gimp_value_array_index (return_vals, 1));

  gimp_value_array_unref (return_vals);

  return status;
}

static void
pdb_image_new (void)
{
  const gchar *procs[] = { "gimp-image-new", "gimp-image-new-with-precision" };
  gint         n_images = gimp_container_get_n_children (gimp->images);
  gint         i;

  for (i = 0; i < G_N_ELEMENTS (procs); i++)
    {
      GimpImage *image = NULL;
      GError    *error = NULL;

      /*  invalid argument (gimp_pdb_base_type_is_valid()): a calling
       *  error, not a CRITICAL from gimp_image_new()
       */
      g_assert_cmpint (run_image_new (procs[i], GIMP_CMYK, &image, &error),
                       ==, GIMP_PDB_CALLING_ERROR);
      g_assert_null (image);
      g_assert_nonnull (error);
      g_test_message ("%s (CMYK): %s", procs[i], error->message);
      g_assert_nonnull (strstr (error->message, "CMYK"));
      g_clear_error (&error);

      g_assert_cmpint (gimp_container_get_n_children (gimp->images), ==, n_images);

      /*  RGB still works  */
      g_assert_cmpint (run_image_new (procs[i], GIMP_RGB, &image, &error),
                       ==, GIMP_PDB_SUCCESS);
      g_assert_no_error (error);
      g_assert_nonnull (image);
      g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
      g_object_unref (image);
    }
}

static void
pdb_convert_cmyk (void)
{
  GimpImage         *image;
  GimpValueArray    *return_vals;
  GimpPDBStatusType  status;
  GError            *error = NULL;

  image = gimp_image_new (gimp, 16, 16, GIMP_RGB, GIMP_PRECISION_U8_NON_LINEAR);

  return_vals = gimp_pdb_execute_procedure_by_name (gimp->pdb,
                                                    gimp_get_user_context (gimp),
                                                    NULL, &error,
                                                    "gimp-image-convert-cmyk",
                                                    GIMP_TYPE_IMAGE, image,
                                                    G_TYPE_BYTES,    NULL,
                                                    GIMP_TYPE_COLOR_RENDERING_INTENT,
                                                    GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                                    G_TYPE_BOOLEAN,  TRUE,
                                                    G_TYPE_NONE);
  status = g_value_get_enum (gimp_value_array_index (return_vals, 0));
  gimp_value_array_unref (return_vals);

  g_assert_cmpint (status, !=, GIMP_PDB_SUCCESS);
  g_assert_nonnull (error);
  g_test_message ("gimp-image-convert-cmyk: status %d: %s", status, error->message);
  g_assert_nonnull (strstr (error->message, "CMYK"));
  g_clear_error (&error);

  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);

  g_object_unref (image);
}

/*  A minimal XCF v28: header, no properties, no layers, no channels.  */
static GFile *
write_minimal_xcf (GimpImageBaseType base_type)
{
  GByteArray *data = g_byte_array_new ();
  GFile      *file;
  gchar      *filename;
  GError     *error = NULL;
  gint        fd;
  /*  width, height, base type, precision (U8 non-linear), PROP_END  */
  guint32     words[] = { 16, 16, base_type, GIMP_PRECISION_U8_NON_LINEAR, 0, 0 };
  /*  layer and channel offset lists: one 64-bit 0 each (v11+)  */
  const guint8 zeros[16] = { 0, };
  gint        i;

  g_byte_array_append (data, (const guint8 *) "gimp xcf v028", 14);

  for (i = 0; i < G_N_ELEMENTS (words); i++)
    {
      guint32 be = GUINT32_TO_BE (words[i]);

      g_byte_array_append (data, (const guint8 *) &be, 4);
    }

  g_byte_array_append (data, zeros, sizeof (zeros));

  fd = g_file_open_tmp ("gimp-test-nosupport-XXXXXX.xcf", &filename, &error);
  g_assert_no_error (error);
  close (fd);

  g_file_set_contents (filename, (const gchar *) data->data, data->len, &error);
  g_assert_no_error (error);

  file = g_file_new_for_path (filename);

  g_free (filename);
  g_byte_array_unref (data);

  return file;
}

static GimpImage *
load_xcf (GFile              *file,
          GimpPDBStatusType  *status,
          GError            **error)
{
  GimpPlugInProcedure *proc;

  proc = gimp_plug_in_manager_file_procedure_find (gimp->plug_in_manager,
                                                   GIMP_FILE_PROCEDURE_GROUP_OPEN,
                                                   file, NULL);
  g_assert_nonnull (proc);

  return file_open_image (gimp, gimp_get_user_context (gimp), NULL, file,
                          0, 0, TRUE, FALSE, proc,
                          GIMP_RUN_NONINTERACTIVE, NULL, status, NULL,
                          error);
}

static void
xcf_load_cmyk (void)
{
  GimpImage         *image;
  GFile             *file;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GError            *error  = NULL;
  gint               n_images = gimp_container_get_n_children (gimp->images);

  /*  control: the same minimal file as RGB loads  */
  file  = write_minimal_xcf (GIMP_RGB);
  image = load_xcf (file, &status, &error);
  g_assert_no_error (error);
  g_assert_cmpint (status, ==, GIMP_PDB_SUCCESS);
  g_assert_nonnull (image);
  g_assert_cmpint (gimp_image_get_base_type (image), ==, GIMP_RGB);
  g_object_unref (image);
  g_file_delete (file, NULL, NULL);
  g_object_unref (file);

  n_images = gimp_container_get_n_children (gimp->images);

  /*  CMYK: refused as unsupported, not as corrupt  */
  file  = write_minimal_xcf (GIMP_CMYK);
  image = load_xcf (file, &status, &error);

  g_assert_null (image);
  g_assert_cmpint (status, !=, GIMP_PDB_SUCCESS);
  g_assert_nonnull (error);
  g_test_message ("XCF load (CMYK): %s", error->message);
  g_assert_true (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOSYS));
  g_assert_null (strstr (error->message, "corrupt"));
  g_assert_nonnull (strstr (error->message, "CMYK"));
  g_clear_error (&error);

  g_assert_cmpint (gimp_container_get_n_children (gimp->images), ==, n_images);

  g_file_delete (file, NULL, NULL);
  g_object_unref (file);
}


int
main (int    argc,
      char **argv)
{
  gint result;

  /* before anything can call gimp_babl_has_cmyk_support() */
  g_setenv ("GIMP_TESTING_NO_CMYK", "1", TRUE);

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  gimp = gimp_init_for_testing ();

  g_test_add_func ("/cmyk-nosupport/N1/no-cmyk-support",  no_cmyk_support);
  g_test_add_func ("/cmyk-nosupport/N2/pdb-image-new",    pdb_image_new);
  g_test_add_func ("/cmyk-nosupport/N3/pdb-convert-cmyk", pdb_convert_cmyk);
  g_test_add_func ("/cmyk-nosupport/N4/xcf-load",         xcf_load_cmyk);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}
