#include "ui/IconWidgets.h"

namespace kevc {

Gtk::Widget* makeIconLabel(const char* icon, const Glib::ustring& text) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
    auto* image = Gtk::make_managed<Gtk::Image>();
    image->set_from_icon_name(icon);
    box->append(*image);
    box->append(*Gtk::make_managed<Gtk::Label>(text));
    return box;
}

TaskButton::TaskButton(Gtk::Button& button, const char* runningIcon, Glib::ustring runningTooltip,
                       Glib::ustring runningLabel)
    : button_(button), runningIcon_(runningIcon), tooltip_(button.get_tooltip_text()),
      runningTooltip_(std::move(runningTooltip)), designTooltip_(tooltip_), designRunningTooltip_(runningTooltip_),
      runningLabel_(std::move(runningLabel)) {
    // The icon and label the design gave it: its icon-name, or the button's child, or in a box there.
    Gtk::Widget* child = button.get_child();
    if (!button.get_icon_name().empty()) {
        iconName_ = true;
        icon_ = button.get_icon_name();
    } else if (auto* box = dynamic_cast<Gtk::Box*>(child)) {
        for (Gtk::Widget* c = box->get_first_child(); c; c = c->get_next_sibling()) {
            if (auto* image = dynamic_cast<Gtk::Image*>(c); image && !image_) image_ = image;
            if (auto* label = dynamic_cast<Gtk::Label*>(c); label && !text_) text_ = label;
        }
    } else {
        image_ = dynamic_cast<Gtk::Image*>(child);
        text_ = dynamic_cast<Gtk::Label*>(child);
    }
    if (image_ && !iconName_) icon_ = image_->get_icon_name();
    if (text_) label_ = text_->get_label();
    button.signal_clicked().connect(sigc::track_obj(
        [this] {
            if (running_) stop_.emit();
            else start_.emit();
        },
        *this));
}

void TaskButton::setRunning(bool running) {
    if (running == running_) return;
    running_ = running;
    update();
}

void TaskButton::setTooltips(Glib::ustring tooltip, Glib::ustring runningTooltip) {
    tooltip_ = std::move(tooltip);
    runningTooltip_ = std::move(runningTooltip);
    update();
}

void TaskButton::update() {
    if (iconName_) button_.set_icon_name(running_ ? runningIcon_ : icon_);
    else if (image_) image_->set_from_icon_name(running_ ? runningIcon_ : icon_);
    if (text_ && !runningLabel_.empty()) text_->set_label(running_ ? runningLabel_ : label_);
    button_.set_tooltip_text(running_ ? runningTooltip_ : tooltip_);
}

} // namespace kevc
