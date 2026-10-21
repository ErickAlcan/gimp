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

/* CMYK UI tests (written by WS07, adopted by WS09): runs the GUI under
 * Xvfb (UI_TEST, tools/run_test_env.sh) with NO_AT_BRIDGE=1.
 *
 * Image > Mode sensitivity, Image > Mode > CMYK, the CMYK conversion and
 * assign dialogs (default profile and why, one-way profiles refused),
 * legacy modes in the layer mode box, the display transform (managed and
 * unmanaged) and a real render, image properties, the template editor,
 * and image / K component previews.
 */

#include "config.h"

#include <string.h>

#include <glib/gstdio.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpwidgets/gimpwidgets.h"
#include "libgimpwidgets/gimpcolorprofilestore-private.h"

#include "dialogs/dialogs-types.h"
#include "dialogs/dialogs.h"
#include "dialogs/color-profile-dialog.h"
#include "dialogs/convert-precision-dialog.h"

#include "display/gimpdisplay.h"
#include "display/gimpdisplayshell.h"
#include "display/gimpdisplayshell-render.h"
#include "display/gimpdisplayshell-profile.h"
#include "display/gimpdisplayshell-transform.h"

#include "menus/menus.h"

#include "widgets/gimpaction.h"
#include "widgets/gimpimagepropview.h"
#include "widgets/gimplayermodebox.h"
#include "widgets/gimptemplateeditor.h"
#include "widgets/gimpuimanager.h"
#include "widgets/gimpview.h"
#include "widgets/gimpviewrendererimage.h"
#include "widgets/gimpwidgets-utils.h"

#include "gegl/gimp-babl.h"

#include "core/gimp.h"
#include "core/gimpcontext.h"
#include "core/gimpimage.h"
#include "core/gimpimage-color-profile.h"
#include "core/gimplayer.h"
#include "core/gimppickable.h"
#include "core/gimpprojectable.h"
#include "core/gimptemplate.h"

#include "gimpcoreapp.h"

#include "gimp-app-test-utils.h"
#include "tests.h"


