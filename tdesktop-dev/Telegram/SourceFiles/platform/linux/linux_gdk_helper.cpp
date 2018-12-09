/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifndef TDESKTOP_DISABLE_GTK_INTEGRATION
#include "platform/linux/linux_gdk_helper.h"

#include "platform/linux/linux_libs.h"

extern "C" {
#undef signals
#include <gdk/gdkx.h>
#define signals public
} // extern "C"

namespace Platform {
namespace internal {

enum class GtkLoaded {
	GtkNone,
	Gtk2,
	Gtk3,
};

GtkLoaded gdk_helper_loaded = GtkLoaded::GtkNone;

// To be able to compile with gtk-3.0 headers as well
#define GdkDrawable GdkWindow

// Gtk 2
using f_gdk_x11_drawable_get_xdisplay = Display*(*)(GdkDrawable*);
f_gdk_x11_drawable_get_xdisplay gdk_x11_drawable_get_xdisplay = nullptr;

using f_gdk_x11_drawable_get_xid = XID(*)(GdkDrawable*);
f_gdk_x11_drawable_get_xid gdk_x11_drawable_get_xid = nullptr;

// Gtk 3
using f_gdk_x11_window_get_type = GType (*)(void);
f_gdk_x11_window_get_type gdk_x11_window_get_type = nullptr;

// To be able to compile with gtk-2.0 headers as well
template <typename Object>
inline bool gdk_is_x11_window_check(Object *obj) {
	return Libs::g_type_cit_helper(obj, gdk_x11_window_get_type());
}

using f_gdk_window_get_display = GdkDisplay*(*)(GdkWindow *window);
f_gdk_window_get_display gdk_window_get_display = nullptr;

using f_gdk_x11_display_get_xdisplay = Display*(*)(GdkDisplay *display);
f_gdk_x11_display_get_xdisplay gdk_x11_display_get_xdisplay = nullptr;

using f_gdk_x11_window_get_xid = Window(*)(GdkWindow *window);
f_gdk_x11_window_get_xid gdk_x11_window_get_xid = nullptr;

bool GdkHelperLoadGtk2(QLibrary &lib) {
	if (!Libs::load(lib, "gdk_x11_drawable_get_xdisplay", gdk_x11_drawable_get_xdisplay)) return false;
	if (!Libs::load(lib, "gdk_x11_drawable_get_xid", gdk_x11_drawable_get_xid)) return false;
	return true;
}

bool GdkHelperLoadGtk3(QLibrary &lib) {
	if (!Libs::load(lib, "gdk_x11_window_get_type", gdk_x11_window_get_type)) return false;
	if (!Libs::load(lib, "gdk_window_get_display", gdk_window_get_display)) return false;
	if (!Libs::load(lib, "gdk_x11_display_get_xdisplay", gdk_x11_display_get_xdisplay)) return false;
	if (!Libs::load(lib, "gdk_x11_window_get_xid", gdk_x11_window_get_xid)) return false;
	return true;
}

void GdkHelperLoad(QLibrary &lib) {
	gdk_helper_loaded = GtkLoaded::GtkNone;
	if (GdkHelperLoadGtk2(lib)) {
		gdk_helper_loaded = GtkLoaded::Gtk2;
	} else if (GdkHelperLoadGtk3(lib)) {
		gdk_helper_loaded = GtkLoaded::Gtk3;
	}
}

bool GdkHelperLoaded() {
	return gdk_helper_loaded != GtkLoaded::GtkNone;
}

void XSetTransientForHint(GdkWindow *window, quintptr winId) {
	if (gdk_helper_loaded == GtkLoaded::Gtk2) {
		::XSetTransientForHint(gdk_x11_drawable_get_xdisplay(window),
							   gdk_x11_drawable_get_xid(window),
							   winId);
	} else if (gdk_helper_loaded == GtkLoaded::Gtk3) {
		if (gdk_is_x11_window_check(window)) {
			::XSetTransientForHint(gdk_x11_display_get_xdisplay(gdk_window_get_display(window)),
								   gdk_x11_window_get_xid(window),
								   winId);
		}
	}
}

} // namespace internal
} // namespace Platform
#endif // !TDESKTOP_DISABLE_GTK_INTEGRATION
