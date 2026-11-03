#pragma once

#include "core/UiDocument.h"
#include "core/UiEdit.h"
#include "editor/DocumentPage.h"

#include <gtkmm.h>
#include <gtksourceview/gtksource.h>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kevc {

// The box the design sits in. It tells the page when it's laid out again, so the selection
// rectangle drawn over it follows the widget.
class DesignFrame : public Gtk::Box {
public:
    DesignFrame() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {}
    sigc::signal<void()>& signalAllocated() { return allocated_; }

protected:
    void size_allocate_vfunc(int width, int height, int baseline) override {
        Gtk::Box::size_allocate_vfunc(width, height, baseline);
        allocated_.emit();
    }

private:
    sigc::signal<void()> allocated_;
};

// A GtkBuilder .ui file in the editor area: a live preview of the design (click a widget to select
// it), an outline of its objects, the selected object's properties, and the XML itself. Every edit
// goes through ui::UiDocument, so saving rewrites only what was edited.
class DesignerPage : public Gtk::Box, public DocumentPage {
public:
    // Throws Glib::Error / std::runtime_error if the file can't be read. A file that isn't valid
    // XML opens in the XML view with the error shown.
    explicit DesignerPage(const std::filesystem::path& file);
    ~DesignerPage() override;

    const std::filesystem::path& path() const override { return path_; }
    void moveTo(const std::filesystem::path& file) override;
    bool modified() const override;
    void save() override;
    Gtk::Widget& pageWidget() override { return *this; }
    Gtk::Widget& tabLabel() override { return tabBox_; }
    void focusPage() override;
    void activateTextAction(const char* action) override;
    sigc::signal<void()>& signalCloseRequested() override { return closeRequested_; }

    // The selected object (a node uid; 0 for none).
    int selected() const { return selected_; }
    void select(int uid);

    // Something worth a line in the status bar.
    sigc::signal<void(const Glib::ustring&)>& signalStatus() { return status_; }
    // A file to open at a line (Go to handler).
    sigc::signal<void(const std::filesystem::path&, int line, int column)>& signalOpenFile() { return openFile_; }
    // Code was written for the design: the project needs this pkg-config module (gtkmm-4.0, gtk4)
    // and these files (the generated ones and the user's).
    sigc::signal<void(const std::string& module, const std::vector<std::filesystem::path>& files)>& signalCodeWritten() {
        return codeWritten_;
    }

private:
    enum class View { Design, Xml };
    void showView(View view);
    void setBufferText(const std::string& text);
    std::string bufferText() const;
    std::string currentText() const;
    // Parses the XML view into the document; false (with the error shown) if it doesn't parse.
    bool applyXml();
    // Records an undo step around `change` (unless it changed nothing) and refreshes everything.
    void edit(const std::function<void()>& change);
    void undo();
    void redo();
    // Replaces the document with `text`, keeping the selection on the same element if it's there.
    void restore(const std::string& text);
    void documentChanged();
    void updateTitle();
    void showMessage(const Glib::ustring& text, bool error);

    void rebuildPreview();
    // Shows the notebook and stack pages that hold the selection, so it's on screen.
    void revealSelection();
    void stripMnemonics(GtkWidget* widget);
    void pickAt(double x, double y);
    void drawSelection(const Cairo::RefPtr<Cairo::Context>& cr);
    int rootOf(int uid) const; // the top-level object holding a node

    // Building: the palette, drag and drop, the clipboard, the context menu (DesignerEditing.cpp).
    struct DropPlan {
        ui::Placement placement;
        graphene_rect_t indicator{}; // in selectionLayer_'s coordinates
        bool line = false;           // between two children, else an area
    };
    void buildPalette();
    void setupEditing();
    int objectAt(double x, double y); // the object under a point of the selection layer
    std::optional<DropPlan> planDrop(double x, double y);
    // Where a new object of `cls` goes without a drop: into the selection, or after it.
    std::optional<ui::Placement> insertPlacement(const std::string& cls);
    void insertObject(const std::string& cls, const std::string& xml, const std::optional<ui::Placement>& where);
    bool drop(double x, double y);
    void showContextMenu(Gtk::Widget& over, double x, double y);
    void updateEditActions();
    bool textFieldFocused();
    void copySelected();
    void cutSelected();
    void paste();
    void deleteSelected();
    void wrapSelected(const std::string& cls);
    void shiftSelected(int delta);

    // Menus (DesignerMenus.cpp): <menu>s in the outline, a menu part's properties, adding entries.
    bool selectedIsMenuPart() const;
    void addMenuRows(const ui::Node& part, int depth);
    void rebuildMenuProperties(ui::Node& part);
    void addMenuEntry(ui::MenuPart what); // into the selected menu part, or beside a selected item
    void addTopLevelMenu();
    bool pasteMenuPart(const ui::UiDocument& pasted); // false if it holds no menu part
    int shownFor(int uid) const; // the preview widget to outline: the node's own, or what shows its menu

