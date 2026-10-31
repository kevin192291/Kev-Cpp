#include "core/UiEdit.h"

#include <fstream>

#include <gtest/gtest.h>

using namespace kevc::ui;

namespace {

Catalog catalog() {
    std::ifstream in(std::string(KEVC_SOURCE_DIR) + "/data/designer/catalog.json");
    return Catalog::parse(std::string((std::istreambuf_iterator<char>(in)), {}));
}

const std::string kForm = R"(<interface>
  <object class="GtkBox" id="form">
    <property name="orientation">vertical</property>
    <child>
      <object class="GtkLabel" id="first">
        <property name="label">First</property>
      </object>
    </child>
    <child>
      <object class="GtkLabel" id="second"/>
    </child>
    <style>
      <class name="card"/>
    </style>
  </object>
  <object class="GtkGrid" id="grid">
    <child>
      <object class="GtkLabel" id="cell">
        <layout>
          <property name="column">1</property>
          <property name="row">2</property>
        </layout>
      </object>
    </child>
  </object>
  <object class="GtkStack" id="stack"/>
  <object class="GtkCenterBox" id="center"/>
  <object class="GtkBox" id="empty">
    <style>
      <class name="toolbar"/>
    </style>
  </object>
</interface>
)";

Node* byId(UiDocument& doc, const std::string& id) {
    for (Node* o : doc.objects())
        if (UiDocument::objectId(*o) == id) return o;
    return nullptr;
}

std::vector<std::string> childIds(const Node& parent) {
    std::vector<std::string> out;
    for (Node* o : UiDocument::childObjects(parent)) out.push_back(UiDocument::objectId(*o));
    return out;
}

// The result is a well-formed file that reads back the same.
void expectSound(const UiDocument& doc) {
    const std::string text = doc.text();
    EXPECT_EQ(UiDocument::parse(text).text(), text);
}

} // namespace

TEST(UiCatalog, ReadsTheBundledCatalog) {
    const Catalog c = catalog();
    ASSERT_GE(c.groups().size(), 4u);
    EXPECT_EQ(c.groups().front(), "Windows");
    const ClassInfo* box = c.find("GtkBox");
    ASSERT_TRUE(box);
    EXPECT_EQ(box->slots, Slots::Children);
    EXPECT_EQ(box->group, "Containers");
    const ClassInfo* header = c.find("GtkHeaderBar");
    ASSERT_TRUE(header);
    EXPECT_EQ(header->slots, Slots::Typed);
    EXPECT_EQ(header->names, (std::vector<std::string>{"start", "end"}));
    EXPECT_EQ(header->property, "title-widget");
    EXPECT_EQ(c.find("GtkNotebook")->page, "GtkNotebookPage");
    EXPECT_EQ(c.find("GtkButton")->slots, Slots::None);
    EXPECT_EQ(c.find("GtkWindow")->slots, Slots::Window);
    EXPECT_EQ(c.find("NoSuchWidget"), nullptr);
    for (const auto& info : c.classes()) EXPECT_FALSE(info.group.empty()) << info.cls;
    EXPECT_THROW(Catalog::parse(R"({"classes":[{"class":"X","slots":"sideways"}]})"), std::runtime_error);
}

TEST(UiEdit, NewObjectsGetFreeIdsAndDefaults) {
    auto doc = UiDocument::parse(R"(<interface><object class="GtkButton" id="button1"/><object class="GtkSpinButton" id="spin_button1"/></interface>)");
    EXPECT_EQ(freshId(doc, "GtkButton"), "button2");
    EXPECT_EQ(freshId(doc, "GtkSpinButton"), "spin_button2");
    EXPECT_EQ(freshId(doc, "GtkLabel"), "label1");
    EXPECT_EQ(freshId(doc, "AdwStatusPage"), "status_page1");
    EXPECT_EQ(freshId(doc, "MyWidget"), "my_widget1");
    EXPECT_EQ(freshId(doc, "GtkButton", {"button2"}), "button3");

    const Catalog c = catalog();
    const std::string box = newObjectXml(doc, "GtkBox", c.find("GtkBox"));
    EXPECT_EQ(box, "<object class=\"GtkBox\" id=\"box1\"><property name=\"orientation\">vertical</property>"
                   "<property name=\"spacing\">6</property></object>");
    EXPECT_NE(newObjectXml(doc, "GtkSpinButton", c.find("GtkSpinButton")).find("GtkAdjustment"), std::string::npos);
    // In the catalog's order: the title first.
    EXPECT_EQ(newObjectXml(doc, "GtkWindow", c.find("GtkWindow")).find("<property name=\"title\">"), 39u);
    EXPECT_EQ(newObjectXml(doc, "MyWidget", nullptr), "<object class=\"MyWidget\" id=\"my_widget1\"></object>");
}

