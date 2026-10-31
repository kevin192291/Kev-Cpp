#include "core/UiCodegen.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
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

// The generated code compiles and links against the real libraries (where they're installed).
TEST(UiCodegen, GeneratedGtkmmCodeBuilds) {
    if (!hasPkgConfig("gtkmm-4.0")) GTEST_SKIP() << "gtkmm-4.0 development files aren't installed";
    const auto doc = UiDocument::parse(kNotes);
    const Code code = generateGtkmm(doc, "main_window.ui", basicGtkTypes());
    TempDir dir("gtkmm");
    for (const auto& f : code.generated) dir.write(f.name, f.text);
    for (const auto& f : code.user) dir.write(f.name, f.text);
    dir.write("main.cpp", "#include \"main_window.h\"\nint main(int argc, char**) { if (argc > 99) MainWindow::create(); }\n");
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