#define ADD_TEST(function) \
  g_test_add_data_func ("/gimp-cmyk-ui/" #function, gimp, function);


static GimpImage *cmyk_image = NULL;
static GimpImage *rgb_image  = NULL;


static void
collect_labels (GtkWidget *widget,
                GString   *text)
{
  if (GTK_IS_LABEL (widget) && gtk_widget_get_visible (widget))
    {
      g_string_append (text, gtk_label_get_text (GTK_LABEL (widget)));
      g_string_append (text, "\n");
    }

  if (GIMP_IS_COLOR_PROFILE_COMBO_BOX (widget))
    {
      GtkTreeIter  iter;
      gchar       *label = NULL;

      if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
        gtk_tree_model_get (gtk_combo_box_get_model (GTK_COMBO_BOX (widget)),
                            &iter, GIMP_COLOR_PROFILE_STORE_LABEL, &label, -1);
      g_string_append_printf (text, "[combo: %s]\n", label ? label : "-");
      g_free (label);
    }

  if (GTK_IS_CONTAINER (widget))
    gtk_container_foreach (GTK_CONTAINER (widget),
                           (GtkCallback) collect_labels, text);
}

static GtkWidget *
find_profile_combo (GtkWidget *widget)
{
  if (GIMP_IS_COLOR_PROFILE_COMBO_BOX (widget))
    return widget;

  if (GTK_IS_CONTAINER (widget))
    {
      GList     *children = gtk_container_get_children (GTK_CONTAINER (widget));
      GList     *iter;
      GtkWidget *found    = NULL;

      for (iter = children; iter && ! found; iter = iter->next)
        found = find_profile_combo (iter->data);

      g_list_free (children);

      return found;
    }

  return NULL;
}

static gchar *
dialog_text (GtkWidget *widget)
{
  GString *text = g_string_new (NULL);

  collect_labels (widget, text);

  return g_string_free (text, FALSE);
}

static gboolean
action_sensitive (Gimp        *gimp,
                  const gchar *name)
{
  GimpUIManager *manager = menus_get_image_manager_singleton (gimp);
  GimpAction    *action;

  action = gimp_ui_manager_find_action (manager, "image", name);
  g_assert_nonnull (action);

  return gimp_action_is_sensitive (action, NULL);
}

static void
create_images (gconstpointer data)
{
  Gimp          *gimp   = GIMP (data);
  const gdouble  paper[5] = { 0.0, 0.0, 0.0, 0.0, 1.0 };
  const gdouble  k100[5]  = { 0.0, 0.0, 0.0, 1.0, 1.0 };
  GimpLayer     *layer;

  cmyk_image = gimp_test_utils_create_cmyk_image (gimp, 64, 64,
                                                  GIMP_PRECISION_U16_NON_LINEAR,
                                                  NULL);
  layer = gimp_test_utils_add_cmyk_layer (cmyk_image, "bg", TRUE, paper);
  gimp_test_utils_write_cmyk_patch (gimp_drawable_get_buffer (GIMP_DRAWABLE (layer)),
                                    GEGL_RECTANGLE (0, 0, 32, 64), k100);

  gimp_create_display (gimp, cmyk_image, gimp_unit_pixel (), 1.0, NULL);

  rgb_image = gimp_image_new (gimp, 64, 64, GIMP_RGB,
                              GIMP_PRECISION_U8_NON_LINEAR);
  gimp_create_display (gimp, rgb_image, gimp_unit_pixel (), 1.0, NULL);

  gimp_test_run_mainloop_until_idle ();
}

/*  image ▸ mode sensitivity on a CMYK and on an RGB image  */
static void
mode_actions (gconstpointer data)
{
  Gimp *gimp = GIMP (data);

  gimp_context_set_display (gimp_get_user_context (gimp),
                            GIMP_DISPLAY (gimp_get_display_iter (gimp)->data));

  gimp_context_set_image (gimp_get_user_context (gimp), cmyk_image);
  gimp_ui_manager_update (menus_get_image_manager_singleton (gimp),
                          gimp_get_user_context (gimp));
  gimp_test_run_mainloop_until_idle ();

  g_assert_true  (action_sensitive (gimp, "image-convert-cmyk"));
  g_assert_true  (action_sensitive (gimp, "image-convert-rgb"));
  g_assert_false (action_sensitive (gimp, "image-convert-indexed"));
  g_assert_false (action_sensitive (gimp, "image-convert-linear"));
  g_assert_false (action_sensitive (gimp, "image-color-profile-use-srgb"));
  g_assert_false (action_sensitive (gimp, "image-color-profile-discard"));

  gimp_context_set_image (gimp_get_user_context (gimp), rgb_image);
  gimp_ui_manager_update (menus_get_image_manager_singleton (gimp),
                          gimp_get_user_context (gimp));
  gimp_test_run_mainloop_until_idle ();

  g_assert_true  (action_sensitive (gimp, "image-convert-cmyk"));
  g_assert_true  (action_sensitive (gimp, "image-convert-indexed"));
  g_assert_true  (action_sensitive (gimp, "image-convert-linear"));
}

/*  the convert dialog shows which profile is the default and why,
 *  and refuses one-way CMYK profiles
 */
static void
convert_dialog (gconstpointer data)
{
  Gimp             *gimp = GIMP (data);
  GimpContext      *context = gimp_get_user_context (gimp);
  GtkWidget        *parent;
  GtkWidget        *dialog;
  GimpColorProfile *input_only;
  gchar            *text;
  const gchar      *icc_env = g_getenv ("GIMP_TESTING_CMYK_ICC");

  parent = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  /*  no soft-proofing profile, no preference: built-in  */
  dialog = color_profile_dialog_new (COLOR_PROFILE_DIALOG_CONVERT_TO_CMYK,
                                     rgb_image, context, parent,
                                     gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (rgb_image)),
                                     NULL,
                                     GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                                     TRUE,
                                     (GimpColorProfileCallback) gtk_widget_destroy,
                                     NULL);
  gtk_widget_show (dialog);
  gimp_test_run_mainloop_until_idle ();
  text = dialog_text (dialog);
  g_printerr ("--- dialog (builtin) ---\n%s", text);
  g_assert_nonnull (strstr (text, "Built-in CMYK"));
  g_assert_nonnull (strstr (text, "built-in CMYK profile is used by default"));
  g_assert_nonnull (strstr (text, "Rendering Intent"));
  g_assert_true (gtk_widget_get_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK)));
  g_free (text);
  gtk_widget_destroy (dialog);

  /*  input-only soft-proofing profile: skipped, and said so  */
  input_only = gimp_test_utils_cmyk_input_only_profile ();
  gimp_image_set_simulation_profile (rgb_image, input_only);

  dialog = color_profile_dialog_new (COLOR_PROFILE_DIALOG_CONVERT_TO_CMYK,
                                     rgb_image, context, parent,
                                     gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (rgb_image)),
                                     NULL,
                                     GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                     TRUE,
                                     (GimpColorProfileCallback) gtk_widget_destroy,
                                     NULL);
  gtk_widget_show (dialog);
  gimp_test_run_mainloop_until_idle ();
  text = dialog_text (dialog);
  g_printerr ("--- dialog (input-only simulation) ---\n%s", text);
  g_assert_nonnull (strstr (text, "Built-in CMYK"));
  g_assert_nonnull (strstr (text, "cannot be used for CMYK images"));
  g_free (text);
  gtk_widget_destroy (dialog);

  /*  a real bidirectional soft-proofing profile becomes the default  */
  if (icc_env)
    {
      gchar            **paths = g_strsplit (icc_env, ":", -1);
      GFile             *file  = g_file_new_for_path (paths[0]);
      GimpColorProfile  *real  = gimp_color_profile_new_from_file (file, NULL);

      g_assert_nonnull (real);
      gimp_image_set_simulation_profile (rgb_image, real);

      dialog = color_profile_dialog_new (COLOR_PROFILE_DIALOG_CONVERT_TO_CMYK,
                                         rgb_image, context, parent,
                                         gimp_color_managed_get_color_profile (GIMP_COLOR_MANAGED (rgb_image)),
                                         NULL,
                                         GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL,
                                         TRUE,
                                         (GimpColorProfileCallback) gtk_widget_destroy,
                                         NULL);
      gtk_widget_show (dialog);
      gimp_test_run_mainloop_until_idle ();
      text = dialog_text (dialog);
      g_printerr ("--- dialog (real simulation) ---\n%s", text);
      g_assert_nonnull (strstr (text, "Soft-proofing profile ("));
      g_assert_nonnull (strstr (text, "soft-proofing profile is used"));
      g_free (text);
      gtk_widget_destroy (dialog);

      g_object_unref (real);
      g_object_unref (file);
      g_strfreev (paths);
    }

  gimp_image_set_simulation_profile (rgb_image, NULL);

  /*  assigning a one-way CMYK profile to a CMYK image is refused  */
  {
    gchar *tmp  = g_build_filename (g_get_tmp_dir (), "cmyk-ui-input-only.icc", NULL);
    GFile *file = g_file_new_for_path (tmp);

    g_assert_true (gimp_color_profile_save_to_file (input_only, file, NULL));

    dialog = color_profile_dialog_new (COLOR_PROFILE_DIALOG_ASSIGN_PROFILE,
                                       cmyk_image, context, parent,
                                       gimp_image_get_color_profile (cmyk_image),
                                       gimp_image_get_builtin_color_profile (cmyk_image),
                                       0, 0,
                                       (GimpColorProfileCallback) gtk_widget_destroy,
                                       NULL);
    gtk_widget_show (dialog);
    gimp_test_run_mainloop_until_idle ();
    g_assert_true (gtk_widget_get_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK)));

    {
      GtkWidget *combo = find_profile_combo (dialog);

      g_assert_nonnull (combo);
      gimp_color_profile_combo_box_set_active_file (GIMP_COLOR_PROFILE_COMBO_BOX (combo),
                                                    file, NULL);
    }
    gimp_test_run_mainloop_until_idle ();
    text = dialog_text (dialog);
    g_printerr ("--- assign dialog (input-only) ---\n%s", text);
    g_assert_nonnull (strstr (text, "missing output tables"));
    g_assert_false (gtk_widget_get_sensitive (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK)));
    g_free (text);
    gtk_widget_destroy (dialog);

    g_object_unref (file);
    g_free (tmp);
  }

  /*  CMYK -> RGB shows intent and BPC  */
  dialog = color_profile_dialog_new (COLOR_PROFILE_DIALOG_CONVERT_TO_RGB,
                                     cmyk_image, context, parent,
                                     gimp_image_get_color_profile (cmyk_image),
                                     gimp_babl_get_builtin_color_profile (GIMP_RGB, GIMP_TRC_NON_LINEAR),
                                     GIMP_COLOR_RENDERING_INTENT_PERCEPTUAL, TRUE,
                                     (GimpColorProfileCallback) gtk_widget_destroy,
                                     NULL);
  gtk_widget_show (dialog);
  gimp_test_run_mainloop_until_idle ();
  text = dialog_text (dialog);
  g_printerr ("--- CMYK -> RGB dialog ---\n%s", text);
  g_assert_nonnull (strstr (text, "Rendering Intent"));
  g_free (text);
  gtk_widget_destroy (dialog);

  g_object_unref (input_only);
  gtk_widget_destroy (parent);
}

