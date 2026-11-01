#include "ui/SplashWindow.h"

namespace kevc {

SplashWindow::SplashWindow(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                           const std::string& version, unsigned showMs)
    : SplashWindowUi(object, builder) {
    // 640 wide in the design: a multiple of 128, which the CF-19's Mesa crocus needs (see
    // Application's GL check).
    set_transient_for(parent);
    version_->set_text("Version " + version);

    // Away after a moment, or at once on a click or a key.
    auto click = Gtk::GestureClick::create();
    click->signal_pressed().connect([this](int, double, double) { set_visible(false); });
    add_controller(click);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint, guint, Gdk::ModifierType) {
            set_visible(false);
            return true;
        },
        false);
    add_controller(keys);
    Glib::signal_timeout().connect_once(sigc::track_obj([this] { set_visible(false); }, *this), showMs);
    signal_hide().connect([this] { Glib::signal_idle().connect_once([this] { delete this; }); });
}

} // namespace kevc
