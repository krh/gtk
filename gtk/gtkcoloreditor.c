/* GTK - The GIMP Toolkit
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include "gtkcolorchooserprivate.h"
#include "gtkcoloreditor.h"
#include "gtkcolorplane.h"
#include "gtkcolorscale.h"
#include "gtkcolorswatch.h"
#include "gtkgrid.h"
#include "gtkorientable.h"
#include "gtkaspectframe.h"
#include "gtkdrawingarea.h"
#include "gtkentry.h"
#include "gtkoverlay.h"
#include "gtkhsv.h"
#include "gtkadjustment.h"
#include "gtklabel.h"
#include "gtkspinbutton.h"
#include "gtkalignment.h"
#include "gtkintl.h"

#include <math.h>

struct _GtkColorEditorPrivate
{
  GtkWidget *overlay;
  GtkWidget *grid;
  GtkWidget *swatch;
  GtkWidget *entry;
  GtkWidget *h_slider;
  GtkWidget *h_popup;
  GtkWidget *h_entry;
  GtkWidget *a_slider;
  GtkWidget *a_popup;
  GtkWidget *a_entry;
  GtkWidget *sv_plane;
  GtkWidget *sv_popup;
  GtkWidget *s_entry;
  GtkWidget *v_entry;
  GtkWidget *current_popup;
  GtkWidget *popdown_focus;

  GtkAdjustment *h_adj;
  GtkAdjustment *s_adj;
  GtkAdjustment *v_adj;
  GtkAdjustment *a_adj;

  guint text_changed : 1;
  guint show_alpha   : 1;
};

enum
{
  PROP_ZERO,
  PROP_COLOR,
  PROP_SHOW_ALPHA
};

static void gtk_color_editor_iface_init (GtkColorChooserInterface *iface);

G_DEFINE_TYPE_WITH_CODE (GtkColorEditor, gtk_color_editor, GTK_TYPE_BOX,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_COLOR_CHOOSER,
                                                gtk_color_editor_iface_init))

static guint
scale_round (gdouble value, gdouble scale)
{
  value = floor (value * scale + 0.5);
  value = MAX (value, 0);
  value = MIN (value, scale);
  return (guint)value;
}

static void
update_entry (GtkColorEditor *editor)
{
  gchar *text;
  gdouble h, s, v;
  gdouble r, g, b;

  h = gtk_adjustment_get_value (editor->priv->h_adj);
  s = gtk_adjustment_get_value (editor->priv->s_adj);
  v = gtk_adjustment_get_value (editor->priv->v_adj);
  gtk_hsv_to_rgb (h, s, v, &r, &g, &b);

  text = g_strdup_printf ("#%02X%02X%02X",
                          scale_round (r, 255),
                          scale_round (g, 255),
                          scale_round (b, 255));
  gtk_entry_set_text (GTK_ENTRY (editor->priv->entry), text);
  editor->priv->text_changed = FALSE;
  g_free (text);
}

static void
entry_apply (GtkWidget      *entry,
             GtkColorEditor *editor)
{
  GdkRGBA color;
  gchar *text;

  if (!editor->priv->text_changed)
    return;

  text = gtk_editable_get_chars (GTK_EDITABLE (editor->priv->entry), 0, -1);
  if (gdk_rgba_parse (&color, text))
    {
      color.alpha = gtk_adjustment_get_value (editor->priv->a_adj);
      gtk_color_chooser_set_color (GTK_COLOR_CHOOSER (editor), &color);
    }

  editor->priv->text_changed = FALSE;

  g_free (text);
}

static gboolean
entry_focus_out (GtkWidget      *entry,
                 GdkEventFocus  *event,
                 GtkColorEditor *editor)
{
  entry_apply (entry, editor);
  return FALSE;
}

static void
entry_text_changed (GtkWidget      *entry,
                    GParamSpec     *pspec,
                    GtkColorEditor *editor)
{
  editor->priv->text_changed = TRUE;
}

static void
hsv_changed (GtkColorEditor *editor)
{
  GdkRGBA color;
  gdouble h, s, v;

  h = gtk_adjustment_get_value (editor->priv->h_adj);
  s = gtk_adjustment_get_value (editor->priv->s_adj);
  v = gtk_adjustment_get_value (editor->priv->v_adj);
  gtk_hsv_to_rgb (h, s, v, &color.red, &color.green, &color.blue);
  color.alpha = gtk_adjustment_get_value (editor->priv->a_adj);
  update_entry (editor);
  gtk_color_swatch_set_color (GTK_COLOR_SWATCH (editor->priv->swatch), &color);
  gtk_color_scale_set_color (GTK_COLOR_SCALE (editor->priv->a_slider), &color);
  g_object_notify (G_OBJECT (editor), "color");
}

static void
dismiss_current_popup (GtkColorEditor *editor)
{
  if (editor->priv->current_popup)
    {
      gtk_widget_hide (editor->priv->current_popup);
      editor->priv->current_popup = NULL;
      if (editor->priv->popdown_focus)
        {
          gtk_widget_grab_focus (editor->priv->popdown_focus);
          editor->priv->popdown_focus = NULL;
        }
    }
}

static void
popup_edit (GtkWidget      *widget,
            GtkColorEditor *editor)
{
  GtkWidget *popup = NULL;
  GtkWidget *toplevel;
  GtkWidget *focus;

  if (widget == editor->priv->sv_plane)
    {
      popup = editor->priv->sv_popup;
      focus = editor->priv->s_entry;
    }
  else if (widget == editor->priv->h_slider)
    {
      popup = editor->priv->h_popup;
      focus = editor->priv->h_entry;
    }
  else if (widget == editor->priv->a_slider)
    {
      popup = editor->priv->a_popup;
      focus = editor->priv->a_entry;
    }

  if (popup)
    {
      dismiss_current_popup (editor);
      toplevel = gtk_widget_get_toplevel (GTK_WIDGET (editor));
      editor->priv->popdown_focus = gtk_window_get_focus (GTK_WINDOW (toplevel));
      editor->priv->current_popup = popup;
      gtk_widget_show (popup);
      gtk_widget_grab_focus (focus);
    }
}

static gboolean
popup_key_press (GtkWidget      *popup,
                 GdkEventKey    *event,
                 GtkColorEditor *editor)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      dismiss_current_popup (editor);
      return TRUE;
    }

  return FALSE;
}

static gboolean
get_child_position (GtkOverlay     *overlay,
                    GtkWidget      *widget,
                    GtkAllocation  *allocation,
                    GtkColorEditor *editor)
{
  GtkRequisition req;
  GtkAllocation alloc;
  gint s, e;

  gtk_widget_get_preferred_size (widget, &req, NULL);

  allocation->width = req.width;
  allocation->height = req.height;

  if (widget == editor->priv->sv_popup)
    {
      if (gtk_widget_get_direction (overlay) == GTK_TEXT_DIR_RTL)
        allocation->x = 0;
      else
        allocation->x = gtk_widget_get_allocated_width (GTK_WIDGET (overlay)) - req.width;
      allocation->y = req.height / 3;
    }
  else if (widget == editor->priv->h_popup)
    {
      gtk_widget_get_allocation (editor->priv->h_slider, &alloc);
      gtk_range_get_slider_range (GTK_RANGE (editor->priv->h_slider), &s, &e);

      if (gtk_widget_get_direction (overlay) == GTK_TEXT_DIR_RTL)
        gtk_widget_translate_coordinates (editor->priv->h_slider,
                                          gtk_widget_get_parent (editor->priv->grid),
                                          - req.width, (s + e - req.height) / 2,
                                          &allocation->x, &allocation->y);
      else
        gtk_widget_translate_coordinates (editor->priv->h_slider,
                                          gtk_widget_get_parent (editor->priv->grid),
                                          alloc.width, (s + e - req.height) / 2,
                                          &allocation->x, &allocation->y);
    }
  else if (widget == editor->priv->a_popup)
    {
      gtk_widget_get_allocation (editor->priv->a_slider, &alloc);
      gtk_range_get_slider_range (GTK_RANGE (editor->priv->a_slider), &s, &e);

      gtk_widget_translate_coordinates (editor->priv->a_slider,
                                        gtk_widget_get_parent (editor->priv->grid),
                                        (s + e - req.width) / 2, - req.height,
                                        &allocation->x, &allocation->y);
    }
  else
    return FALSE;

  allocation->x = CLAMP (allocation->x, 0, gtk_widget_get_allocated_width (GTK_WIDGET (overlay)) - req.width);
  allocation->y = CLAMP (allocation->y, 0, gtk_widget_get_allocated_height (GTK_WIDGET (overlay)) - req.height);

  return TRUE;
}

static void
value_changed (GtkAdjustment *a, GtkAdjustment *as)
{
  gdouble scale;

  scale = gtk_adjustment_get_upper (as) / gtk_adjustment_get_upper (a);
  g_signal_handlers_block_by_func (as, value_changed, a);
  gtk_adjustment_set_value (as, gtk_adjustment_get_value (a) * scale);
  g_signal_handlers_unblock_by_func (as, value_changed, a);
}

static GtkAdjustment *
scaled_adjustment (GtkAdjustment *a, gdouble scale)
{
  GtkAdjustment *as;

  as = gtk_adjustment_new (gtk_adjustment_get_value (a) * scale,
                           gtk_adjustment_get_lower (a) * scale,
                           gtk_adjustment_get_upper (a) * scale,
                           gtk_adjustment_get_step_increment (a) * scale,
                           gtk_adjustment_get_page_increment (a) * scale,
                           gtk_adjustment_get_page_size (a) * scale);

  g_signal_connect (a, "value-changed", G_CALLBACK (value_changed), as);
  g_signal_connect (as, "value-changed", G_CALLBACK (value_changed), a);

  return as;
}

static gboolean
popup_draw (GtkWidget *popup, cairo_t *cr, GtkColorEditor *editor)
{
  GtkStyleContext *context;
  gint width, height;

  context = gtk_widget_get_style_context (popup);
  width = gtk_widget_get_allocated_width (popup);
  height = gtk_widget_get_allocated_height (popup);

  gtk_render_background (context, cr, 0, 0, width, height);
  gtk_render_frame (context, cr, 0, 0, width, height);

  return FALSE;
}

static void
gtk_color_editor_init (GtkColorEditor *editor)
{
  GtkWidget *grid;
  GtkWidget *box;
  GtkWidget *slider;
  GtkWidget *alignment;
  GtkAdjustment *adj;
  GdkRGBA transparent = { 0, 0, 0, 0 };

  editor->priv = G_TYPE_INSTANCE_GET_PRIVATE (editor,
                                              GTK_TYPE_COLOR_EDITOR,
                                              GtkColorEditorPrivate);
  editor->priv->show_alpha = TRUE;

  editor->priv->h_adj = gtk_adjustment_new (0, 0, 1, 0.01, 0.1, 0);
  editor->priv->s_adj = gtk_adjustment_new (0, 0, 1, 0.01, 0.1, 0);
  editor->priv->v_adj = gtk_adjustment_new (0, 0, 1, 0.01, 0.1, 0);
  editor->priv->a_adj = gtk_adjustment_new (0, 0, 1, 0.01, 0.1, 0);
  g_object_ref_sink (editor->priv->h_adj);
  g_object_ref_sink (editor->priv->s_adj);
  g_object_ref_sink (editor->priv->v_adj);
  g_object_ref_sink (editor->priv->a_adj);
  g_signal_connect_swapped (editor->priv->h_adj, "value-changed",
                            G_CALLBACK (hsv_changed), editor);
  g_signal_connect_swapped (editor->priv->s_adj, "value-changed",
                            G_CALLBACK (hsv_changed), editor);
  g_signal_connect_swapped (editor->priv->a_adj, "value-changed",
                            G_CALLBACK (hsv_changed), editor);
  g_signal_connect_swapped (editor->priv->v_adj, "value-changed",
                            G_CALLBACK (hsv_changed), editor);

  gtk_widget_push_composite_child ();

  editor->priv->overlay = gtk_overlay_new ();
  gtk_widget_override_background_color (editor->priv->overlay, 0, &transparent);
  editor->priv->grid = grid = gtk_grid_new ();
  gtk_grid_set_row_spacing (GTK_GRID (grid), 12);
  gtk_grid_set_column_spacing (GTK_GRID (grid), 12);

  editor->priv->swatch = gtk_color_swatch_new ();
  gtk_widget_set_sensitive (editor->priv->swatch, FALSE);
  gtk_color_swatch_set_corner_radii (GTK_COLOR_SWATCH (editor->priv->swatch), 2, 2, 2, 2);

  editor->priv->entry = gtk_entry_new ();
  g_signal_connect (editor->priv->entry, "activate",
                    G_CALLBACK (entry_apply), editor);
  g_signal_connect (editor->priv->entry, "notify::text",
                    G_CALLBACK (entry_text_changed), editor);
  g_signal_connect (editor->priv->entry, "focus-out-event",
                    G_CALLBACK (entry_focus_out), editor);

  editor->priv->h_slider = slider = g_object_new (GTK_TYPE_COLOR_SCALE,
                                                  "adjustment", editor->priv->h_adj,
                                                  "draw-value", FALSE,
                                                  "orientation", GTK_ORIENTATION_VERTICAL,
                                                  NULL);
  gtk_color_scale_set_type (GTK_COLOR_SCALE (slider), GTK_COLOR_SCALE_HUE);
  if (gtk_widget_get_direction (slider) == GTK_TEXT_DIR_RTL)
    gtk_style_context_add_class (gtk_widget_get_style_context (slider), GTK_STYLE_CLASS_SCALE_HAS_MARKS_ABOVE);
  else
    gtk_style_context_add_class (gtk_widget_get_style_context (slider), GTK_STYLE_CLASS_SCALE_HAS_MARKS_BELOW);

  editor->priv->a_slider = slider = g_object_new (GTK_TYPE_COLOR_SCALE,
                                                  "adjustment", editor->priv->a_adj,
                                                  "draw-value", FALSE,
                                                  "orientation", GTK_ORIENTATION_HORIZONTAL,
                                                  NULL);
  gtk_color_scale_set_type (GTK_COLOR_SCALE (slider), GTK_COLOR_SCALE_ALPHA);
  gtk_style_context_add_class (gtk_widget_get_style_context (slider), GTK_STYLE_CLASS_SCALE_HAS_MARKS_ABOVE);

  editor->priv->sv_plane = gtk_color_plane_new (editor->priv->h_adj,
                                                editor->priv->s_adj,
                                                editor->priv->v_adj);
  gtk_widget_set_size_request (editor->priv->sv_plane, 300, 300);

  gtk_grid_attach (GTK_GRID (grid), editor->priv->swatch,   1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->entry,    2, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->h_slider, 0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->sv_plane, 1, 1, 2, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->a_slider, 1, 2, 2, 1);
  alignment = gtk_alignment_new (0.5, 0.5, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 30, 30);
  gtk_container_add (GTK_CONTAINER (editor->priv->overlay), alignment);
  gtk_container_add (GTK_CONTAINER (alignment), grid);

  editor->priv->sv_popup = gtk_alignment_new (0.5, 0.5, 0, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (editor->priv->sv_popup),
                               GTK_STYLE_CLASS_TOOLTIP);
  gtk_alignment_set_padding (GTK_ALIGNMENT (editor->priv->sv_popup), 12, 12, 12, 12);
  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  adj = scaled_adjustment (editor->priv->s_adj, 100);
  editor->priv->s_entry = gtk_spin_button_new (adj, 1, 0);
  g_signal_connect (editor->priv->s_entry, "key-press-event",
                    G_CALLBACK (popup_key_press), editor);

  adj = scaled_adjustment (editor->priv->v_adj, 100);
  editor->priv->v_entry = gtk_spin_button_new (adj, 1, 0);
  g_signal_connect (editor->priv->v_entry, "key-press-event",
                    G_CALLBACK (popup_key_press), editor);

  gtk_container_add (GTK_CONTAINER (editor->priv->sv_popup), grid);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("S"), 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->s_entry, 1, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("V"), 0, 1, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->v_entry, 1, 1, 1, 1);
  gtk_widget_show_all (editor->priv->sv_popup);
  gtk_widget_hide (editor->priv->sv_popup);
  gtk_widget_set_no_show_all (editor->priv->sv_popup, TRUE);

  gtk_overlay_add_overlay (GTK_OVERLAY (editor->priv->overlay), editor->priv->sv_popup);

  g_signal_connect (editor->priv->sv_popup, "draw",
                    G_CALLBACK (popup_draw), editor);
  g_signal_connect (editor->priv->sv_plane, "popup-menu",
                    G_CALLBACK (popup_edit), editor);

  editor->priv->h_popup = gtk_alignment_new (0.5, 0.5, 0, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (editor->priv->h_popup),
                               GTK_STYLE_CLASS_TOOLTIP);
  gtk_alignment_set_padding (GTK_ALIGNMENT (editor->priv->h_popup), 12, 12, 12, 12);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  adj = scaled_adjustment (editor->priv->h_adj, 100);
  editor->priv->h_entry = gtk_spin_button_new (adj, 1, 0);
  g_signal_connect (editor->priv->h_entry, "key-press-event",
                    G_CALLBACK (popup_key_press), editor);
  gtk_container_add (GTK_CONTAINER (editor->priv->h_popup), box);
  gtk_container_add (GTK_CONTAINER (box), gtk_label_new ("H"));
  gtk_container_add (GTK_CONTAINER (box), editor->priv->h_entry);
  gtk_widget_show_all (editor->priv->h_popup);
  gtk_widget_hide (editor->priv->h_popup);
  gtk_widget_set_no_show_all (editor->priv->h_popup, TRUE);

  gtk_overlay_add_overlay (GTK_OVERLAY (editor->priv->overlay), editor->priv->h_popup);

  g_signal_connect (editor->priv->h_popup, "draw",
                    G_CALLBACK (popup_draw), editor);
  g_signal_connect (editor->priv->h_slider, "popup-menu",
                    G_CALLBACK (popup_edit), editor);

  editor->priv->a_popup = gtk_alignment_new (0.5, 0.5, 0, 0);
  gtk_style_context_add_class (gtk_widget_get_style_context (editor->priv->a_popup),
                               GTK_STYLE_CLASS_TOOLTIP);
  gtk_alignment_set_padding (GTK_ALIGNMENT (editor->priv->a_popup), 12, 12, 12, 12);
  grid = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (grid), 6);
  adj = scaled_adjustment (editor->priv->a_adj, 100);
  editor->priv->a_entry = gtk_spin_button_new (adj, 1, 0);
  g_signal_connect (editor->priv->a_entry, "key-press-event",
                    G_CALLBACK (popup_key_press), editor);
  gtk_container_add (GTK_CONTAINER (editor->priv->a_popup), grid);
  gtk_grid_attach (GTK_GRID (grid), gtk_label_new ("A"), 0, 0, 1, 1);
  gtk_grid_attach (GTK_GRID (grid), editor->priv->a_entry, 1, 0, 1, 1);
  gtk_widget_show_all (editor->priv->a_popup);
  gtk_widget_hide (editor->priv->a_popup);
  gtk_widget_set_no_show_all (editor->priv->a_popup, TRUE);

  gtk_overlay_add_overlay (GTK_OVERLAY (editor->priv->overlay), editor->priv->a_popup);

  g_signal_connect (editor->priv->a_popup, "draw",
                    G_CALLBACK (popup_draw), editor);
  g_signal_connect (editor->priv->a_slider, "popup-menu",
                    G_CALLBACK (popup_edit), editor);

  g_signal_connect (editor->priv->overlay, "get-child-position",
                    G_CALLBACK (get_child_position), editor);

  gtk_widget_show_all (editor->priv->overlay);

  gtk_container_add (GTK_CONTAINER (editor), editor->priv->overlay);
  gtk_widget_pop_composite_child ();
}

static void
gtk_color_editor_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  GtkColorEditor *ce = GTK_COLOR_EDITOR (object);
  GtkColorChooser *cc = GTK_COLOR_CHOOSER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      {
        GdkRGBA color;
        gtk_color_chooser_get_color (cc, &color);
        g_value_set_boxed (value, &color);
      }
      break;
    case PROP_SHOW_ALPHA:
      g_value_set_boolean (value, gtk_widget_get_visible (ce->priv->a_slider));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_color_editor_set_show_alpha (GtkColorEditor *editor,
                                 gboolean        show_alpha)
{
  if (editor->priv->show_alpha != show_alpha)
    {
      editor->priv->show_alpha = show_alpha;

      gtk_widget_set_visible (editor->priv->a_slider, show_alpha);

      gtk_color_swatch_set_show_alpha (GTK_COLOR_SWATCH (editor->priv->swatch), show_alpha);
    }
}

static void
gtk_color_editor_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  GtkColorEditor *ce = GTK_COLOR_EDITOR (object);
  GtkColorChooser *cc = GTK_COLOR_CHOOSER (object);

  switch (prop_id)
    {
    case PROP_COLOR:
      gtk_color_chooser_set_color (cc, g_value_get_boxed (value));
      break;
    case PROP_SHOW_ALPHA:
      gtk_color_editor_set_show_alpha (ce, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_color_editor_finalize (GObject *object)
{
  GtkColorEditor *editor = GTK_COLOR_EDITOR (object);

  g_clear_object (&editor->priv->h_adj);
  g_clear_object (&editor->priv->s_adj);
  g_clear_object (&editor->priv->v_adj);
  g_clear_object (&editor->priv->a_adj);

  G_OBJECT_CLASS (gtk_color_editor_parent_class)->finalize (object);
}

static void
gtk_color_editor_class_init (GtkColorEditorClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  object_class->finalize = gtk_color_editor_finalize;
  object_class->get_property = gtk_color_editor_get_property;
  object_class->set_property = gtk_color_editor_set_property;

  g_object_class_override_property (object_class, PROP_COLOR, "color");
  g_object_class_override_property (object_class, PROP_SHOW_ALPHA, "show-alpha");

  g_type_class_add_private (class, sizeof (GtkColorEditorPrivate));
}

static void
gtk_color_editor_get_color (GtkColorChooser *chooser,
                            GdkRGBA         *color)
{
  GtkColorEditor *editor = GTK_COLOR_EDITOR (chooser);
  gdouble h, s, v;

  h = gtk_adjustment_get_value (editor->priv->h_adj);
  s = gtk_adjustment_get_value (editor->priv->s_adj);
  v = gtk_adjustment_get_value (editor->priv->v_adj);
  gtk_hsv_to_rgb (h, s, v, &color->red, &color->green, &color->blue);
  color->alpha = gtk_adjustment_get_value (editor->priv->a_adj);
}

static void
gtk_color_editor_set_color (GtkColorChooser *chooser,
                            const GdkRGBA   *color)
{
  GtkColorEditor *editor = GTK_COLOR_EDITOR (chooser);
  gdouble h, s, v;

  gtk_rgb_to_hsv (color->red, color->green, color->blue, &h, &s, &v);

  gtk_adjustment_set_value (editor->priv->h_adj, h);
  gtk_adjustment_set_value (editor->priv->s_adj, s);
  gtk_adjustment_set_value (editor->priv->v_adj, v);
  gtk_adjustment_set_value (editor->priv->a_adj, color->alpha);
  gtk_color_swatch_set_color (GTK_COLOR_SWATCH (editor->priv->swatch), color);
  gtk_color_scale_set_color (GTK_COLOR_SCALE (editor->priv->a_slider), color);

  update_entry (editor);

  gtk_widget_queue_draw (GTK_WIDGET (editor));

  g_object_notify (G_OBJECT (editor), "color");
}

static void
gtk_color_editor_iface_init (GtkColorChooserInterface *iface)
{
  iface->get_color = gtk_color_editor_get_color;
  iface->set_color = gtk_color_editor_set_color;
}

GtkWidget *
gtk_color_editor_new (void)
{
  return (GtkWidget *) g_object_new (GTK_TYPE_COLOR_EDITOR, NULL);
}