TEST(UiEdit, PlacesChildrenInOrderAndIndented) {
    auto doc = UiDocument::parse(kForm);
    const Catalog c = catalog();
    Node* form = byId(doc, "form");
    Node* added = place(doc, {Placement::Kind::Child, form, "", nullptr}, R"(<object class="GtkButton" id="ok"/>)", c);
    ASSERT_TRUE(added);
    EXPECT_EQ(UiDocument::objectId(*added), "ok");
    EXPECT_EQ(childIds(*form), (std::vector<std::string>{"first", "second", "ok"}));
    // After the last child, before <style>, laid out like the others.
    EXPECT_NE(doc.text().find("      <object class=\"GtkLabel\" id=\"second\"/>\n"
                              "    </child>\n"
                              "    <child>\n"
                              "      <object class=\"GtkButton\" id=\"ok\"/>\n"
                              "    </child>\n"
                              "    <style>"),
              std::string::npos)
        << doc.text();

    place(doc, {Placement::Kind::Child, form, "", byId(doc, "second")}, R"(<object class="GtkEntry" id="name"/>)", c);
    EXPECT_EQ(childIds(*form), (std::vector<std::string>{"first", "name", "second", "ok"}));

    // A container with no children yet: before its <style>.
    Node* empty = byId(doc, "empty");
    place(doc, {Placement::Kind::Child, empty, "start", nullptr}, R"(<object class="GtkButton" id="b"/>)", c);
    EXPECT_NE(doc.text().find("  <object class=\"GtkBox\" id=\"empty\">\n"
                              "    <child type=\"start\">\n"
                              "      <object class=\"GtkButton\" id=\"b\"/>\n"
                              "    </child>\n"
                              "    <style>"),
              std::string::npos)
        << doc.text();
    expectSound(doc);
}

TEST(UiEdit, PlacesIntoPropertiesGridCellsPagesAndTheTopLevel) {
    auto doc = UiDocument::parse(kForm);
    const Catalog c = catalog();
    Node* center = byId(doc, "center");
    place(doc, {Placement::Kind::Property, center, "end-widget"}, R"(<object class="GtkLabel" id="right"/>)", c);
    ASSERT_TRUE(propertyObject(*center, "end-widget"));
    EXPECT_EQ(UiDocument::objectId(*propertyObject(*center, "end-widget")), "right");
    // A property slot holds one: placing again replaces it.
    place(doc, {Placement::Kind::Property, center, "end-widget"}, R"(<object class="GtkLabel" id="right2"/>)", c);
    EXPECT_EQ(UiDocument::objectId(*propertyObject(*center, "end-widget")), "right2");
    EXPECT_EQ(byId(doc, "right"), nullptr);

    Node* grid = byId(doc, "grid");
    Node* cell = place(doc, {Placement::Kind::Grid, grid, "", nullptr, 3, 0}, R"(<object class="GtkButton" id="g"/>)", c);
    EXPECT_EQ(layoutProperty(*cell, "column"), "3");
    EXPECT_EQ(layoutProperty(*cell, "row"), "0");
    EXPECT_EQ(layoutProperty(*byId(doc, "cell"), "row"), "2");

    Node* stack = byId(doc, "stack");
    Node* paged = place(doc, {Placement::Kind::Page, stack}, R"(<object class="GtkLabel" id="p1"/>)", c);
    ASSERT_TRUE(paged);
    EXPECT_EQ(UiDocument::objectId(*paged), "p1");
    Node* page = UiDocument::parentObject(*paged);
    EXPECT_EQ(UiDocument::className(*page), "GtkStackPage");
    EXPECT_EQ(UiDocument::property(*page, "name"), "page1");
    EXPECT_EQ(UiDocument::property(*page, "title"), "Page 1");
    place(doc, {Placement::Kind::Page, stack}, R"(<object class="GtkLabel" id="p2"/>)", c);
    EXPECT_EQ(UiDocument::childObjects(*stack).size(), 2u);

    Node* window = place(doc, {Placement::Kind::TopLevel}, R"(<object class="GtkWindow" id="w"/>)", c);
    EXPECT_EQ(doc.topLevelObjects().back(), window);
    expectSound(doc);
}

