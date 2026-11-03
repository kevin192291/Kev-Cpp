#include "ui/MessageDialog.h"

#include "ui/Dialogs.h"

namespace kevc {

MessageDialog::MessageDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window* parent,
                             Kind kind, const Glib::ustring& message, const Glib::ustring& detail)
    : MessageDialogUi(object, builder), kind_(kind) {
    if (parent) set_transient_for(*parent);
    // The design shows every part; each kind keeps the ones it uses.
    switch (kind) {
    case Kind::Info: icon_->set_from_icon_name("kevc-about"); break;
    case Kind::Error: icon_->set_from_icon_name("kevc-error"); break;
    case Kind::Question: icon_->set_from_icon_name("kevc-help"); break;
    case Kind::Prompt:
        // A prompt's message is the entry's label, not a heading.
        icon_->set_visible(false);
        message_->remove_css_class("kevc-message");
        break;
    }
    message_->set_text(message);
    message_->set_visible(!message.empty());
    detail_->set_text(detail);
    detailScroll_->set_visible(!detail.empty());
    // The detail scrolls only when it's too long to show whole (release notes, a list of missing
    // tools). A scrolled window that may scroll asks for a wrapped label's height at too narrow a
    // width, which leaves a gap under a short one.
    int minWidth = 0, width = 0, minHeight = 0, height = 0, baseline = 0;
    detail_->measure(Gtk::Orientation::HORIZONTAL, -1, minWidth, width, baseline, baseline);
    detail_->measure(Gtk::Orientation::VERTICAL, width, minHeight, height, baseline, baseline);
    if (height <= detailScroll_->get_max_content_height())
        detailScroll_->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::NEVER);
    entry_->set_visible(kind == Kind::Prompt);
    error_->set_visible(false);

    // Dismissing it is choosing the cancel button, as GTK's own message boxes do.
    signal_close_request().connect(
        [this] {
            choose(cancelIndex_);
            // Once chosen it hides (deleteWhenHidden sets hide-on-close); a handler can refuse.
            return !decided_;
        },
        false);
    closeOnEscape(*this);
    deleteWhenHidden(this);
}

void MessageDialog::setButtons(const std::vector<Glib::ustring>& labels, int cancelIndex, int defaultIndex,
                               ChoiceHandler onChoice) {
    onChoice_ = std::move(onChoice);
    cancelIndex_ = cancelIndex;
    defaultIndex_ = defaultIndex;
    for (Gtk::Button* b : buttonList_) buttons_->remove(*b);
    buttonList_.clear();
    for (size_t i = 0; i < labels.size(); ++i) {
        auto* button = Gtk::make_managed<Gtk::Button>(labels[i]);
        button->signal_clicked().connect([this, i] { choose(static_cast<int>(i)); });
        // The default is the likely answer; a default that is the cancel button ("Cancel" for a
        // deletion) isn't one to highlight.
        if (static_cast<int>(i) == defaultIndex && defaultIndex != cancelIndex)
            button->add_css_class("suggested-action");
        buttons_->append(*button);
        buttonList_.push_back(button);
    }
    if (defaultIndex >= 0 && defaultIndex < static_cast<int>(buttonList_.size()))
        set_default_widget(*buttonList_[defaultIndex]);
}

void MessageDialog::showProblem(const Glib::ustring& text) {
    error_->set_text(text);
    error_->set_visible(!text.empty());
}

void MessageDialog::open() {
    // The focus is set before the window maps: otherwise GTK gives it to the first thing that takes
    // it, the selectable detail, which then shows all its text selected.
    if (kind_ == Kind::Prompt) set_focus(*entry_);
    else if (defaultIndex_ >= 0 && defaultIndex_ < static_cast<int>(buttonList_.size()))
        set_focus(*buttonList_[defaultIndex_]);
    present();
}

void MessageDialog::choose(int index) {
    if (decided_) return;
    if (onChoice_ && !onChoice_(index)) return; // stays open
    decided_ = true;
    set_visible(false);
}

} // namespace kevc
