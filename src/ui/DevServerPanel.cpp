#include "ui/DevServerPanel.h"

namespace kevc {

DevServerPanel::DevServerPanel(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder)
    : DevServerPanelUi(object, builder) {}

void DevServerPanel::setState(bool running, bool ready) {
    task_.setRunning(running);
    open_->set_sensitive(ready);
}

void DevServerPanel::appendLog(const std::string& text) {
    auto buffer = log_->get_buffer();
    buffer->insert(buffer->end(), text);
    auto end = buffer->create_mark(buffer->end());
    log_->scroll_to(end);
    buffer->delete_mark(end);
}

void DevServerPanel::clearLog() {
    log_->get_buffer()->set_text("");
}

void DevServerPanel::on_open_clicked() {
    openClicked_.emit();
}

} // namespace kevc