TEST(UiEdit, RemovesWhatHoldsAnObject) {
    auto doc = UiDocument::parse(kForm);
    const Catalog c = catalog();
    removeObject(doc, *byId(doc, "second"));
    EXPECT_EQ(childIds(*byId(doc, "form")), (std::vector<std::string>{"first"}));
    EXPECT_EQ(doc.text().find("<child>\n      <object class=\"GtkLabel\" id=\"second\"/>"), std::string::npos);

    Node* stack = byId(doc, "stack");
    Node* paged = place(doc, {Placement::Kind::Page, stack}, R"(<object class="GtkLabel" id="p1"/>)", c);
    removeObject(doc, *paged);
    EXPECT_TRUE(UiDocument::childObjects(*byId(doc, "stack")).empty()); // the page went too

    place(doc, {Placement::Kind::Property, byId(doc, "center"), "start-widget"}, R"(<object class="GtkLabel" id="l"/>)", c);
    removeObject(doc, *byId(doc, "l"));
    EXPECT_FALSE(UiDocument::propertyNode(*byId(doc, "center"), "start-widget"));

    removeObject(doc, *byId(doc, "empty"));
    EXPECT_EQ(byId(doc, "empty"), nullptr);
    expectSound(doc);
}

TEST(UiEdit, MovesObjectsBetweenContainers) {
    auto doc = UiDocument::parse(kForm);
    const Catalog c = catalog();
    // Out of a grid cell into a box: its <layout> stays behind.
    Node* moved = moveObject(doc, *byId(doc, "cell"), {Placement::Kind::Child, byId(doc, "form"), "", byId(doc, "first")}, c);
    ASSERT_TRUE(moved);
    EXPECT_EQ(UiDocument::objectId(*moved), "cell");
    EXPECT_FALSE(moved->firstChild("layout"));
    EXPECT_EQ(childIds(*byId(doc, "form")), (std::vector<std::string>{"cell", "first", "second"}));
    EXPECT_TRUE(UiDocument::childObjects(*byId(doc, "grid")).empty());

    // Into a grid: a cell of its own.
    moved = moveObject(doc, *byId(doc, "first"), {Placement::Kind::Grid, byId(doc, "grid"), "", nullptr, 0, 4}, c);
    EXPECT_EQ(layoutProperty(*moved, "row"), "4");
    EXPECT_EQ(UiDocument::property(*moved, "label"), "First"); // its properties came along

    // Not into itself.
    Node* form = byId(doc, "form");
    EXPECT_EQ(moveObject(doc, *form, {Placement::Kind::Child, form, "", nullptr}, c), nullptr);
    expectSound(doc);
}

TEST(UiEdit, WrapsAnObjectInAContainer) {
    auto doc = UiDocument::parse(kForm);
    const Catalog c = catalog();
    Node* scroller = wrapObject(doc, *byId(doc, "first"), newObjectXml(doc, "GtkScrolledWindow", c.find("GtkScrolledWindow")), c);
    ASSERT_TRUE(scroller);
    EXPECT_EQ(UiDocument::className(*scroller), "GtkScrolledWindow");
    EXPECT_EQ(childIds(*byId(doc, "form")), (std::vector<std::string>{"scrolled_window1", "second"}));
    EXPECT_EQ(childIds(*scroller), (std::vector<std::string>{"first"}));

    // In a grid the container takes the cell.
    Node* box = wrapObject(doc, *byId(doc, "cell"), newObjectXml(doc, "GtkBox", c.find("GtkBox")), c);
    EXPECT_EQ(layoutProperty(*box, "column"), "1");
    EXPECT_FALSE(layoutProperty(*byId(doc, "cell"), "column"));

    // Into a property slot container: its first slot.
    Node* paned = wrapObject(doc, *byId(doc, "second"), newObjectXml(doc, "GtkPaned", c.find("GtkPaned")), c);
    ASSERT_TRUE(propertyObject(*paned, "start-child"));
    EXPECT_EQ(UiDocument::objectId(*propertyObject(*paned, "start-child")), "second");

    EXPECT_EQ(wrapObject(doc, *byId(doc, "form"), "<object class=\"GtkBox\"/>", c), nullptr); // top level
    expectSound(doc);
}

