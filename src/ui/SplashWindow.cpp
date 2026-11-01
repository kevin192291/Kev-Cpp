#include "ui/SplashWindow.h"

namespace kevc {

SplashWindow::SplashWindow(Gtk::Window& parent, const std::string& version, unsigned showMs) {
    set_title("Kev-C++ Pro");
    set_transient_for(parent);
    set_modal(true);
    set_decorated(false);
    set_resizable(false);
    // 640 wide: a multiple of 128, which the CF-19's Mesa crocus needs (see Application's GL check).
    set_default_size(640, 384);
    add_css_class("kevc-splash");

    auto* logo = Gtk::make_managed<Gtk::Picture>();
    logo->set_resource("/org/kevcpp/KevCpp/icons/hicolor/512x512/apps/org.kevcpp.KevCpp.png");
    logo->set_size_request(176, 176);
    logo->set_can_shrink(true);
    logo->set_content_fit(Gtk::ContentFit::CONTAIN);
    logo->set_valign(Gtk::Align::CENTER);

    auto* title = Gtk::make_managed<Gtk::Label>("Kev-C++ Pro");
    title->add_css_class("kevc-splash-title");
    title->set_xalign(0.0f);
    auto* subtitle = Gtk::make_managed<Gtk::Label>("C, C++ and web development for Linux");
    subtitle->add_css_class("kevc-splash-subtitle");
    subtitle->set_xalign(0.0f);
    auto* words = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    words->set_valign(Gtk::Align::CENTER);
    words->append(*title);
    words->append(*subtitle);

    auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 28);
    top->set_vexpand(true);
    top->set_valign(Gtk::Align::CENTER);
    top->append(*logo);
    top->append(*words);

    // Bottom: what it's doing on the left, the version on the right (as Dev-C++'s splash has it).
    status_.set_text("Starting…");
    status_.add_css_class("kevc-splash-small");
    status_.set_xalign(0.0f);
    status_.set_hexpand(true);
    auto* versionLabel = Gtk::make_managed<Gtk::Label>("Version " + version);
    versionLabel->add_css_class("kevc-splash-small");
    versionLabel->set_xalign(1.0f);
    versionLabel->set_selectable(false);
    auto* bottom = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
    bottom->append(status_);
    bottom->append(*versionLabel);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    box->set_margin_start(32);
    box->set_margin_end(18);
    box->set_margin_top(28);
    box->set_margin_bottom(12);
    box->append(*top);
    box->append(*bottom);
    set_child(*box);

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