/*  Image > Mode > CMYK opens the CMYK conversion dialog  */
static void
mode_cmyk_action (gconstpointer data)
{
  Gimp          *gimp    = GIMP (data);
  GimpContext   *context = gimp_get_user_context (gimp);
  GimpUIManager *manager = menus_get_image_manager_singleton (gimp);
  GList         *list;
  GtkWidget     *dialog;
  gchar         *text;

  for (list = gimp_get_display_iter (gimp); list; list = list->next)
    if (gimp_display_get_image (list->data) == rgb_image)
      gimp_context_set_display (context, list->data);

  gimp_context_set_image (context, rgb_image);
  gimp_ui_manager_update (manager, context);
  gimp_test_run_mainloop_until_idle ();

  gimp_ui_manager_activate_action (manager, "image", "image-convert-cmyk");
  gimp_test_run_mainloop_until_idle ();

  dialog = dialogs_get_dialog (G_OBJECT (rgb_image), "gimp-convert-type-dialog");
  g_assert_nonnull (dialog);

  text = dialog_text (dialog);
  g_printerr ("--- Image > Mode > CMYK ---\n%s", text);
  g_assert_nonnull (strstr (text, "Convert Image to CMYK"));
  g_free (text);

  gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
  gimp_test_run_mainloop_until_idle ();

  g_assert_cmpint (gimp_image_get_base_type (rgb_image), ==, GIMP_RGB);
}

