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
 * Owner: WS09.  Skeleton created by WS01a.
 */

#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "gegl/gimp-babl.h"

#include "core/gimp.h"
#include "core/gimpimage.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


static Gimp *gimp = NULL;


static void
no_cmyk_support (void)
{
  g_assert_nonnull (gimp);
  g_assert_false (gimp_babl_has_cmyk_support ());
}

static void
skeleton (void)
{
  g_test_skip ("N: PDB gimp-image-new and XCF load errors: not implemented yet (WS09)");
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

  g_test_add_func ("/cmyk-nosupport/no-cmyk-support", no_cmyk_support);
  g_test_add_func ("/cmyk-nosupport/skeleton",        skeleton);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  return result;
}