    // Code from the design (DesignerCode.cpp): "gtkmm" or "c".
    std::optional<std::string> codeLanguage() const; // the one already written, if any
    void writeCode(const ui::UiDocument& doc, const std::string& language, bool quiet);
    void exportBlueprint();
    void goToHandler(int uid, const std::string& signal);

    void rebuildOutline();
    void scheduleProperties();
    void rebuildProperties();
    void rebuildSignals(ui::Node* object);
    void restoreFocus(bool sameObject, const std::optional<std::string>& focused);
    Gtk::Widget& propertyEditor(int uid, GParamSpec* spec, const std::optional<std::string>& value);
    Gtk::Widget& rawEditor(int uid, const std::string& name, const std::string& value);
    void setProperty(int uid, const std::string& name, const std::string& value);
    void resetProperty(int uid, const std::string& name);

    void onFileChanged(Gio::FileMonitor::Event event);
    void watchFile();

    std::filesystem::path path_;
    ui::UiDocument doc_;
    std::string savedText_;
    std::vector<std::string> undo_, redo_;
    int selected_ = 0;
    int shownRoot_ = 0; // the top-level object in the preview
    View view_ = View::Design;
    bool syncing_ = false; // widgets being filled in: their change signals aren't edits
    bool propertiesPending_ = false;
    int sideWidth_ = 360;      // the outline and properties
    bool settingSplit_ = false;

    Gtk::Box tabBox_{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Label tabTitle_;
    Gtk::Button tabClose_;

    Gtk::Box bar_{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Box viewSwitch_{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::ToggleButton designButton_, xmlButton_;
    Gtk::Label message_;
    Gtk::MenuButton codeButton_;
    Gtk::Stack stack_;

    Gtk::Paned split_{Gtk::Orientation::HORIZONTAL};
    Gtk::Box designArea_{Gtk::Orientation::HORIZONTAL, 0}; // palette | canvas
    Gtk::ScrolledWindow paletteScroll_;
    Gtk::Box palette_{Gtk::Orientation::VERTICAL, 0};
    Gtk::ScrolledWindow canvasScroll_;
    Gtk::Overlay canvas_;
    DesignFrame frame_;                               // the design, which gets no input itself
    Gtk::Label fakeTitle_;                            // for a window without a header bar
    Gtk::DrawingArea selectionLayer_;                 // takes the clicks, draws the selection
    Gtk::Paned side_{Gtk::Orientation::VERTICAL};
    Gtk::ScrolledWindow outlineScroll_;
    Gtk::ListBox outline_;
    Gtk::Notebook inspector_; // Properties | Signals
    Gtk::ScrolledWindow propsScroll_;
    Gtk::Box props_{Gtk::Orientation::VERTICAL, 0};
    Gtk::ScrolledWindow signalsScroll_;
    Gtk::Box signals_{Gtk::Orientation::VERTICAL, 0};
    Gtk::PopoverMenu contextMenu_;
    Glib::RefPtr<Gio::SimpleActionGroup> editActions_; // design.cut, design.wrap('GtkBox')…

    Gtk::ScrolledWindow xmlScroll_;
    GtkSourceBuffer* xmlBuffer_ = nullptr;
    GtkSourceView* xmlView_ = nullptr;

    GtkBuilder* builder_ = nullptr; // owns the preview's objects
    GtkWidget* previewRoot_ = nullptr;
    std::map<int, GtkWidget*> widgets_; // node uid → its widget in the preview
    // The page each notebook (by uid) shows, and each stack's visible child, kept across rebuilds.
    std::map<int, int> notebookPages_, stackChildren_;
    std::set<int> previewRoots_;        // the top-level objects the preview can show
    std::vector<std::string> actionGroups_; // dummy groups inserted on frame_
    std::map<Gtk::ListBoxRow*, int> rowUids_;
    std::map<std::string, Gtk::Widget*> propertyEditors_; // name → its field ("" for the id)
    int propertiesUid_ = 0;                               // whose properties are shown
    Glib::RefPtr<Gio::FileMonitor> monitor_;
    std::string dragging_;             // "new:GtkButton" / "move:<uid>" while a drag from here is on
    std::optional<DropPlan> dropPlan_; // where it would land

    sigc::signal<void()> closeRequested_;
    sigc::signal<void(const Glib::ustring&)> status_;
    sigc::signal<void(const std::filesystem::path&, int, int)> openFile_;
    sigc::signal<void(const std::string&, const std::vector<std::filesystem::path>&)> codeWritten_;
};

} // namespace kevc