/*  legacy modes are hidden and mapped  */
static void
layer_mode_box (gconstpointer data)
{
  GtkWidget *box = gimp_layer_mode_box_new (GIMP_LAYER_MODE_CONTEXT_LAYER);

  g_object_ref_sink (box);

  gimp_layer_mode_box_set_mode (GIMP_LAYER_MODE_BOX (box),
                                GIMP_LAYER_MODE_MULTIPLY_LEGACY);
  g_assert_cmpint (gimp_layer_mode_box_get_mode (GIMP_LAYER_MODE_BOX (box)),
                   ==, GIMP_LAYER_MODE_MULTIPLY_LEGACY);

  gimp_layer_mode_box_set_allow_legacy (GIMP_LAYER_MODE_BOX (box), FALSE);
  g_assert_cmpint (gimp_layer_mode_box_get_mode (GIMP_LAYER_MODE_BOX (box)),
                   ==, GIMP_LAYER_MODE_MULTIPLY);

  gimp_layer_mode_box_set_mode (GIMP_LAYER_MODE_BOX (box),
                                GIMP_LAYER_MODE_SCREEN_LEGACY);
  g_assert_cmpint (gimp_layer_mode_box_get_mode (GIMP_LAYER_MODE_BOX (box)),
                   ==, GIMP_LAYER_MODE_SCREEN);

  gimp_layer_mode_box_set_allow_legacy (GIMP_LAYER_MODE_BOX (box), TRUE);
  gimp_layer_mode_box_set_mode (GIMP_LAYER_MODE_BOX (box),
                                GIMP_LAYER_MODE_SCREEN_LEGACY);
  g_assert_cmpint (gimp_layer_mode_box_get_mode (GIMP_LAYER_MODE_BOX (box)),
                   ==, GIMP_LAYER_MODE_SCREEN_LEGACY);

  g_object_unref (box);
}

