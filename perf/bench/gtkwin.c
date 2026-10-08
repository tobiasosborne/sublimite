#include <gtk/gtk.h>
#include <time.h>
#include <stdio.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static double T0;
static gboolean on_draw(GtkWidget*w,cairo_t*cr,gpointer d){ printf("gtk_init->first draw %.1f ms\n",(now()-T0)*1e3); gtk_main_quit(); return FALSE; }
int main(int argc,char**argv){ T0=now(); gtk_init(&argc,&argv); double t1=now();
  GtkWidget*w=gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_default_size(GTK_WINDOW(w),1440,900);
  GtkWidget*da=gtk_drawing_area_new(); gtk_container_add(GTK_CONTAINER(w),da); g_signal_connect(da,"draw",G_CALLBACK(on_draw),NULL);
  gtk_widget_show_all(w); printf("gtk_init %.1f ms; ",(t1-T0)*1e3); gtk_main(); return 0; }
