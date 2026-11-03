#pragma once

#include "ui/NewProjectDialog.ui.h"

#include "core/ProjectSession.h"

#include <filesystem>
#include <functional>

namespace kevc {

// File → New Project: the project types, a name and a folder. Its layout is NewProjectDialog.ui
// (edit it on Kev-C++'s Design page); this class fills the list of types and creates the project.
// It deletes itself when closed.
class NewProjectDialog : public NewProjectDialogUi {
public:
    // Returns an error to show, or "" once the project is made (the dialog then closes).
    using CreateHandler =
        std::function<Glib::ustring(const std::string& name, const std::filesystem::path& location, ProjectTemplate)>;

    static NewProjectDialog* create(Gtk::Window& parent, const std::filesystem::path& defaultLocation,
                                    CreateHandler onCreate) {
        return NewProjectDialogUi::create<NewProjectDialog>(parent, defaultLocation, std::move(onCreate));
    }
    NewProjectDialog(BaseObjectType* object, const Glib::RefPtr<Gtk::Builder>& builder, Gtk::Window& parent,
                     const std::filesystem::path& defaultLocation, CreateHandler onCreate);

protected:
    void on_templates_row_activated() override;
    void on_browse_clicked() override;
    void on_cancel_clicked() override;
    void on_create_clicked() override;

private:
    void submit();

    CreateHandler onCreate_;
};

} // namespace kevc
