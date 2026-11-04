#include "core/UiCodegen.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace kevc::ui;
namespace fs = std::filesystem;

namespace {

const std::string kNotes = R"(<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <requires lib="gtk" version="4.0"/>
  <object class="GtkApplicationWindow" id="main_window">
    <property name="title" translatable="yes">Notes</property>
    <property name="default-width">400</property>
    <signal name="close-request" handler="on_main_window_close_request"/>
    <child type="titlebar">
      <object class="GtkHeaderBar" id="header"/>
    </child>
    <child>
      <object class="GtkBox">
        <property name="orientation">vertical</property>
        <child>
          <object class="GtkEntry" id="title">
            <signal name="activate" handler="on_save"/>
          </object>
        </child>
        <child>
          <object class="GtkSwitch" id="dark-mode">
            <signal name="state-set" handler="on_dark_mode_state_set" after="yes"/>
          </object>
        </child>
        <child>
          <object class="GtkSpinButton" id="size">
            <property name="adjustment">
              <object class="GtkAdjustment" id="size_adjustment">
                <property name="upper">10</property>
              </object>
            </property>
          </object>
        </child>
        <child>
          <object class="AdwBanner" id="banner"/>
        </child>
        <child>
          <object class="GtkButton" id="save">
            <property name="label">_Save</property>
            <signal name="clicked" handler="on_save"/>
            <style>
              <class name="suggested-action"/>
            </style>
          </object>
        </child>
        <child>
          <object class="GtkButton">
            <signal name="clicked" handler="on_unnamed_clicked"/>
          </object>
        </child>
      </object>
    </child>
  </object>
  <template class="NotesRow" parent="GtkBox"/>
</interface>
)";

// A panel: a top-level widget of its own, with a popover its menu button names.
const std::string kPanel = R"(<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <requires lib="gtk" version="4.0"/>
  <object class="GtkBox" id="search_panel">
    <property name="orientation">vertical</property>
    <signal name="map" handler="on_search_panel_map"/>
    <child>
      <object class="GtkSearchEntry" id="query">
        <signal name="search-changed" handler="on_query_changed"/>
      </object>
    </child>
    <child>
      <object class="GtkMenuButton" id="options">
        <property name="popover">options_popover</property>
      </object>
    </child>
  </object>
  <object class="GtkPopover" id="options_popover">
    <child>
      <object class="GtkLabel" id="hint"/>
    </child>
  </object>
</interface>
)";

// Menus and the other top-level things a window's class holds: what it names, and what nothing names.
const std::string kMenus = R"(<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <requires lib="gtk" version="4.0"/>
  <object class="GtkApplicationWindow" id="main_window">
    <child>
      <object class="GtkBox">
        <child>
          <object class="GtkPopoverMenuBar" id="bar">
            <property name="menu-model">menubar</property>
          </object>
        </child>
        <child>
          <object class="GtkMenuButton" id="more">
            <property name="popover">more_popover</property>
          </object>
        </child>
      </object>
    </child>
  </object>
  <menu id="menubar">
    <submenu id="file_menu">
      <attribute name="label">_File</attribute>
      <section id="recent_section"/>
      <section>
        <item>
          <attribute name="label">_Quit</attribute>
          <attribute name="action">app.quit</attribute>
        </item>
      </section>
    </submenu>
  </menu>
  <menu id="context_menu">
    <item>
      <attribute name="label">_Copy</attribute>
      <attribute name="action">win.copy</attribute>
    </item>
  </menu>
  <object class="GtkPopover" id="more_popover">
    <child>
      <object class="GtkButton" id="more_reset">
        <signal name="clicked" handler="on_more_reset_clicked"/>
      </object>
    </child>
  </object>
  <object class="GtkStringList" id="sizes"/>
  <object class="GtkWindow" id="other_window">
    <child>
      <object class="GtkMenuButton">
        <property name="popover">other_popover</property>
      </object>
    </child>
  </object>
  <object class="GtkPopover" id="other_popover"/>
</interface>
)";