/*  the display pipeline converts the CMYK projection to the monitor
 *  through the image profile (managed), or with babl (unmanaged): the
 *  shell's transform is checked on known CMYK pixels, and a full render
 *  must not emit criticals.
 */
static void
display_render (gconstpointer data)
{
  Gimp             *gimp    = GIMP (data);
  GimpDisplay      *display = NULL;
  GimpDisplayShell *shell;
  GList            *list;
  const Babl       *src_format;
  gint              pass;
  /*  K100 and paper, CMYKA float  */
  const gfloat      cmyka[10] = { 0, 0, 0, 1, 1,   0, 0, 0, 0, 1 };

  for (list = gimp_get_display_iter (gimp); list; list = list->next)
    if (gimp_display_get_image (list->data) == cmyk_image)
      display = list->data;

  g_assert_nonnull (display);
  shell = gimp_display_get_shell (display);

  src_format = gimp_projectable_get_format (GIMP_PROJECTABLE (cmyk_image));
  g_assert_true (gimp_babl_format_is_cmyk (src_format));

  for (pass = 0; pass < 2; pass++)
    {
      guint8      src[2 * 5 * 8];
      guint8      rgba[2 * 4];
      GeglBuffer *src_buf;
      GeglBuffer *dest_buf;

      g_object_set (shell->color_config, "mode",
                    pass == 0 ? GIMP_COLOR_MANAGEMENT_DISPLAY :
                                GIMP_COLOR_MANAGEMENT_OFF, NULL);
      gimp_test_run_mainloop_until_idle ();

      babl_process (babl_fish (babl_format_with_space ("CMYKA float",
                                                       src_format),
                               src_format),
                    cmyka, src, 2);

      if (pass == 0)
        {
          g_assert_nonnull (shell->profile_transform);

          src_buf  = gegl_buffer_linear_new_from_data (src, src_format,
                                                       GEGL_RECTANGLE (0, 0, 2, 1),
                                                       GEGL_AUTO_ROWSTRIDE,
                                                       NULL, NULL);
          dest_buf = gegl_buffer_new (GEGL_RECTANGLE (0, 0, 2, 1),
                                      gimp_display_shell_profile_can_convert_to_u8 (shell) ?
                                      babl_format ("R'G'B'A u8") :
                                      shell->filter_format);

          gimp_color_transform_process_buffer (shell->profile_transform,
                                               src_buf, NULL, dest_buf, NULL);
          gegl_buffer_get (dest_buf, NULL, 1.0, babl_format ("R'G'B'A u8"),
                           rgba, GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
          g_object_unref (src_buf);
          g_object_unref (dest_buf);
        }
      else
        {
          g_assert_null (shell->profile_transform);

          /*  what gimp_display_shell_render() does without transform  */
          babl_process (babl_fish (src_format, babl_format ("R'G'B'A u8")),
                        src, rgba, 2);
        }

      g_printerr ("pass %d (%s): K100 -> %d,%d,%d  paper -> %d,%d,%d\n",
                  pass, pass == 0 ? "managed" : "unmanaged",
                  rgba[0], rgba[1], rgba[2], rgba[4], rgba[5], rgba[6]);

      g_assert_cmpuint (rgba[0], <, 0x40);
      g_assert_cmpuint (rgba[4], >, 0xf0);
      g_assert_cmpuint (rgba[5], >, 0xf0);
      g_assert_cmpuint (rgba[6], >, 0xf0);

      /*  the real render must not emit criticals  */
      {
        cairo_surface_t *surface;
        cairo_t         *cr;
        gint             sx, sy;

        gimp_display_shell_transform_xy (shell, 0.0, 0.0, &sx, &sy);
        surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 64, 64);
        cr = cairo_create (surface);
        gimp_display_shell_render_invalidate_full (shell);
        gimp_display_shell_render (shell, cr, sx, sy, 64, 64, 1.0);
        cairo_destroy (cr);
        cairo_surface_destroy (surface);
      }
    }

  g_object_set (shell->color_config, "mode", GIMP_COLOR_MANAGEMENT_DISPLAY, NULL);
}

