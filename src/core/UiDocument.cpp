#include "core/UiDocument.h"

#include <algorithm>
#include <atomic>

namespace kevc::ui {

namespace {

std::atomic<int> nextUid{1};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool blank(std::string_view s) { return std::all_of(s.begin(), s.end(), isSpace); }
bool blankText(const Node& n) { return n.kind == Node::Kind::Text && blank(n.text); }

std::string normalized(std::string_view name) {
    std::string s(name);
    std::replace(s.begin(), s.end(), '_', '-');
    return s;
}

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// Entities a .ui file uses; anything else is kept as written.
std::string decode(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            out += raw[i];
            continue;
        }
        const size_t semi = raw.find(';', i);
        if (semi == std::string_view::npos || semi - i > 10) {
            out += raw[i];
            continue;
        }
        const std::string_view entity = raw.substr(i + 1, semi - i - 1);
        if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "amp") out += '&';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            try {
                appendUtf8(out, std::stoul(std::string(entity.substr(hex ? 2 : 1)), nullptr, hex ? 16 : 10));
            } catch (const std::exception&) {
                out.append(raw.substr(i, semi - i + 1));
            }
        } else {
            out.append(raw.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

std::unique_ptr<Node> textNode(std::string text) {
    auto n = std::make_unique<Node>();
    n->kind = Node::Kind::Text;
    n->uid = nextUid++;
    n->text = std::move(text);
    return n;
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::unique_ptr<Node> document() {
        auto doc = make(Node::Kind::Element, nullptr);
        content(*doc);
        doc->source = std::string(s_);
        return doc;
    }

private:
    std::string_view s_;
    size_t pos_ = 0;

    int lineAt(size_t at) const { return 1 + int(std::count(s_.begin(), s_.begin() + std::min(at, s_.size()), '\n')); }
    [[noreturn]] void fail(size_t at, const std::string& message) const { throw ParseError(lineAt(at), message); }

    std::unique_ptr<Node> make(Node::Kind kind, Node* parent) {
        auto n = std::make_unique<Node>();
        n->kind = kind;
        n->parent = parent;
        n->uid = nextUid++;
        n->changed = n->tagChanged = false;
        return n;
    }

    bool at(std::string_view what) const { return s_.substr(pos_, what.size()) == what; }

    size_t until(std::string_view end, size_t from, const char* what) const {
        const size_t found = s_.find(end, pos_);
        if (found == std::string_view::npos) fail(from, std::string("unterminated ") + what);
        return found + end.size();
    }

    void markup(Node& parent, Node::Kind kind, size_t end, size_t open, size_t close) {
        auto n = make(kind, &parent);
        n->source = std::string(s_.substr(pos_, end - pos_));
        n->text = kind == Node::Kind::Other ? n->source : std::string(s_.substr(pos_ + open, end - pos_ - open - close));
        pos_ = end;
        parent.children.push_back(std::move(n));
    }

    // Reads children into `parent` until its end tag (or the end of the text, for the document).
    void content(Node& parent) {
        while (pos_ < s_.size()) {
            if (s_[pos_] != '<') {
                const size_t end = std::min(s_.find('<', pos_), s_.size());
                auto n = make(Node::Kind::Text, &parent);
                n->source = std::string(s_.substr(pos_, end - pos_));
                n->text = decode(n->source);
                pos_ = end;
                parent.children.push_back(std::move(n));
            } else if (at("</")) {
                const size_t start = pos_;
                const size_t end = until(">", start, "end tag");
                std::string_view name = s_.substr(start + 2, end - start - 3);
                while (!name.empty() && isSpace(name.back())) name.remove_suffix(1);
                if (parent.name.empty()) fail(start, "</" + std::string(name) + "> without a start tag");
                if (name != parent.name)
                    fail(start, "</" + std::string(name) + "> where </" + parent.name + "> was expected");
                parent.closeTag = std::string(s_.substr(start, end - start));
                pos_ = end;
                return;
            } else if (at("<!--")) {
                markup(parent, Node::Kind::Comment, until("-->", pos_, "comment"), 4, 3);
            } else if (at("<![CDATA[")) {
                markup(parent, Node::Kind::CData, until("]]>", pos_, "CDATA section"), 9, 3);
            } else if (at("<?")) {
                markup(parent, Node::Kind::Other, until("?>", pos_, "processing instruction"), 0, 0);
            } else if (at("<!")) {
                const size_t bracket = s_.find('[', pos_), close = s_.find('>', pos_);
                const bool subset = bracket != std::string_view::npos && bracket < close;
                markup(parent, Node::Kind::Other, until(subset ? "]>" : ">", pos_, "declaration"), 0, 0);
            } else {
                element(parent);
            }
        }
        if (!parent.name.empty()) fail(s_.size(), "missing </" + parent.name + ">");
    }

    void element(Node& parent) {
        const size_t start = pos_++;
        auto n = make(Node::Kind::Element, &parent);
        const size_t nameStart = pos_;
        while (pos_ < s_.size() && !isSpace(s_[pos_]) && s_[pos_] != '>' && s_[pos_] != '/') ++pos_;
        n->name = std::string(s_.substr(nameStart, pos_ - nameStart));
        if (n->name.empty()) fail(start, "a '<' that starts no element");
        bool selfClosing = false;
        for (;;) {
            while (pos_ < s_.size() && isSpace(s_[pos_])) ++pos_;
            if (pos_ >= s_.size()) fail(start, "unterminated <" + n->name + ">");
            if (at("/>")) {
                pos_ += 2;
                selfClosing = true;
                break;
            }
            if (s_[pos_] == '>') {
                ++pos_;
                break;
            }
            const size_t keyStart = pos_;
            while (pos_ < s_.size() && !isSpace(s_[pos_]) && s_[pos_] != '=' && s_[pos_] != '>' && s_[pos_] != '/') ++pos_;
            std::string key(s_.substr(keyStart, pos_ - keyStart));
            while (pos_ < s_.size() && isSpace(s_[pos_])) ++pos_;
            if (key.empty() || pos_ >= s_.size() || s_[pos_] != '=') fail(pos_, "malformed attribute in <" + n->name + ">");
            ++pos_;
            while (pos_ < s_.size() && isSpace(s_[pos_])) ++pos_;
            if (pos_ >= s_.size() || (s_[pos_] != '"' && s_[pos_] != '\''))
                fail(pos_, "attribute " + key + " needs a quoted value");
            const char quote = s_[pos_++];
            const size_t end = s_.find(quote, pos_);
            if (end == std::string_view::npos) fail(keyStart, "unterminated value of " + key);
            n->attributes.emplace_back(std::move(key), decode(s_.substr(pos_, end - pos_)));
            pos_ = end + 1;
        }
        n->openTag = std::string(s_.substr(start, pos_ - start));
        if (!selfClosing) content(*n);
        n->source = std::string(s_.substr(start, pos_ - start));
        parent.children.push_back(std::move(n));
    }
};

void writeStartTag(const Node& n, std::string& out, bool selfClosing) {
    out += '<';
    out += n.name;
    for (const auto& [key, value] : n.attributes) {
        out += ' ';
        out += key;
        out += "=\"";
        out += escape(value, true);
        out += '"';
    }
    out += selfClosing ? "/>" : ">";
}

using Spans = std::map<int, std::pair<size_t, size_t>>;

void write(const Node& n, std::string& out, Spans* spans = nullptr);

void writeElement(const Node& n, std::string& out, Spans* spans);

void write(const Node& n, std::string& out, Spans* spans) {
    if (spans && n.isElement()) {
        const size_t begin = out.size();
        writeElement(n, out, spans);
        (*spans)[n.uid] = {begin, out.size()};
        return;
    }
    writeElement(n, out, spans);
}

void writeElement(const Node& n, std::string& out, Spans* spans) {
    switch (n.kind) {
    case Node::Kind::Text: out += n.changed ? escape(n.text) : n.source; return;
    case Node::Kind::Comment: out += n.changed ? "<!--" + n.text + "-->" : n.source; return;
    case Node::Kind::CData: out += n.changed ? "<![CDATA[" + n.text + "]]>" : n.source; return;
    case Node::Kind::Other: out += n.changed ? n.text : n.source; return;
    case Node::Kind::Element: break;
    }
    if (!n.changed && !n.tagChanged && !spans) {
        out += n.source;
        return;
    }
    if (n.name.empty()) { // the document
        for (const auto& c : n.children) write(*c, out, spans);
        return;
    }
    if (!n.changed && !n.tagChanged) { // spans wanted: the same text, element by element
        out += n.openTag;
        for (const auto& c : n.children) write(*c, out, spans);
        out += n.closeTag;
        return;
    }
    const bool wasEmptyTag = !n.openTag.empty() && n.closeTag.empty();
    if (n.children.empty()) {
        if (!n.tagChanged) out += n.openTag;
        else writeStartTag(n, out, n.closeTag.empty());
        if (!n.closeTag.empty()) out += n.closeTag;
        return;
    }
    if (n.tagChanged) {
        writeStartTag(n, out, false);
    } else if (wasEmptyTag) { // <x a="1"/> gained children: keep the attributes as written
        std::string_view open = n.openTag;
        open.remove_suffix(2);
        while (!open.empty() && isSpace(open.back())) open.remove_suffix(1);
        out += open;
        out += '>';
    } else {
        out += n.openTag;
    }
    for (const auto& c : n.children) write(*c, out, spans);
    out += n.closeTag.empty() ? "</" + n.name + ">" : n.closeTag;
}

// Everything in a node is new: written fresh, with fresh uids.
void markNew(Node& n) {
    n.uid = nextUid++;
    n.source.clear();
    n.openTag.clear();
    n.closeTag.clear();
    n.changed = n.tagChanged = true;
    for (auto& c : n.children) markNew(*c);
}

// Drops the line breaks and indentation between elements (not inside text).
void stripLayout(Node& n) {
    const bool hasText = std::any_of(n.children.begin(), n.children.end(), [](const auto& c) {
        return (c->kind == Node::Kind::Text && !blank(c->text)) || c->kind == Node::Kind::CData;
    });
    if (!hasText)
        std::erase_if(n.children, [](const auto& c) { return blankText(*c); });
    for (auto& c : n.children) stripLayout(*c);
}

// Lays out a new element's children one per line, if it holds elements (not text).
void indentNew(Node& n, const std::string& indent, const std::string& unit) {
    if (n.kind != Node::Kind::Element) return;
    const bool elementsOnly = std::all_of(n.children.begin(), n.children.end(), [](const auto& c) {
        return c->kind != Node::Kind::Text && c->kind != Node::Kind::CData ? true : blankText(*c);
    });
    const bool hasElements = std::any_of(n.children.begin(), n.children.end(), [](const auto& c) { return c->isElement(); });
    if (!elementsOnly || !hasElements) return;
    std::vector<std::unique_ptr<Node>> laidOut;
    for (auto& c : n.children) {
        if (blankText(*c)) continue;
        auto ws = textNode("\n" + indent + unit);
        ws->parent = &n;
        laidOut.push_back(std::move(ws));
        indentNew(*c, indent + unit, unit);
        laidOut.push_back(std::move(c));
    }
    auto ws = textNode("\n" + indent);
    ws->parent = &n;
    laidOut.push_back(std::move(ws));
    n.children = std::move(laidOut);
}

size_t indexIn(const Node& n) {
    const auto& siblings = n.parent->children;
    for (size_t i = 0; i < siblings.size(); ++i)
        if (siblings[i].get() == &n) return i;
    return siblings.size();
}

// The indentation of the line an element starts on, if it starts one.
std::optional<std::string> lineIndent(const Node& n) {
    if (!n.parent) return std::nullopt;
    const size_t i = indexIn(n);
    if (i == 0) return std::nullopt;
    const Node& before = *n.parent->children[i - 1];
    if (!blankText(before)) return std::nullopt;
    const size_t nl = before.text.rfind('\n');
    if (nl == std::string::npos) return std::nullopt;
    return before.text.substr(nl + 1);
}

void collectObjects(const Node& n, std::vector<Node*>& out) {
    for (const auto& c : n.children) {
        if (UiDocument::isObject(*c)) out.push_back(c.get());
        collectObjects(*c, out);
    }
}

Node* findUid(const Node& n, int uid) {
    for (const auto& c : n.children) {
        if (c->uid == uid) return c.get();
        if (Node* found = findUid(*c, uid)) return found;
    }
    return nullptr;
}

} // namespace

// ---- Node

std::string Node::attribute(std::string_view key) const {
    for (const auto& [k, v] : attributes)
        if (k == key) return v;
    return {};
}

bool Node::hasAttribute(std::string_view key) const {
    return std::any_of(attributes.begin(), attributes.end(), [&](const auto& a) { return a.first == key; });
}

std::vector<Node*> Node::childElements(std::string_view element) const {
    std::vector<Node*> out;
    for (const auto& c : children)
        if (c->isElement(element)) out.push_back(c.get());
    return out;
}

Node* Node::firstChild(std::string_view element) const {
    for (const auto& c : children)
        if (c->isElement(element)) return c.get();
    return nullptr;
}

std::string Node::innerText() const {
    std::string out;
    for (const auto& c : children)
        if (c->kind == Kind::Text || c->kind == Kind::CData) out += c->text;
    return out;
}

// ---- UiDocument

UiDocument::UiDocument() : root_(std::make_unique<Node>()) {}

UiDocument UiDocument::parse(std::string_view text) {
    UiDocument doc;
    doc.root_ = Parser(text).document();
    return doc;
}

std::string UiDocument::text() const {
    std::string out;
    write(*root_, out);
    return out;
}

std::string UiDocument::textOf(const Node& node) {
    std::string out;
    write(node, out);
    return out;
}

std::map<int, std::pair<size_t, size_t>> UiDocument::spans() const {
    std::string out;
    Spans spans;
    write(*root_, out, &spans);
    return spans;
}

Node* UiDocument::objectAt(size_t offset) const {
    const auto all = spans();
    Node* best = nullptr;
    size_t bestSize = std::string::npos;
    for (Node* o : objects()) {
        auto it = all.find(o->uid);
        if (it == all.end()) continue;
        const auto [begin, end] = it->second;
        if (offset >= begin && offset < end && end - begin < bestSize) {
            best = o;
            bestSize = end - begin;
        }
    }
    return best;
}

Node* UiDocument::interface() const { return root_->firstChild("interface"); }

Node* UiDocument::find(int uid) const { return findUid(*root_, uid); }

std::vector<Node*> UiDocument::objects() const {
    std::vector<Node*> out;
    collectObjects(*root_, out);
    return out;
}

std::vector<Node*> UiDocument::topLevelObjects() const {
    std::vector<Node*> out;
    if (Node* iface = interface())
        for (Node* c : iface->childElements())
            if (isObject(*c)) out.push_back(c);
    return out;
}

std::vector<Node*> UiDocument::childObjects(const Node& object) {
    std::vector<Node*> out;
    for (Node* holder : object.childElements())
        if (holder->name == "child" || holder->name == "property")
            for (Node* o : holder->childElements("object")) out.push_back(o);
    return out;
}

Node* UiDocument::parentObject(const Node& node) {
    for (Node* p = node.parent; p; p = p->parent)
        if (isObject(*p)) return p;
    return nullptr;
}

std::string UiDocument::className(const Node& object) {
    return object.attribute(object.name == "template" ? "parent" : "class");
}

std::string UiDocument::objectId(const Node& object) {
    return object.attribute(object.name == "template" ? "class" : "id");
}

Node* UiDocument::propertyNode(const Node& object, std::string_view name) {
    const std::string wanted = normalized(name);
    for (Node* p : object.childElements("property"))
        if (normalized(p->attribute("name")) == wanted) return p;
    return nullptr;
}

std::optional<std::string> UiDocument::property(const Node& object, std::string_view name) {
    if (Node* p = propertyNode(object, name)) return p->innerText();
    return std::nullopt;
}

std::vector<std::pair<std::string, std::string>> UiDocument::properties(const Node& object) {
    std::vector<std::pair<std::string, std::string>> out;
    for (Node* p : object.childElements("property"))
        if (p->childElements().empty()) out.emplace_back(p->attribute("name"), p->innerText());
    return out;
}

void UiDocument::setProperty(Node& object, std::string_view name, std::string_view value) {
    if (Node* p = propertyNode(object, name)) {
        setText(*p, value);
        return;
    }
    int index = 0, i = 0;
    for (Node* e : object.childElements()) {
        ++i;
        if (e->name == "property") index = i;
    }
    insertElement(object, "<property name=\"" + escape(name, true) + "\">" + escape(value) + "</property>", index);
}

void UiDocument::setText(Node& element, std::string_view text) {
    if (element.childElements().empty() && element.innerText() == text) return;
    element.children.clear();
    if (!text.empty()) {
        auto t = textNode(std::string(text));
        t->parent = &element;
        element.children.push_back(std::move(t));
    }
    touch(element);
}

void UiDocument::removeProperty(Node& object, std::string_view name) {
    if (Node* p = propertyNode(object, name)) removeElement(*p);
}

void UiDocument::setAttribute(Node& element, std::string_view key, std::string_view value) {
    auto& attrs = element.attributes;
    auto it = std::find_if(attrs.begin(), attrs.end(), [&](const auto& a) { return a.first == key; });
    if (value.empty()) {
        if (it == attrs.end()) return;
        attrs.erase(it);
    } else if (it != attrs.end()) {
        if (it->second == value) return;
        it->second = std::string(value);
    } else {
        attrs.emplace_back(std::string(key), std::string(value));
    }
    element.tagChanged = true;
    if (element.parent) touch(*element.parent);
}

std::string UiDocument::indentUnit() const {
    if (Node* iface = interface()) {
        const auto inner = iface->childElements();
        if (!inner.empty()) {
            const auto mine = lineIndent(*iface).value_or("");
            const auto theirs = lineIndent(*inner.front());
            if (theirs && theirs->size() > mine.size()) return theirs->substr(mine.size());
        }
    }
    return "  ";
}

Node* UiDocument::insertElement(Node& parent, std::string_view xml, int index) {
    auto fragment = Parser(xml).document();
    std::unique_ptr<Node> element;
    for (auto& c : fragment->children)
        if (c->isElement()) {
            element = std::move(c);
            break;
        }
    if (!element) throw ParseError(1, "no element to insert");
    markNew(*element);
    stripLayout(*element);
    element->parent = &parent;

    auto& kids = parent.children;
    std::vector<size_t> elements;
    for (size_t i = 0; i < kids.size(); ++i)
        if (kids[i]->isElement()) elements.push_back(i);
    // Laid out one per line unless the parent writes its elements on one line (<a><b/><c/></a>).
    const auto unit = indentUnit();
    const std::string parentIndent = lineIndent(parent).value_or("");
    std::optional<std::string> indent = elements.empty() ? std::optional(parentIndent + unit) : lineIndent(*kids[elements.front()]);
    const bool mixedText = std::any_of(kids.begin(), kids.end(), [](const auto& c) {
        return (c->kind == Node::Kind::Text && !blank(c->text)) || c->kind == Node::Kind::CData;
    });
    if (mixedText) indent.reset();
    if (indent) indentNew(*element, *indent, unit);

    auto ws = [&](const std::string& text) {
        auto t = textNode(text);
        t->parent = &parent;
        return t;
    };
    Node* inserted = element.get();
    if (elements.empty()) {
        if (indent) {
            kids.clear();
            kids.push_back(ws("\n" + *indent));
            kids.push_back(std::move(element));
            kids.push_back(ws("\n" + parentIndent));
        } else {
            kids.push_back(std::move(element));
        }
    } else if (index < 0 || size_t(index) >= elements.size()) {
        size_t at = elements.back() + 1;
        if (indent) kids.insert(kids.begin() + long(at++), ws("\n" + *indent));
        kids.insert(kids.begin() + long(at), std::move(element));
    } else {
        const size_t at = elements[size_t(index)];
        if (indent) kids.insert(kids.begin() + long(at), ws("\n" + *indent));
        kids.insert(kids.begin() + long(at), std::move(element));
    }
    touch(parent);
    return inserted;
}

void UiDocument::removeElement(Node& element) {
    Node* parent = element.parent;
    if (!parent) return;
    auto& kids = parent->children;
    size_t i = indexIn(element);
    if (i > 0 && blankText(*kids[i - 1])) {
        kids.erase(kids.begin() + long(i - 1));
        --i;
    }
    kids.erase(kids.begin() + long(i));
    if (std::all_of(kids.begin(), kids.end(), [](const auto& c) { return blankText(*c); })) kids.clear();
    touch(*parent);
}

std::vector<int> UiDocument::pathOf(const Node& node) {
    std::vector<int> path;
    for (const Node* n = &node; n->parent; n = n->parent) path.push_back(int(indexIn(*n)));
    std::reverse(path.begin(), path.end());
    return path;
}

Node* UiDocument::atPath(const std::vector<int>& path) const {
    Node* n = root_.get();
    for (int i : path) {
        if (i < 0 || size_t(i) >= n->children.size()) return nullptr;
        n = n->children[size_t(i)].get();
    }
    return n;
}

void UiDocument::touch(Node& node) {
    for (Node* n = &node; n; n = n->parent) n->changed = true;
}

std::string escape(std::string_view text, bool attribute) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"':
            if (attribute) out += "&quot;";
            else out += c;
            break;
        default: out += c;
        }
    }
    return out;
}

