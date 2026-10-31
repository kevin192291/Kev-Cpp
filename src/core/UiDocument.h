#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A GtkBuilder .ui file as the GTK designer edits it. The XML keeps the exact text it was read from,
// node by node, and writes unchanged nodes back byte for byte, so saving after an edit changes only
// the lines that were edited (and a file nobody edited is written back identical).
namespace kevc::ui {

struct ParseError : std::runtime_error {
    ParseError(int line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message), line(line) {}
    int line;
};

struct Node {
    enum class Kind { Element, Text, Comment, CData, Other }; // Other: <?xml …?>, <!DOCTYPE …>

    Kind kind = Kind::Element;
    std::string name;                                            // elements ("" for the document)
    std::vector<std::pair<std::string, std::string>> attributes; // decoded values, in order
    std::string text; // Text and CData: decoded; Comment: between the markers; Other: the whole markup
    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;
    int uid = 0; // unique in the process, never reused (selections refer to it)

    // What was read, to write unchanged parts back exactly (empty for nodes added later).
    std::string source;
    std::string openTag;
    std::string closeTag;   // empty when the element was written <like/this>
    bool changed = true;    // written out fresh: its own text, or (elements) from its children
    bool tagChanged = true; // its start tag is written fresh (attributes edited)

    bool isElement(std::string_view element = {}) const {
        return kind == Kind::Element && !name.empty() && (element.empty() || name == element);
    }
    std::string attribute(std::string_view key) const;
    bool hasAttribute(std::string_view key) const;
    std::vector<Node*> childElements(std::string_view element = {}) const;
    Node* firstChild(std::string_view element) const;
    // The text directly inside (for <property>…</property>).
    std::string innerText() const;
};

class UiDocument {
public:
    // Throws ParseError.
    static UiDocument parse(std::string_view text);

    UiDocument();
    UiDocument(UiDocument&&) noexcept = default;
    UiDocument& operator=(UiDocument&&) noexcept = default;

    std::string text() const;
    // One node's text, as text() would write it.
    static std::string textOf(const Node& node);
    // Where each element starts and ends in text(): uid → [begin, end).
    std::map<int, std::pair<size_t, size_t>> spans() const;
    // The innermost object whose element contains `offset` in text().
    Node* objectAt(size_t offset) const;
    Node& root() { return *root_; } // the document; its element child is <interface>
    const Node& root() const { return *root_; }
    Node* interface() const;
    Node* find(int uid) const;

    // GtkBuilder objects: <object> and <template> elements, in document order.
    std::vector<Node*> objects() const;
    // The objects directly inside <interface>.
    std::vector<Node*> topLevelObjects() const;
    // The objects an object holds: in its <child> elements, and object-valued properties
    // (<property name="titlebar"><object …>), in document order.
    static std::vector<Node*> childObjects(const Node& object);
    static Node* parentObject(const Node& node);
    static bool isObject(const Node& node) { return node.isElement("object") || node.isElement("template"); }
    // An object's class: <object class="GtkButton">, or a template's parent class.
    static std::string className(const Node& object);
    // Its id, or a template's class name.
    static std::string objectId(const Node& object);

    // Properties written in the file. Names match with - and _ treated the same, as GObject does.
    static Node* propertyNode(const Node& object, std::string_view name);
    static std::optional<std::string> property(const Node& object, std::string_view name);
    // name → value of the plain-text properties, in file order.
    static std::vector<std::pair<std::string, std::string>> properties(const Node& object);
    // Sets <property name="…">value</property>, adding it after the object's other properties.
    void setProperty(Node& object, std::string_view name, std::string_view value);
    void removeProperty(Node& object, std::string_view name);
    // Sets an attribute ("" removes it).
    void setAttribute(Node& element, std::string_view key, std::string_view value);

    // Inserts the element written in `xml` as the `index`th element inside `parent` (-1: last),
    // indented like the elements around it. Returns it.
    Node* insertElement(Node& parent, std::string_view xml, int index = -1);
    // Removes an element and the line break and indentation before it.
    void removeElement(Node& element);

    // Path of child indices from the root, to find "the same" node after the text is parsed again.
    static std::vector<int> pathOf(const Node& node);
    Node* atPath(const std::vector<int>& path) const;

    // Marks a node and everything around it as changed (after editing it directly).
    static void touch(Node& node);

private:
    std::unique_ptr<Node> root_;
    std::string indentUnit() const;
};

// ---- the preview

enum class ClassKind {
    Unknown,    // not a type in this process (a custom widget, libadwaita…): shown as a placeholder
    Window,     // GtkWindow and subclasses: shown as a box with its titlebar and child
    Widget,     // any other widget
    Controller, // event controllers: left out, so a shortcut in the design can't act on the IDE
    Other,      // other objects (adjustments, models, popovers): kept, never shown on their own
};

struct PreviewRoot {
    std::string id;  // in Preview::xml
    int uid = 0;     // the document's node
    bool window = false;
    bool titlebar = false; // a window with its own titlebar (a header bar)
    std::string title;
    int width = 0, height = 0; // a window's default size (0: natural)
};

struct PreviewAction {
    std::string group, name; // "win", "save"
    std::string target;      // the object's action-target, GVariant text ("" if none)
};

struct Preview {
    std::string xml;              // what GtkBuilder builds for the design view
    std::map<std::string, int> ids; // object id in `xml` → the document node it shows
    std::vector<PreviewRoot> roots; // top-level widgets and windows, in file order
    std::vector<PreviewAction> actions; // the actions buttons in the design refer to
};

// A copy of the document that GtkBuilder can build safely inside the IDE: signals, closures and
// event controllers removed, windows turned into boxes (a window can't sit inside another widget),
// templates into objects, unknown classes into labelled placeholders (their children still shown),
// header bars given a title of their own, and an id on every object so a clicked widget leads back
// to its node.
Preview makePreview(const UiDocument& doc, const std::function<ClassKind(const std::string& className)>& kindOf);

// Escapes text for XML content, or for an attribute value in double quotes.
std::string escape(std::string_view text, bool attribute = false);

} // namespace kevc::ui