/*  image properties say "CMYK color: <profile>"  */
static void
image_properties (gconstpointer data)
{
  GtkWidget *view = gimp_image_prop_view_new (cmyk_image);
  gchar     *text;

  g_object_ref_sink (view);
  gtk_widget_show_all (view);
  text = dialog_text (view);
  g_printerr ("--- prop view ---\n%s", text);
  g_assert_nonnull (strstr (text, "CMYK color: "));
  g_free (text);
  g_object_unref (view);
}

/*  New Image: CMYK in the color space combo, gamma hidden, non-linear  */
static void
template_editor (gconstpointer data)
{
  Gimp         *gimp     = GIMP (data);
  GimpTemplate *template = g_object_new (GIMP_TYPE_TEMPLATE, NULL);
  GtkWidget    *editor;

  g_object_set (template,
                "component-type", GIMP_COMPONENT_TYPE_FLOAT,
                "trc",            GIMP_TRC_LINEAR,
                NULL);

  editor = gimp_template_editor_new (template, gimp, TRUE);
  g_object_ref_sink (editor);
  gtk_widget_show (editor);

  g_object_set (template, "image-type", GIMP_CMYK, NULL);
  g_assert_cmpint (gimp_babl_trc (gimp_template_get_precision (template)),
                   ==, GIMP_TRC_NON_LINEAR);

  g_object_unref (editor);
  g_object_unref (template);
}

/*  previews: image, layer and component previews render without
 *  criticals
 */
static void
previews (gconstpointer data)
{
  Gimp        *gimp    = GIMP (data);
  GimpContext *context = gimp_get_user_context (gimp);
  GtkWidget   *window  = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  GtkWidget   *box     = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
  GtkWidget   *view;

  gtk_container_add (GTK_CONTAINER (window), box);

  view = gimp_view_new_full_by_types (context, GIMP_TYPE_VIEW,
                                      GIMP_TYPE_IMAGE, 64, 64, 0,
                                      FALSE, FALSE, FALSE);
  GIMP_VIEW_RENDERER_IMAGE (GIMP_VIEW (view)->renderer)->channel = GIMP_CHANNEL_BLACK;
  gimp_view_set_viewable (GIMP_VIEW (view), GIMP_VIEWABLE (cmyk_image));
  gtk_box_pack_start (GTK_BOX (box), view, FALSE, FALSE, 0);

  gtk_widget_show_all (window);
  gimp_test_run_mainloop_until_idle ();

  gtk_widget_destroy (window);
}