// ---- the preview

namespace {

class PreviewWriter {
public:
    PreviewWriter(const std::function<ClassKind(const std::string&)>& kindOf, Preview& preview)
        : kindOf_(kindOf), p_(preview) {}

    void interface(const Node& iface) {
        out_ += "<interface>";
        for (Node* c : iface.childElements()) {
            if (c->name == "requires") {
                if (c->attribute("lib") == "gtk") copy(*c);
            } else if (UiDocument::isObject(*c)) {
                topLevel(*c);
            } else {
                copy(*c);
            }
        }
        out_ += "</interface>";
        p_.xml = std::move(out_);
    }

private:
    const std::function<ClassKind(const std::string&)>& kindOf_;
    Preview& p_;
    std::string out_;
    std::string windowTitle_; // of the window being written, for its header bar
    std::map<std::string, ClassKind> kinds_;

    ClassKind kind(const std::string& cls) {
        if (cls.empty()) return ClassKind::Unknown;
        auto it = kinds_.find(cls);
        if (it == kinds_.end()) it = kinds_.emplace(cls, kindOf_(cls)).first;
        return it->second;
    }

    std::string idOf(const Node& o) {
        std::string id = UiDocument::objectId(o);
        if (id.empty()) id = "__kevc_" + std::to_string(o.uid);
        p_.ids[id] = o.uid;
        return id;
    }

