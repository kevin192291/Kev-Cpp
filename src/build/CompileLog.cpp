#include "build/CompileLog.h"

namespace kevc {

CompileLog::CompileLog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : CompileLogUi(object, builder), end_(buffer_->create_mark("end", buffer_->end(), false)) {
    // The view's own right-click menu copies the selected text; the design adds Copy All below it.
    auto actions = Gio::SimpleActionGroup::create();
    actions->add_action("copy-all", [this] {
        if (const Glib::ustring all = text(); !all.empty()) get_clipboard()->set_text(all);
    });
    insert_action_group("log", actions);
}

void CompileLog::append(const std::string& text, const Glib::ustring& tag) {
    if (tag.empty()) buffer_->insert(buffer_->end(), text);
    else buffer_->insert_with_tag(buffer_->end(), text, tag);
    view_->scroll_to(end_);
}

} // namespace kevc
