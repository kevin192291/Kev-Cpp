---
order: 11
section: widgets
description: A function that makes a GtkDrawingArea drawn with cairo; a click moves the dot
packages: gtk4
includes: <gtk/gtk.h>
---
// A widget drawn with cairo; clicking moves the dot. Make it in activate():
//   gtk_window_set_child(GTK_WINDOW(window), create_drawing_area());
typedef struct
{
    double x;
    double y;
} DotPosition;

// Called whenever GTK needs the picture; width and height are the widget's current size.
static void draw_area(GtkDrawingArea* area, cairo_t* cr, int width, int height, gpointer user_data)
{
    (void)area;
    const DotPosition* dot = user_data;

    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);

    // A frame 10 px inside the edges; 0.5 puts 1 px lines on whole pixels so they're sharp.
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 10.5, 10.5, width - 21.0, height - 21.0);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.35, 0.7, 1.0);
    cairo_arc(cr, dot->x, dot->y, 12.0, 0.0, 2.0 * G_PI);
    cairo_fill(cr);
    {{cursor}}
}

static void on_area_pressed(GtkGestureClick* gesture, int press_count, double x, double y, gpointer user_data)
{
    (void)press_count;
    DotPosition* dot = user_data;
    dot->x = x;
    dot->y = y;
    gtk_widget_queue_draw(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
}

static GtkWidget* create_drawing_area(void)
{
    GtkWidget* area = gtk_drawing_area_new();
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(area), 400);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(area), 300);
    gtk_widget_set_hexpand(area, TRUE);
    gtk_widget_set_vexpand(area, TRUE);

    DotPosition* dot = g_new(DotPosition, 1);
    dot->x = 200.0;
    dot->y = 150.0;
    // g_free frees the position when the area is destroyed.
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_area, dot, g_free);

    GtkGesture* click = gtk_gesture_click_new();
    g_signal_connect(click, "pressed", G_CALLBACK(on_area_pressed), dot);
    gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(click));
    return area;
}