    void topLevel(const Node& o) {
        const ClassKind k = kind(UiDocument::className(o));
        const std::string id = object(o, false);
        if (k == ClassKind::Window || k == ClassKind::Widget || k == ClassKind::Unknown) {
            PreviewRoot root;
            root.id = id;
            root.uid = o.uid;
            root.window = k == ClassKind::Window;
            if (root.window) {
                root.title = UiDocument::property(o, "title").value_or("");
                for (const Node* c : shownChildren(o))
                    if (isTitlebar(o, *c)) root.titlebar = true;
                auto number = [&](const char* name) {
                    try {
                        return std::stoi(UiDocument::property(o, name).value_or("0"));
                    } catch (const std::exception&) {
                        return 0;
                    }
                };
                root.width = std::max(0, number("default-width"));
                root.height = std::max(0, number("default-height"));
            }
            p_.roots.push_back(std::move(root));
        }
    }

    void startObject(const std::string& cls, const std::string& id) {
        out_ += "<object class=\"" + escape(cls, true) + "\" id=\"" + escape(id, true) + "\">";
    }

    static bool shown(ClassKind k) { return k == ClassKind::Widget || k == ClassKind::Window || k == ClassKind::Unknown; }

    // The objects a window or placeholder shows, titlebar first.
    std::vector<const Node*> shownChildren(const Node& o) {
        std::vector<const Node*> titlebars, others;
        for (Node* holder : o.childElements()) {
            const bool child = holder->name == "child";
            const std::string prop = normalized(holder->attribute("name"));
            if (!child && holder->name != "property") continue;
            const bool titlebar = child ? holder->attribute("type") == "titlebar" : prop == "titlebar";
            for (Node* c : holder->childElements("object"))
                if (shown(kind(UiDocument::className(*c)))) (titlebar ? titlebars : others).push_back(c);
        }
        titlebars.insert(titlebars.end(), others.begin(), others.end());
        return titlebars;
    }