TEST(UiEdit, ShiftsObjectsAmongTheirSiblings) {
    auto doc = UiDocument::parse(kForm);
    Node* second = shiftObject(doc, *byId(doc, "second"), -1);
    ASSERT_TRUE(second);
    EXPECT_EQ(UiDocument::objectId(*second), "second");
    EXPECT_EQ(childIds(*byId(doc, "form")), (std::vector<std::string>{"second", "first"}));
    EXPECT_EQ(shiftObject(doc, *second, -1), nullptr); // already first
    EXPECT_TRUE(shiftObject(doc, *byId(doc, "second"), 1));
    EXPECT_EQ(childIds(*byId(doc, "form")), (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(doc.text(), kForm); // and back exactly as it was
}

TEST(UiEdit, SetsSignalHandlers) {
    auto doc = UiDocument::parse(kForm);
    Node* first = byId(doc, "first");
    setSignalHandler(doc, *first, "notify::label", "on_first_label");
    ASSERT_EQ(signalHandlers(*first).size(), 1u);
    EXPECT_EQ(signalHandlers(*first)[0].handler, "on_first_label");
    EXPECT_NE(doc.text().find("        <property name=\"label\">First</property>\n"
                              "        <signal name=\"notify::label\" handler=\"on_first_label\"/>\n"),
              std::string::npos)
        << doc.text();
    setSignalHandler(doc, *first, "notify::label", "on_label_changed");
    EXPECT_EQ(signalHandlers(*first)[0].handler, "on_label_changed");
    setSignalHandler(doc, *first, "notify::label", "");
    EXPECT_TRUE(signalHandlers(*first).empty());
    EXPECT_EQ(doc.text(), kForm);
}

TEST(UiEdit, PastedIdsDontCollide) {
    auto doc = UiDocument::parse(kForm);
    const std::string pasted = withUniqueIds(doc, R"(<object class="GtkBox" id="form"><child><object class="GtkLabel" id="first"><property name="mnemonic-widget">entry7</property></object></child><child><object class="GtkEntry" id="entry7"/></child><child><object class="GtkLabel" id="new_one"/></child></object>)");
    EXPECT_NE(pasted.find("id=\"form1\""), std::string::npos) << pasted;
    EXPECT_NE(pasted.find("id=\"first1\""), std::string::npos);
    EXPECT_NE(pasted.find("id=\"entry7\""), std::string::npos); // not taken: kept
    EXPECT_NE(pasted.find("id=\"new_one\""), std::string::npos);

    const std::string again = withUniqueIds(UiDocument::parse(R"(<interface><object class="GtkEntry" id="entry7"/></interface>)"),
                                            R"(<object class="GtkBox"><child><object class="GtkLabel"><property name="mnemonic-widget">entry7</property></object></child><child><object class="GtkEntry" id="entry7"/></child></object>)");
    EXPECT_NE(again.find("id=\"entry1\""), std::string::npos) << again;
    EXPECT_NE(again.find("<property name=\"mnemonic-widget\">entry1</property>"), std::string::npos); // follows it
}

TEST(UiEdit, DescribesHowAnObjectIsHeld) {
    auto doc = UiDocument::parse(R"(<interface><object class="GtkWindow"><child type="titlebar"><object class="GtkHeaderBar" id="h"><child type="start"><object class="GtkButton" id="b"/></child></object></child><property name="child"><object class="GtkLabel" id="l"/></property></object></interface>)");
    EXPECT_EQ(holderOf(*byId(doc, "h")), "child:titlebar");
    EXPECT_EQ(holderOf(*byId(doc, "l")), "property:child");
    EXPECT_EQ(holderOf(*doc.topLevelObjects()[0]), "");
    EXPECT_EQ(childObjectsOfType(*byId(doc, "h"), "start").size(), 1u);
    EXPECT_TRUE(childObjectsOfType(*byId(doc, "h"), "end").empty());
    EXPECT_TRUE(isWithin(*byId(doc, "b"), *doc.topLevelObjects()[0]));
    EXPECT_FALSE(isWithin(*byId(doc, "h"), *byId(doc, "b")));
}
