// The GTK designer's code: writing gtkmm or C code for the design, Blueprint, and Go to handler.
// The rest of DesignerPage is in DesignerPage.cpp and DesignerEditing.cpp.
#include "designer/DesignerPage.h"

#include "designer/DesignerTypes.h"

#include <fstream>
#include <sstream>

namespace kevc {

namespace fs = std::filesystem;
using ui::Node;
using ui::UiDocument;

namespace {

std::optional<std::string> readText(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Writes only when the text differs, so a save that changes nothing in the code doesn't make the
// build recompile it.
bool writeIfChanged(const fs::path& file, const std::string& text) {
    if (readText(file) == text) return false;
    Glib::file_set_contents(file.string(), text);
    return true;
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) out += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
    return out;
}

} // namespace

std::optional<std::string> DesignerPage::codeLanguage() const {
    const fs::path dir = path_.parent_path();
    const std::string stem = path_.stem().string();
    std::error_code ec;
    if (fs::exists(dir / (stem + ".ui.cpp"), ec)) return "gtkmm";
    if (fs::exists(dir / (stem + "-ui.c"), ec)) return "c";
    return std::nullopt;
}

void DesignerPage::writeCode(const UiDocument& doc, const std::string& language, bool quiet) {
    const bool cpp = language == "gtkmm";
    const std::string uiFile = path_.filename().string();
    const ui::Code code = cpp ? ui::generateGtkmm(doc, uiFile, designer::gtkTypes()) : ui::generateC(doc, uiFile, designer::gtkTypes());
    const fs::path dir = path_.parent_path();
    std::vector<std::string> written, created;
    try {
        for (const auto& f : code.generated)
            if (writeIfChanged(dir / f.name, f.text)) written.push_back(f.name);
        std::error_code ec;
        for (const auto& f : code.user)
            if (!fs::exists(dir / f.name, ec)) {
                Glib::file_set_contents((dir / f.name).string(), f.text);
                created.push_back(f.name);
            }
    } catch (const Glib::Error& e) {
        showMessage(Glib::ustring("Couldn't write the code: ") + e.what(), true);
        return;
    }
    std::vector<fs::path> files;
    for (const auto& f : code.generated) files.push_back(dir / f.name);
    for (const auto& f : code.user) files.push_back(dir / f.name);
    codeWritten_.emit(cpp ? "gtkmm-4.0" : "gtk4", files);
    std::string message;
    if (!written.empty()) message = "Wrote " + join(written);
    if (!created.empty()) message += (message.empty() ? "Created " : "; created ") + join(created) + " for your code";
    if (message.empty() && !quiet) message = "The code is up to date";
    for (const auto& w : code.warnings) message += (message.empty() ? "" : ". ") + w;
    if (!message.empty()) showMessage(message, !code.warnings.empty());
}

void DesignerPage::exportBlueprint() {
    const fs::path file = path_.parent_path() / (path_.stem().string() + ".blp");
    try {
        Glib::file_set_contents(file.string(), ui::generateBlueprint(doc_, designer::gtkTypes()));
        showMessage("Wrote " + file.filename().string() + " (Blueprint). The designer keeps editing the .ui; the "
                    ".blp isn't read back.", false);
    } catch (const Glib::Error& e) {
        showMessage(Glib::ustring("Couldn't write ") + file.filename().string() + ": " + e.what(), true);
    }
}

void DesignerPage::goToHandler(int uid, const std::string& signal) {
    const auto language = codeLanguage();
    if (!language) {
        showMessage("There's no code for this design yet: Code → Write gtkmm Code or Write C Code first.", true);
        return;
    }
    // The generated code has to know the handler before the user's code can override it.
    if (modified()) {
        try {
            save();
        } catch (const Glib::Error& e) {
            showMessage(Glib::ustring("Couldn't save: ") + e.what(), true);
            return;
        }
    }
    Node* object = doc_.find(uid);
    if (!object) return;
    const bool cpp = *language == "gtkmm";
    const auto stub = ui::handlerStub(doc_, *object, signal, cpp, designer::gtkTypes());
    if (!stub) {
        showMessage("Only handlers in a window with an id get code (and the object needs an id too).", true);
        return;
    }
    const fs::path dir = path_.parent_path(), file = dir / stub->file;
    std::error_code ec;
    if (!fs::exists(file, ec)) writeCode(doc_, *language, true); // the user's file was removed: a fresh one
    std::string text = readText(file).value_or("");
    auto line = ui::findDefinition(text, stub->qualifiedName);
    if (!line) {
        try {
            if (cpp) {
                const fs::path header = dir / stub->declarationFile;
                const std::string declared = readText(header).value_or("");
                const std::string name = stub->qualifiedName.substr(stub->qualifiedName.find("::") + 2);
                const std::string className = stub->qualifiedName.substr(0, stub->qualifiedName.find("::"));
                if (declared.find(" " + name + "(") == std::string::npos)
                    Glib::file_set_contents(header.string(), ui::addDeclaration(declared, className, stub->declaration));
            }
            text += stub->definition;
            Glib::file_set_contents(file.string(), text);
        } catch (const Glib::Error& e) {
            showMessage(Glib::ustring("Couldn't add the handler: ") + e.what(), true);
            return;
        }
        line = ui::findDefinition(text, stub->qualifiedName);
        showMessage("Added " + stub->qualifiedName + " to " + stub->file, false);
    }
    // Just after the function's "{", so Enter starts its first line.
    std::istringstream lines(text);
    int number = 0;
    for (std::string l; std::getline(lines, l);)
        if (++number >= line.value_or(1))
            if (const auto brace = l.find('{'); brace != std::string::npos) {
                openFile_.emit(file, number, int(brace) + 2);
                return;
            }
    openFile_.emit(file, line.value_or(1), 1);
}

} // namespace kevc
