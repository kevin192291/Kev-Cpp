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

TaskButton::TaskButton(const char* icon, Glib::ustring tooltip, const char* runningIcon, Glib::ustring runningTooltip,
                       int iconSize)
    : icon_(icon), runningIcon_(runningIcon), tooltip_(std::move(tooltip)), runningTooltip_(std::move(runningTooltip)) {
    image_.set_pixel_size(iconSize);
    box_.append(image_);
    text_.set_visible(false);
    box_.append(text_);
    set_child(box_);
    set_has_frame(false);
    // Leave the keyboard focus in the editor, as toolbar buttons do.
    set_focus_on_click(false);
    signal_clicked().connect([this] {
        if (running_) stop_.emit();
        else start_.emit();
    });
    update();
}

void TaskButton::setLabels(Glib::ustring label, Glib::ustring runningLabel) {
    label_ = std::move(label);
    runningLabel_ = std::move(runningLabel);
    text_.set_visible(true);
    set_has_frame(true);
    update();
}

void TaskButton::setRunning(bool running) {
    if (running == running_) return;
    running_ = running;
    update();
}

void TaskButton::update() {
    image_.set_from_icon_name(running_ ? runningIcon_ : icon_);
    set_tooltip_text(running_ ? runningTooltip_ : tooltip_);
    text_.set_text(running_ ? runningLabel_ : label_);
}

} // namespace kevc
