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

/* CMYK image tests I1-I15 (devel-docs/cmyk-mode/TESTING.md).
 * Owner: WS03.  Skeleton created by WS01a.
 */

#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"

#include "widgets/widgets-types.h"

#include "widgets/gimpuimanager.h"

#include "core/gimp.h"
#include "core/gimpimage.h"

#include "tests.h"

#include "gimp-app-test-utils.h"


static Gimp *gimp = NULL;


static void
skeleton (void)
{
  g_assert_nonnull (gimp);

  g_test_skip ("I1-I15: not implemented yet (WS03)");
}


int
main (int    argc,
      char **argv)
{
  gint result;

  g_test_init (&argc, &argv, NULL);

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_SRCDIR",
                                       "app/tests/gimpdir");

  gimp = gimp_init_for_testing ();

  g_test_add_func ("/cmyk-image/skeleton", skeleton);

  result = g_test_run ();

  gimp_test_utils_set_gimp3_directory ("GIMP_TESTING_ABS_TOP_BUILDDIR",
                                       "app/tests/gimpdir-output");

  gimp_exit (gimp, TRUE);

  /* report the skeleton as skipped, not as passed */
  return result ? result : GIMP_EXIT_TEST_SKIPPED;
}