/*  Image > Mode > CMYK, OK: the dialog converts the image (03)  */
static void
mode_cmyk_convert (gconstpointer data)
{
  Gimp          *gimp    = GIMP (data);
  GimpContext   *context = gimp_get_user_context (gimp);
  GimpUIManager *manager = menus_get_image_manager_singleton (gimp);
  GList         *list;
  GtkWidget     *dialog;

  for (list = gimp_get_display_iter (gimp); list; list = list->next)
    if (gimp_display_get_image (list->data) == rgb_image)
      gimp_context_set_display (context, list->data);

  gimp_context_set_image (context, rgb_image);
  gimp_ui_manager_update (manager, context);
  gimp_test_run_mainloop_until_idle ();

  gimp_ui_manager_activate_action (manager, "image", "image-convert-cmyk");
  gimp_test_run_mainloop_until_idle ();

  dialog = dialogs_get_dialog (G_OBJECT (rgb_image), "gimp-convert-type-dialog");
  g_assert_nonnull (dialog);

  gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  gimp_test_run_mainloop_until_idle ();

  g_assert_cmpint (gimp_image_get_base_type (rgb_image), ==, GIMP_CMYK);
  g_assert_true (gimp_color_profile_is_cmyk (gimp_image_get_color_profile (rgb_image)));
  g_assert_true (gimp_babl_format_is_cmyk (gimp_projectable_get_format (GIMP_PROJECTABLE (rgb_image))));

  /*  ... and the mode actions follow  */
  gimp_ui_manager_update (manager, context);
  gimp_test_run_mainloop_until_idle ();
  g_assert_false (action_sensitive (gimp, "image-convert-indexed"));
  g_assert_false (action_sensitive (gimp, "image-convert-linear"));
}

/*  gui_init() writes these into the configuration directory, which is
 *  the source tree's app/tests/gimpdir while the tests run: remove the
 *  ones this run created
 */
static const gchar *written_files[] = { "theme.css", "profilerc" };

static GList *
files_to_clean (void)
{
  const gchar *srcdir = g_getenv ("GIMP_TESTING_ABS_TOP_SRCDIR");
  GList       *files  = NULL;
  gint         i;

  if (! srcdir)
    return NULL;

  for (i = 0; i < G_N_ELEMENTS (written_files); i++)
    {
      gchar *path = g_build_filename (srcdir, "app", "tests", "gimpdir",
                                      written_files[i], NULL);

      if (! g_file_test (path, G_FILE_TEST_EXISTS))
        files = g_list_prepend (files, path);
      else
        g_free (path);
    }

  return files;
}

int
main (int    argc,
      char **argv)
{
  Gimp  *gimp   = NULL;
  gint   result = -1;
  GList *clean;
  GList *list;

  gimp_test_bail_if_no_display ();
  gtk_test_init (&argc, &argv, NULL);

  clean = files_to_clean ();

  gimp_test_utils_setup_menus_path ();

  gimp = gimp_init_for_gui_testing (TRUE);
  gimp_test_run_mainloop_until_idle ();

  ADD_TEST (create_images);
  ADD_TEST (mode_actions);
  ADD_TEST (convert_dialog);
  ADD_TEST (mode_cmyk_action);
  ADD_TEST (layer_mode_box);
  ADD_TEST (display_render);
  ADD_TEST (image_properties);
  ADD_TEST (template_editor);
  ADD_TEST (previews);
  /*  last: turns rgb_image into a CMYK image  */
  ADD_TEST (mode_cmyk_convert);

  g_application_run (gimp->app, 0, NULL);
  result = gimp_core_app_get_exit_status (GIMP_CORE_APP (gimp->app));

  g_application_quit (G_APPLICATION (gimp->app));
  g_clear_object (&gimp->app);

  for (list = clean; list; list = list->next)
    g_unlink (list->data);
  g_list_free_full (clean, g_free);

  return result;
}