    // Writes an object; returns its id. `fill`: it's a window's content, so it takes the window's height.
    std::string object(const Node& o, bool fill) {
        const std::string cls = UiDocument::className(o);
        const ClassKind k = kind(cls);
        const std::string id = idOf(o);
        if (k == ClassKind::Window) {
            const std::string savedTitle = windowTitle_;
            windowTitle_ = UiDocument::property(o, "title").value_or("");
            startObject("GtkBox", id);
            out_ += "<property name=\"orientation\">vertical</property><style><class name=\"background\"/></style>";
            const auto children = shownChildren(o);
            for (const Node* c : children) {
                const bool titlebar = c == children.front() && isTitlebar(o, *c);
                out_ += "<child>";
                object(*c, !titlebar);
                out_ += "</child>";
            }
            out_ += "</object>";
            windowTitle_ = savedTitle;
            return id;
        }
        if (k == ClassKind::Unknown) {
            startObject("GtkBox", id);
            out_ += "<property name=\"orientation\">vertical</property>";
            if (fill) out_ += "<property name=\"vexpand\">true</property>";
            out_ += "<style><class name=\"kevc-placeholder\"/></style><child>";
            const std::string label = "__kevc_label_" + std::to_string(o.uid);
            p_.ids[label] = o.uid;
            startObject("GtkLabel", label);
            out_ += "<property name=\"label\">" + escape(cls.empty() ? "(no class)" : cls) +
                    "</property><property name=\"xalign\">0</property><style><class name=\"kevc-placeholder-title\"/></style></object></child>";
            for (const Node* c : shownChildren(o)) {
                out_ += "<child>";
                object(*c, false);
                out_ += "</child>";
            }
            out_ += "</object>";
            return id;
        }

        startObject(cls, id);
        std::string actionName, actionTarget;
        for (const auto& c : o.children) {
            if (!c->isElement()) continue;
            const Node& e = *c;
            if (e.name == "signal" || e.name == "binding") continue;
            if (e.name == "property") {
                if (contains(e, "closure")) continue;
                const std::string name = normalized(e.attribute("name"));
                if (cls == "GtkSearchBar" && name == "search-mode-enabled") continue; // shown open, below
                if (name == "action-name") actionName = e.innerText();
                if (name == "action-target") actionTarget = e.innerText();
                const auto inner = e.childElements("object");
                const bool unknown = std::any_of(inner.begin(), inner.end(), [&](const Node* x) {
                    return kind(UiDocument::className(*x)) == ClassKind::Unknown;
                });
                // A placeholder is a widget: fine for a property that holds one, not for a model.
                if (unknown && !widgetSlot(name)) continue;
                if (inner.empty()) {
                    copy(e);
                } else {
                    writeStartTag(e, out_, false);
                    for (Node* inner : e.childElements("object")) object(*inner, false);
                    out_ += "</property>";
                }
            } else if (e.name == "child") {
                const auto inner = e.childElements("object");
                const bool controller = std::any_of(inner.begin(), inner.end(), [&](const Node* x) {
                    return kind(UiDocument::className(*x)) == ClassKind::Controller;
                });
                if (controller) continue;
                writeStartTag(e, out_, false);
                for (Node* x : e.childElements()) {
                    if (x->name == "object") object(*x, false);
                    else copy(*x);
                }
                out_ += "</child>";
            } else {
                copy(e);
            }
        }
        if (fill && !UiDocument::propertyNode(o, "vexpand")) out_ += "<property name=\"vexpand\">true</property>";
        // A search bar is closed until its window opens it; here it's open, so what's in it can be seen.
        if (cls == "GtkSearchBar") out_ += "<property name=\"search-mode-enabled\">true</property>";
        // A header bar shows its window's title unless it has a title widget, and here its window is the IDE.
        if (cls == "GtkHeaderBar" && !UiDocument::propertyNode(o, "title-widget")) {
            out_ += "<property name=\"title-widget\"><object class=\"GtkLabel\"><property name=\"label\">" +
                    escape(windowTitle_) + "</property><style><class name=\"title\"/></style></object></property>";
        }
        out_ += "</object>";
        if (const auto dot = actionName.find('.'); dot != std::string::npos && dot > 0)
            p_.actions.push_back({actionName.substr(0, dot), actionName.substr(dot + 1), actionTarget});
        return id;
    }

