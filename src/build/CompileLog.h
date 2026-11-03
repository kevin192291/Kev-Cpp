#pragma once

#include "build/CompileLog.ui.h"

#include <string>

namespace kevc {

// The bottom "Compile Log" tab: the commands a build runs and their output. Its layout and the
// styles its lines are written in (the text tags command, summary, error, warning and success) are
// CompileLog.ui; edit them on Kev-C++'s Design page.
class CompileLog : public CompileLogUi {
public:
    static Glib::RefPtr<CompileLog> create() { return CompileLogUi::create<CompileLog>(); }
    CompileLog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder);

    // Adds text at the end, in one of the design's tags (or none), and scrolls to it.
    void append(const std::string& text, const Glib::ustring& tag = {});
    void clear() { buffer_->set_text(""); }

private:
    Glib::RefPtr<Gtk::TextMark> end_;
};

} // namespace kevc