std::string fileText(const Code& code, const std::string& name, bool user = false) {
    for (const auto& f : user ? code.user : code.generated)
        if (f.name == name) return f.text;
    return "(missing " + name + ")";
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

bool hasPkgConfig(const std::string& module) {
    return std::system(("pkg-config --exists " + module + " 2>/dev/null").c_str()) == 0;
}

class TempDir {
public:
    explicit TempDir(const std::string& tag) {
        path_ = fs::temp_directory_path() / ("kevc-codegen-" + tag + "-" + std::to_string(::getpid()));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempDir() { fs::remove_all(path_); }
    const fs::path& path() const { return path_; }
    void write(const std::string& name, const std::string& text) const { std::ofstream(path_ / name) << text; }

private:
    fs::path path_;
};

} // namespace

TEST(UiCodegen, Names) {
    EXPECT_EQ(pascalCase("main_window"), "MainWindow");
    EXPECT_EQ(pascalCase("prefs-dialog2"), "PrefsDialog2");
    EXPECT_EQ(pascalCase("2nd"), "Window2nd");
    EXPECT_EQ(identifier("dark-mode"), "dark_mode");
    EXPECT_EQ(identifier("delete"), "delete_");
    EXPECT_EQ(identifier("3d"), "_3d");
}

TEST(UiCodegen, EmbedsTheDesignWithoutSignals) {
    const std::string ui = embeddedUi(UiDocument::parse(kNotes));
    EXPECT_FALSE(contains(ui, "<signal"));
    EXPECT_TRUE(contains(ui, "<property name=\"title\" translatable=\"yes\">Notes</property>"));
    EXPECT_NO_THROW(UiDocument::parse(ui));
}

TEST(UiCodegen, GtkmmBaseClassWithMembersAndHandlers) {
    const auto doc = UiDocument::parse(kNotes);
    const Code code = generateGtkmm(doc, "main_window.ui", basicGtkTypes());
    const std::string h = fileText(code, "main_window.ui.h"), cpp = fileText(code, "main_window.ui.cpp");
    EXPECT_TRUE(contains(h, "class MainWindowUi : public Gtk::ApplicationWindow {"));
    EXPECT_TRUE(contains(h, "Gtk::HeaderBar* header = nullptr;"));
    EXPECT_TRUE(contains(h, "Gtk::Switch* dark_mode = nullptr;"));
    EXPECT_TRUE(contains(h, "Glib::RefPtr<Gtk::Adjustment> size_adjustment;"));
    EXPECT_TRUE(contains(h, "Gtk::Widget* banner = nullptr;")); // gtkmm has no AdwBanner
    EXPECT_TRUE(contains(h, "virtual bool on_main_window_close_request() { return false; }"));
    EXPECT_TRUE(contains(h, "virtual void on_save() {}"));
    EXPECT_EQ(h.find("virtual void on_save()"), h.rfind("virtual void on_save()")); // once, for two signals
    EXPECT_TRUE(contains(cpp, "dark_mode = builder->get_widget<Gtk::Switch>(\"dark-mode\");"));
    EXPECT_TRUE(contains(cpp, "size_adjustment = builder->get_object<Gtk::Adjustment>(\"size_adjustment\");"));
    EXPECT_TRUE(contains(cpp, "g_signal_connect_data(gobj(), \"close-request\""));
    EXPECT_TRUE(contains(cpp, "-> gboolean { return self->on_main_window_close_request(); }"));
    EXPECT_TRUE(contains(cpp, "g_signal_connect_data(save->gobj(), \"clicked\""));
    EXPECT_TRUE(contains(cpp, "g_signal_connect_data(title->gobj(), \"activate\""));
    EXPECT_TRUE(contains(cpp, "GConnectFlags(G_CONNECT_SWAPPED | G_CONNECT_AFTER)")); // the switch's after="yes"
    EXPECT_FALSE(contains(cpp, "<signal name"));

    const std::string userH = fileText(code, "main_window.h", true), userCpp = fileText(code, "main_window.cpp", true);
    EXPECT_TRUE(contains(userH, "class MainWindow : public MainWindowUi {"));
    EXPECT_TRUE(contains(userH, "static MainWindow* create() { return MainWindowUi::create<MainWindow>(); }"));
    // Arguments of create() go on to the window's constructor.
    EXPECT_TRUE(contains(h, "static T* create(Args&&... args)"));
    EXPECT_TRUE(contains(h, "std::forward<Args>(args)..."));
    EXPECT_TRUE(contains(userH, "    // Signal handlers (Kev-C++ adds new ones here)\n    bool on_main_window_close_request() override;"));
    EXPECT_TRUE(contains(userCpp, "void MainWindow::on_save() {\n}"));

    // What it couldn't do, it says.
    ASSERT_EQ(code.warnings.size(), 2u);
    EXPECT_TRUE(contains(code.warnings[0], "NotesRow"));
    EXPECT_TRUE(contains(code.warnings[1], "on_unnamed_clicked"));
}

TEST(UiCodegen, CStructPrototypesAndStandIns) {
    const auto doc = UiDocument::parse(kNotes);
    const Code code = generateC(doc, "main_window.ui", basicGtkTypes());
    const std::string h = fileText(code, "main_window-ui.h"), c = fileText(code, "main_window-ui.c");
    EXPECT_TRUE(contains(h, "struct MainWindowUi {\n    GtkApplicationWindow *main_window;\n    GtkHeaderBar *header;"));
    EXPECT_TRUE(contains(h, "GtkWidget *banner;"));
    EXPECT_TRUE(contains(h, "GtkAdjustment *size_adjustment;"));
    EXPECT_TRUE(contains(h, "MainWindowUi *main_window_ui_new(void);"));
    EXPECT_TRUE(contains(h, "gboolean on_dark_mode_state_set(GtkSwitch *self, gboolean arg1, MainWindowUi *ui);"));
    EXPECT_TRUE(contains(h, "void on_save(GtkEntry *self, MainWindowUi *ui);"));
    EXPECT_TRUE(contains(c, "g_signal_connect_after(ui->dark_mode, \"state-set\", G_CALLBACK(on_dark_mode_state_set), ui);"));
    EXPECT_TRUE(contains(c, "g_signal_connect(ui->save, \"clicked\", G_CALLBACK(on_save), ui);"));
    EXPECT_TRUE(contains(c, "__attribute__((weak)) void on_save(GtkEntry *self, MainWindowUi *ui) {"));
    EXPECT_TRUE(contains(c, "static const char main_window_ui[] =\n    \"<?xml version=\\\"1.0\\\" encoding=\\\"UTF-8\\\"?>\\n\"\n"));
    const std::string user = fileText(code, "main_window.c", true);
    EXPECT_TRUE(contains(user, "#include \"main_window-ui.h\""));
    EXPECT_TRUE(contains(user, "gboolean on_dark_mode_state_set(GtkSwitch *self, gboolean arg1, MainWindowUi *ui) {\n"
                               "    (void) self;\n    (void) arg1;\n    (void) ui;\n    return FALSE;\n}"));
}

TEST(UiCodegen, GoToHandlerFindsOrWritesTheStub) {
    const auto doc = UiDocument::parse(kNotes);
    const Node* save = nullptr;
    for (Node* o : doc.objects())
        if (UiDocument::objectId(*o) == "save") save = o;
    ASSERT_TRUE(save);
    const auto cpp = handlerStub(doc, *save, "clicked", true, basicGtkTypes());
    ASSERT_TRUE(cpp);
    EXPECT_EQ(cpp->file, "main_window.cpp");
    EXPECT_EQ(cpp->declarationFile, "main_window.h");
    EXPECT_EQ(cpp->qualifiedName, "MainWindow::on_save");
    EXPECT_EQ(cpp->declaration, "    void on_save() override;\n");
    const auto c = handlerStub(doc, *save, "clicked", false, basicGtkTypes());
    ASSERT_TRUE(c);
    EXPECT_EQ(c->file, "main_window.c");
    EXPECT_TRUE(contains(c->definition, "void on_save(GtkEntry *self, MainWindowUi *ui) {"));
    EXPECT_FALSE(handlerStub(doc, *save, "destroy", true, basicGtkTypes())); // no handler for it

    const std::string source = "#include \"main_window.h\"\n\nvoid MainWindow::on_save() {\n}\n";
    EXPECT_EQ(findDefinition(source, "MainWindow::on_save"), 3);
    EXPECT_FALSE(findDefinition("    void on_save() override;\n", "MainWindow::on_save"));
    EXPECT_EQ(findDefinition("void on_save(GtkEntry *self,\n            MainWindowUi *ui) {\n", "on_save"), 1);
    EXPECT_FALSE(findDefinition("void on_save(GtkEntry *self, MainWindowUi *ui);\n", "on_save"));
    EXPECT_FALSE(findDefinition("void on_save_all(void) {\n", "on_save"));

    const std::string header = "class MainWindow : public MainWindowUi {\nprotected:\n    // Signal handlers (Kev-C++ adds new ones here)\n};\n";
    EXPECT_EQ(addDeclaration(header, "MainWindow", "    void on_quit() override;\n"),
              "class MainWindow : public MainWindowUi {\nprotected:\n    // Signal handlers (Kev-C++ adds new ones here)\n"
              "    void on_quit() override;\n};\n");
    EXPECT_EQ(addDeclaration("class MainWindow : public X {\npublic:\n    int a;\n};\n", "MainWindow", "    void q() override;\n"),
              "class MainWindow : public X {\npublic:\n    int a;\nprotected:\n    void q() override;\n};\n");
}

TEST(UiCodegen, Blueprint) {
    const auto doc = UiDocument::parse(R"(<interface>
  <object class="GtkApplicationWindow" id="win">
    <property name="title" translatable="yes">Hi "you"</property>
    <property name="default-width">300</property>
    <property name="resizable">False</property>
    <child type="titlebar"><object class="GtkHeaderBar"/></child>
    <child>
      <object class="GtkGrid" id="grid">
        <child>
          <object class="GtkButton" id="b">
            <property name="halign">center</property>
            <signal name="clicked" handler="on_b_clicked" swapped="yes"/>
            <style><class name="flat"/><class name="pill"/></style>
            <layout><property name="column">1</property><property name="row">0</property></layout>
          </object>
        </child>
        <child>
          <object class="GtkDropDown">
            <property name="model"><object class="GtkStringList"><items><item translatable="yes">One</item><item>Two</item></items></object></property>
          </object>
        </child>
      </object>
    </child>
  </object>
  <template class="MyRow" parent="GtkBox"><property name="spacing">4</property></template>
  <menu id="menu"><section><item><attribute name="label" translatable="yes">_Quit</attribute><attribute name="action">app.quit</attribute></item></section></menu>
</interface>)");
    EXPECT_EQ(generateBlueprint(doc, basicGtkTypes()),
              "// Written by Kev-C++ from the .ui file, which is what the designer edits: changes made\n"
              "// here aren't read back.\n"
              "using Gtk 4.0;\n"
              "\n"
              "ApplicationWindow win {\n"
              "  title: _(\"Hi \\\"you\\\"\");\n"
              "  default-width: 300;\n"
              "  resizable: false;\n"
              "  [titlebar] HeaderBar {\n"
              "  }\n"
              "  Grid grid {\n"
              "    Button b {\n"
              "      halign: center;\n"
              "      clicked => $on_b_clicked() swapped;\n"
              "      styles [\"flat\", \"pill\"]\n"
              "      layout {\n"
              "        column: 1;\n"
              "        row: 0;\n"
              "      }\n"
              "    }\n"
              "    DropDown {\n"
              "      model: StringList {\n"
              "        strings [_(\"One\"), \"Two\"]\n"
              "      };\n"
              "    }\n"
              "  }\n"
              "}\n"
              "\n"
              "template $MyRow : Box {\n"
              "  spacing: 4;\n"
              "}\n"
              "\n"
              "menu menu {\n"
              "  section {\n"
              "    item {\n"
              "      label: _(\"_Quit\");\n"
              "      action: \"app.quit\";\n"
              "    }\n"
              "  }\n"
              "}\n");
}

TEST(UiCodegen, TopLevelWidgetsGetClassesUnlessAnotherObjectNamesThem) {
    const auto doc = UiDocument::parse(kPanel);
    const auto roots = codeRoots(doc, basicGtkTypes());
    ASSERT_EQ(roots.size(), 1u); // not the popover: the menu button shows it
    EXPECT_EQ(UiDocument::objectId(*roots[0]), "search_panel");

    const Code code = generateGtkmm(doc, "search_panel.ui", basicGtkTypes());
    EXPECT_TRUE(code.warnings.empty());
    const std::string h = fileText(code, "search_panel.ui.h");
    EXPECT_TRUE(contains(h, "class SearchPanelUi : public Gtk::Box {"));
    EXPECT_TRUE(contains(h, "static Glib::RefPtr<T> create(Args&&... args) {"));
    EXPECT_TRUE(contains(h, "Gtk::SearchEntry* query = nullptr;"));
    EXPECT_TRUE(contains(h, "virtual void on_query_changed() {}"));
    EXPECT_TRUE(contains(fileText(code, "search_panel.h", true), "static Glib::RefPtr<SearchPanel> create()"));
    EXPECT_FALSE(contains(h, "OptionsPopover"));
    EXPECT_TRUE(contains(h, "Gtk::Popover* options_popover = nullptr;")); // the panel's, as a member

    const Code c = generateC(doc, "search_panel.ui", basicGtkTypes());
    EXPECT_TRUE(contains(fileText(c, "search_panel-ui.c"), "g_object_force_floating(G_OBJECT(ui->search_panel));"));

    // A window in the same file still gets a window's create().
    const Code both = generateGtkmm(UiDocument::parse(kNotes), "main_window.ui", basicGtkTypes());
    EXPECT_TRUE(contains(fileText(both, "main_window.ui.h"), "static T* create(Args&&... args) {"));
}

// A program's exit code from `cmd` (77: no display to run it on).
int runStatus(const std::string& cmd) {
    const int status = std::system(cmd.c_str());
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// A designed widget lives while something holds it: moving it between containers keeps it, and it
// goes once nothing does. Built and run against the real gtkmm and GTK (run only with a display).
TEST(UiCodegen, MenusAndWhatAWindowNamesAreMembersOfItsClass) {
    const auto doc = UiDocument::parse(kMenus);
    const auto types = basicGtkTypes();
    const Code code = generateGtkmm(doc, "main_window.ui", types);
    EXPECT_TRUE(code.warnings.empty());
    const std::string h = fileText(code, "main_window.ui.h"), cpp = fileText(code, "main_window.ui.cpp");
    const std::string mine = h.substr(h.find("class MainWindowUi"), h.find("class OtherWindowUi") - h.find("class MainWindowUi"));
    const std::string other = h.substr(h.find("class OtherWindowUi"));
    // What the window names (and what those name), and the menus and objects nothing names.
    for (const char* member : {"Gtk::PopoverMenuBar* bar = nullptr;", "Gtk::Popover* more_popover = nullptr;",
                               "Gtk::Button* more_reset = nullptr;", "Glib::RefPtr<Gio::Menu> menubar;",
                               "Glib::RefPtr<Gio::Menu> file_menu;", "Glib::RefPtr<Gio::Menu> recent_section;",
                               "Glib::RefPtr<Gio::Menu> context_menu;", "Glib::RefPtr<Gtk::StringList> sizes;",
                               "virtual void on_more_reset_clicked() {}"})
        EXPECT_TRUE(contains(mine, member)) << member;
    EXPECT_FALSE(contains(mine, "other_popover"));
    // The other window has its own popover and the shared ones, not the first window's.
    EXPECT_TRUE(contains(other, "Gtk::Popover* other_popover = nullptr;"));
    EXPECT_TRUE(contains(other, "Glib::RefPtr<Gio::Menu> context_menu;"));
    EXPECT_FALSE(contains(other, "menubar"));
    EXPECT_FALSE(contains(other, "more_popover"));
    EXPECT_TRUE(contains(cpp, "menubar = builder->get_object<Gio::Menu>(\"menubar\");"));
    EXPECT_TRUE(contains(cpp, "g_signal_connect_data(more_reset->gobj(), \"clicked\","));
    // Go to handler finds the class a popover's button belongs to.
    Node* reset = nullptr;
    for (Node* o : doc.objects())
        if (UiDocument::objectId(*o) == "more_reset") reset = o;
    ASSERT_NE(reset, nullptr);
    const auto stub = handlerStub(doc, *reset, "clicked", true, types);
    ASSERT_TRUE(stub.has_value());
    EXPECT_EQ(stub->qualifiedName, "MainWindow::on_more_reset_clicked");

    const Code c = generateC(doc, "main_window.ui", types);
    const std::string ch = fileText(c, "main_window-ui.h"), cc = fileText(c, "main_window-ui.c");
    EXPECT_TRUE(contains(ch, "GMenu *menubar;"));
    EXPECT_TRUE(contains(cc, "ui->menubar = (GMenu *) gtk_builder_get_object(builder, \"menubar\");"));

    if (!hasPkgConfig("gtkmm-4.0") || !hasPkgConfig("gtk4")) GTEST_SKIP() << "gtkmm-4.0 or gtk4 isn't installed";
    TempDir dir("menus");
    for (const auto& f : code.generated) dir.write(f.name, f.text);
    for (const auto& f : code.user) dir.write(f.name, f.text);
    dir.write("main.cpp", "#include \"main_window.h\"\n"
                          "int main() {\n"
                          "    if (!gtk_init_check()) return 77; // no display (CI): gtkmm's own gtk_init() would exit(1)\n"
                          "    auto app = Gtk::Application::create(\"org.kevcpp.MenuTest\", Gio::Application::Flags::NON_UNIQUE);\n"
                          "    struct Probe : MainWindowUi {\n"
                          "        Probe(BaseObjectType* o, const Glib::RefPtr<Gtk::Builder>& b) : MainWindowUi(o, b) {}\n"
                          "        int check() {\n"
                          "            if (!menubar || menubar->get_n_items() != 1) return 1;\n"
                          "            if (!file_menu || file_menu->get_n_items() != 2 || recent_section->get_n_items() != 0) return 2;\n"
                          "            if (bar->get_menu_model() != menubar) return 3;\n"
                          "            if (more->get_popover() != more_popover || !more_reset) return 4;\n"
                          "            recent_section->append(\"notes.txt\", \"win.open\");\n"
                          "            return file_menu->get_n_items() == 2 && context_menu->get_n_items() == 1 ? 0 : 5;\n"
                          "        }\n"
                          "    };\n"
                          "    auto* window = MainWindowUi::create<Probe>();\n"
                          "    const int status = window->check();\n"
                          "    delete window;\n"
                          "    return status;\n"
                          "}\n");
    const std::string cd = "cd '" + dir.path().string() + "' && ";
    ASSERT_EQ(runStatus(cd + "g++ -std=c++17 -Wall -Wextra -Werror main.cpp main_window.ui.cpp -o app "
                             "$(pkg-config --cflags --libs gtkmm-4.0) >build.log 2>&1"), 0)
        << std::ifstream(dir.path() / "build.log").rdbuf();
    const int run = runStatus(cd + "./app >run.log 2>&1");
    if (run != 77) EXPECT_EQ(run, 0) << std::ifstream(dir.path() / "run.log").rdbuf();
    for (const auto& f : c.generated) dir.write(f.name, f.text);
    dir.write("main.c", "#include \"main_window-ui.h\"\nint main(int argc, char **argv) {\n"
                        "    (void) argv;\n    if (argc > 99) main_window_ui_new();\n    return 0;\n}\n");
    EXPECT_EQ(runStatus(cd + "gcc -std=c17 -Wall -Wextra -Werror main.c main_window-ui.c -o capp "
                             "$(pkg-config --cflags --libs gtk4) >build.log 2>&1"), 0)
        << std::ifstream(dir.path() / "build.log").rdbuf();
}

TEST(UiCodegen, GeneratedPanelCodeBuildsAndLivesAsLongAsItsHolders) {
    if (!hasPkgConfig("gtkmm-4.0") || !hasPkgConfig("gtk4")) GTEST_SKIP() << "gtkmm-4.0 or gtk4 isn't installed";
    const auto doc = UiDocument::parse(kPanel);
    TempDir dir("panel");
    const Code cpp = generateGtkmm(doc, "search_panel.ui", basicGtkTypes());
    for (const auto& f : cpp.generated) dir.write(f.name, f.text);
    for (const auto& f : cpp.user) dir.write(f.name, f.text);
    dir.write("main.cpp", "#include \"search_panel.h\"\n"
                          "static bool gone = false;\n"
                          "int main() {\n"
                          "    if (!gtk_init_check()) return 77; // no display (CI): gtkmm's own gtk_init() would exit(1)\n"
                          "    auto app = Gtk::Application::create(\"org.kevcpp.PanelTest\", Gio::Application::Flags::NON_UNIQUE);\n"
                          "    auto panel = SearchPanel::create();\n"
                          "    g_object_weak_ref(G_OBJECT(panel->gobj()), [](gpointer, GObject*) { gone = true; }, nullptr);\n"
                          "    Gtk::Box box;\n"
                          "    box.append(*panel);\n"
                          "    box.remove(*panel);\n"
                          "    if (gone) return 1; // the RefPtr still holds it\n"
                          "    box.append(*panel);\n"
                          "    panel.reset();\n"
                          "    if (gone) return 2; // the box holds it\n"
                          "    box.remove(*box.get_first_child());\n"
                          "    return gone ? 0 : 3; // nothing does\n"
                          "}\n");
    const std::string cd = "cd '" + dir.path().string() + "' && ";
    ASSERT_EQ(runStatus(cd + "g++ -std=c++17 -Wall -Wextra -Werror main.cpp search_panel.cpp search_panel.ui.cpp -o app "
                             "$(pkg-config --cflags --libs gtkmm-4.0) >build.log 2>&1"), 0)
        << std::ifstream(dir.path() / "build.log").rdbuf();
    const int cppRun = runStatus(cd + "./app >run.log 2>&1");
    if (cppRun != 77) EXPECT_EQ(cppRun, 0) << std::ifstream(dir.path() / "run.log").rdbuf();

    const Code c = generateC(doc, "search_panel.ui", basicGtkTypes());
    for (const auto& f : c.generated) dir.write(f.name, f.text);
    dir.write("main.c", "#include \"search_panel-ui.h\"\n"
                        "static gboolean gone = FALSE;\n"
                        "static void on_gone(gpointer data, GObject *object) { (void) data; (void) object; gone = TRUE; }\n"
                        "int main(void) {\n"
                        "    if (!gtk_init_check()) return 77;\n"
                        "    SearchPanelUi *ui = search_panel_ui_new();\n"
                        "    GtkWidget *panel = GTK_WIDGET(ui->search_panel);\n"
                        "    g_object_weak_ref(G_OBJECT(panel), on_gone, NULL);\n"
                        "    if (!g_object_is_floating(panel)) return 1; /* new, like gtk_box_new()'s */\n"
                        "    GtkWidget *box = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));\n"
                        "    gtk_box_append(GTK_BOX(box), panel);\n"
                        "    if (gone) return 2;\n"
                        "    gtk_box_remove(GTK_BOX(box), panel);\n"
                        "    g_object_unref(box);\n"
                        "    return gone ? 0 : 3;\n"
                        "}\n");
    ASSERT_EQ(runStatus(cd + "gcc -std=c17 -Wall -Wextra -Werror main.c search_panel-ui.c -o capp "
                             "$(pkg-config --cflags --libs gtk4) >build.log 2>&1"), 0)
        << std::ifstream(dir.path() / "build.log").rdbuf();
    const int cRun = runStatus(cd + "./capp >run.log 2>&1");
    if (cRun != 77) EXPECT_EQ(cRun, 0) << std::ifstream(dir.path() / "run.log").rdbuf();
}

// The generated code compiles and links against the real libraries (where they're installed).
TEST(UiCodegen, GeneratedGtkmmCodeBuilds) {
    if (!hasPkgConfig("gtkmm-4.0")) GTEST_SKIP() << "gtkmm-4.0 development files aren't installed";
    const auto doc = UiDocument::parse(kNotes);
    const Code code = generateGtkmm(doc, "main_window.ui", basicGtkTypes());
    TempDir dir("gtkmm");
    for (const auto& f : code.generated) dir.write(f.name, f.text);
    for (const auto& f : code.user) dir.write(f.name, f.text);
    // main.cpp also has a window class of its own whose constructor takes more than the builder
    // (as Kev-C++'s own windows do): create() hands the extra arguments on.
    dir.write("main.cpp", "#include \"main_window.h\"\n#include <string>\n"
                          "class Titled : public MainWindowUi {\n"
                          "public:\n"
                          "    Titled(BaseObjectType* o, const Glib::RefPtr<Gtk::Builder>& b, const std::string& t, int n)\n"
                          "        : MainWindowUi(o, b) { set_title(t + std::to_string(n)); }\n"
                          "};\n"
                          "int main(int argc, char**) {\n"
                          "    if (argc > 99) MainWindow::create();\n"
                          "    if (argc > 99) MainWindowUi::create<Titled>(std::string(\"Notes \"), 2);\n"
                          "}\n");
    const std::string cmd = "cd '" + dir.path().string() + "' && g++ -std=c++17 -Wall -Wextra -Werror main.cpp main_window.cpp "
                            "main_window.ui.cpp -o app $(pkg-config --cflags --libs gtkmm-4.0) 2>&1";
    EXPECT_EQ(std::system((cmd + " >build.log").c_str()), 0) << std::ifstream(dir.path() / "build.log").rdbuf();
}

TEST(UiCodegen, GeneratedCCodeBuildsWithAndWithoutHandlers) {
    if (!hasPkgConfig("gtk4")) GTEST_SKIP() << "gtk4 development files aren't installed";
    const auto doc = UiDocument::parse(kNotes);
    const Code code = generateC(doc, "main_window.ui", basicGtkTypes());
    TempDir dir("c");
    for (const auto& f : code.generated) dir.write(f.name, f.text);
    dir.write("main.c", "#include \"main_window-ui.h\"\nint main(int argc, char **argv) {\n"
                        "    (void) argv;\n    if (argc > 99) main_window_ui_new();\n    return 0;\n}\n");
    const std::string compile = "cd '" + dir.path().string() + "' && gcc -std=c17 -Wall -Wextra -Werror ";
    const std::string libs = " -o app $(pkg-config --cflags --libs gtk4) >build.log 2>&1";
    // Before the user writes any handler: the stand-ins link.
    EXPECT_EQ(std::system((compile + "main.c main_window-ui.c" + libs).c_str()), 0)
        << std::ifstream(dir.path() / "build.log").rdbuf();
    // With the user's file: theirs replace the stand-ins.
    for (const auto& f : code.user) dir.write(f.name, f.text);
    EXPECT_EQ(std::system((compile + "main.c main_window-ui.c main_window.c" + libs).c_str()), 0)
        << std::ifstream(dir.path() / "build.log").rdbuf();
}

// Kev-C++'s own designs (src/**/*.ui with generated gtkmm code beside them): the code embeds the
// design as it is now. It goes stale only if a .ui is changed outside Kev-C++'s designer, which
// rewrites the code on every save; open the .ui in Kev-C++ and save it to bring the code up to date.
TEST(UiCodegen, KevCppsOwnDesignsMatchTheirCode) {
    int checked = 0;
    for (const auto& entry : fs::recursive_directory_iterator(fs::path(KEVC_SOURCE_DIR) / "src")) {
        if (entry.path().extension() != ".ui") continue;
        const fs::path code = entry.path().parent_path() / (entry.path().stem().string() + ".ui.cpp");
        ASSERT_TRUE(fs::exists(code)) << code << " is missing: open " << entry.path().filename()
                                      << " in Kev-C++ and use Code → Write gtkmm Code";
        std::ifstream uiIn(entry.path()), codeIn(code);
        std::stringstream ui, text;
        ui << uiIn.rdbuf();
        text << codeIn.rdbuf();
        const std::string embedded = "R\"__kevc_ui__(" + embeddedUi(UiDocument::parse(ui.str())) + ")__kevc_ui__\"";
        EXPECT_TRUE(contains(text.str(), embedded))
            << code.filename() << " is older than " << entry.path().filename() << ": open the .ui in Kev-C++ and save it";
        ++checked;
    }
    EXPECT_GE(checked, 23); // G4: the splash, the dialogs (step 2), the panels (step 3) and the main window (step 4)
}