    static bool widgetSlot(const std::string& property) {
        static const char* const slots[] = {"child", "content", "titlebar", "title-widget", "label-widget",
                                            "start-child", "end-child", "center-widget", "start-widget",
                                            "end-widget", "popover", "default-widget", "focus-widget"};
        return std::find(std::begin(slots), std::end(slots), property) != std::end(slots);
    }

    static bool isTitlebar(const Node& window, const Node& c) {
        const Node* holder = c.parent;
        if (!holder || holder->parent != &window) return false;
        return holder->name == "child" ? holder->attribute("type") == "titlebar"
                                       : normalized(holder->attribute("name")) == "titlebar";
    }

    static bool contains(const Node& n, std::string_view element) {
        for (const auto& c : n.children)
            if (c->isElement(element) || contains(*c, element)) return true;
        return false;
    }

    void copy(const Node& n) {
        switch (n.kind) {
        case Node::Kind::Text: out_ += escape(n.text); return;
        case Node::Kind::CData: out_ += escape(n.text); return;
        case Node::Kind::Comment:
        case Node::Kind::Other: return;
        case Node::Kind::Element: break;
        }
        if (n.children.empty()) {
            writeStartTag(n, out_, true);
            return;
        }
        writeStartTag(n, out_, false);
        for (const auto& c : n.children) copy(*c);
        out_ += "</" + n.name + ">";
    }
};

} // namespace

Preview makePreview(const UiDocument& doc, const std::function<ClassKind(const std::string& className)>& kindOf) {
    Preview preview;
    if (Node* iface = doc.interface()) PreviewWriter(kindOf, preview).interface(*iface);
    return preview;
}

} // namespace kevc::ui
