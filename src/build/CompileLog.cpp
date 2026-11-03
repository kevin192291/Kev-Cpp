#include "build/CompileLog.h"

namespace kevc {

CompileLog::CompileLog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : CompileLogUi(object, builder), end_(buffer_->create_mark("end", buffer_->end(), false)) {}

void CompileLog::append(const std::string& text, const Glib::ustring& tag) {
    if (tag.empty()) buffer_->insert(buffer_->end(), text);
    else buffer_->insert_with_tag(buffer_->end(), text, tag);
    view_->scroll_to(end_);
}

} // namespace kevc
