#pragma once

#include "ui/MessageDialog.ui.h"

#include <functional>
#include <vector>

namespace kevc {

// Kev-C++'s message boxes: news, errors, questions with a row of buttons, and a line of text to type
// (Dialogs.h: showInfo, showError, askChoice, promptText). Its layout is MessageDialog.ui (edit it on
// Kev-C++'s Design page); this class shows the parts each kind uses and makes the buttons. It
// deletes itself when closed.
class MessageDialog : public MessageDialogUi {
public:
    enum class Kind { Info, Error, Question, Prompt };
    // Gets the chosen button's index; returning false keeps the dialog open (a prompt whose text
    // isn't right yet).
    using ChoiceHandler = std::function<bool(int)>;

    // `parent` may be null: a message before the main window exists.
    static MessageDialog* create(Gtk::Window* parent, Kind kind, const Glib::ustring& message,
                                 const Glib::ustring& detail) {
        return MessageDialogUi::create<MessageDialog>(parent, kind, message, detail);
    }
    MessageDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window* parent, Kind kind,
                  const Glib::ustring& message, const Glib::ustring& detail);

    // The buttons, left to right. Escape and the window's close button choose `cancelIndex` (-1:
    // they choose -1); `defaultIndex` has the focus, so Enter picks it.
    void setButtons(const std::vector<Glib::ustring>& labels, int cancelIndex, int defaultIndex,
                    ChoiceHandler onChoice);

    Gtk::Entry& entry() { return *entry_; }
    // Shows what's wrong with the typed text under the entry.
    void showProblem(const Glib::ustring& text);
    // Shows the dialog with the default button (or a prompt's entry) focused.
    void open();

private:
    void choose(int index);

    std::vector<Gtk::Button*> buttonList_;
    ChoiceHandler onChoice_;
    int cancelIndex_ = -1, defaultIndex_ = -1;
    bool decided_ = false;
    Kind kind_;
};

} // namespace kevc
