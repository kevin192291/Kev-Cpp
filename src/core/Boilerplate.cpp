#include "core/Boilerplate.h"

#include <algorithm>

namespace kevc::boilerplate {

namespace {

// ---- SFML 3 ---------------------------------------------------------------------------------

const char* const kSfml3Window = R"(#include <SFML/Graphics.hpp>

#include <optional>

int main()
{
    // sf::RenderWindow is the window you can draw to (sf::Window alone has no clear() or draw()).
    sf::RenderWindow window(sf::VideoMode({800, 600}), "My Window");
    window.setFramerateLimit(60);

    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
        }

        window.clear(sf::Color::Black);
        // Draw here: window.draw(...);
        window.display();
    }

    return 0;
}
)";

const char* const kSfml3Game = R"(#include <SFML/Graphics.hpp>

#include <optional>

int main()
{
    sf::RenderWindow window(sf::VideoMode({800, 600}), "My Game");
    window.setFramerateLimit(60);

    sf::CircleShape player(30.f);
    player.setFillColor(sf::Color::Green);
    player.setPosition({370.f, 270.f});
    const float speed = 300.f; // pixels per second

    sf::Clock clock;
    while (window.isOpen())
    {
        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>())
            {
                window.close();
            }
            else if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Escape)
                {
                    window.close();
                }
            }
        }

        // The time since the last frame, so the speed is the same at any frame rate.
        const float dt = clock.restart().asSeconds();

        sf::Vector2f direction;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
        {
            direction.x -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
        {
            direction.x += 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up))
        {
            direction.y -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down))
        {
            direction.y += 1.f;
        }
        player.move(direction * speed * dt);

        window.clear(sf::Color::Black);
        window.draw(player);
        window.display();
    }

    return 0;
}
)";

const char* const kSfml3Sprite = R"(// A texture has to live as long as the sprites that show it.
sf::Texture texture;
if (!texture.loadFromFile("image.png"))
{
    return 1;
}
sf::Sprite sprite(texture);
sprite.setPosition({100.f, 100.f});
// In the loop, between clear() and display(): window.draw(sprite);
)";

const char* const kSfml3Text = R"(// A font has to live as long as the texts written in it.
sf::Font font;
if (!font.openFromFile("font.ttf"))
{
    return 1;
}
sf::Text text(font, "Hello", 24);
text.setFillColor(sf::Color::White);
text.setPosition({20.f, 20.f});
// In the loop, between clear() and display(): window.draw(text);
)";

// ---- SFML 2 ---------------------------------------------------------------------------------

const char* const kSfml2Window = R"(#include <SFML/Graphics.hpp>

int main()
{
    // sf::RenderWindow is the window you can draw to (sf::Window alone has no clear() or draw()).
    sf::RenderWindow window(sf::VideoMode(800, 600), "My Window");
    window.setFramerateLimit(60);

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }
        }

        window.clear(sf::Color::Black);
        // Draw here: window.draw(...);
        window.display();
    }

    return 0;
}
)";

const char* const kSfml2Game = R"(#include <SFML/Graphics.hpp>

int main()
{
    sf::RenderWindow window(sf::VideoMode(800, 600), "My Game");
    window.setFramerateLimit(60);

    sf::CircleShape player(30.f);
    player.setFillColor(sf::Color::Green);
    player.setPosition(370.f, 270.f);
    const float speed = 300.f; // pixels per second

    sf::Clock clock;
    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
            {
                window.close();
            }
            else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape)
            {
                window.close();
            }
        }

        // The time since the last frame, so the speed is the same at any frame rate.
        const float dt = clock.restart().asSeconds();

        sf::Vector2f direction;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Left))
        {
            direction.x -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Right))
        {
            direction.x += 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Up))
        {
            direction.y -= 1.f;
        }
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Down))
        {
            direction.y += 1.f;
        }
        player.move(direction * speed * dt);

        window.clear(sf::Color::Black);
        window.draw(player);
        window.display();
    }

    return 0;
}
)";

const char* const kSfml2Sprite = R"(// A texture has to live as long as the sprites that show it.
sf::Texture texture;
if (!texture.loadFromFile("image.png"))
{
    return 1;
}
sf::Sprite sprite(texture);
sprite.setPosition(100.f, 100.f);
// In the loop, between clear() and display(): window.draw(sprite);
)";

const char* const kSfml2Text = R"(// A font has to live as long as the texts written in it.
sf::Font font;
if (!font.loadFromFile("font.ttf"))
{
    return 1;
}
sf::Text text("Hello", font, 24);
text.setFillColor(sf::Color::White);
text.setPosition(20.f, 20.f);
// In the loop, between clear() and display(): window.draw(text);
)";

// The same in SFML 2 and 3.
const char* const kSfmlSound = R"(// A sound buffer has to live as long as the sounds that play it.
sf::SoundBuffer buffer;
if (!buffer.loadFromFile("sound.wav"))
{
    return 1;
}
sf::Sound sound(buffer);
sound.play();
)";

// ---- GTK 4 ----------------------------------------------------------------------------------

const char* const kGtkmmWindow = R"(#include <gtkmm.h>

class MainWindow : public Gtk::ApplicationWindow
{
public:
    MainWindow()
    {
        set_title("My Application");
        set_default_size(640, 480);

        button_.set_label("Click Me");
        button_.set_halign(Gtk::Align::CENTER);
        button_.set_valign(Gtk::Align::CENTER);
        button_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::onButtonClicked));
        set_child(button_);
    }

private:
    void onButtonClicked()
    {
        button_.set_label("Clicked");
    }

    Gtk::Button button_;
};

int main(int argc, char* argv[])
{
    auto app = Gtk::Application::create("com.example.myapp");
    return app->make_window_and_run<MainWindow>(argc, argv);
}
)";

const char* const kGtkmmButton = R"(auto* button = Gtk::make_managed<Gtk::Button>("Click Me");
button->signal_clicked().connect([] {
    // Runs when the button is clicked.
});
)";

const char* const kGtkmmDialog = R"(auto dialog = Gtk::AlertDialog::create("Something happened");
dialog->set_detail("More about it goes here.");
dialog->show(*this); // *this: the window it belongs to
)";

const char* const kGtkWindow = R"(#include <gtk/gtk.h>

static void on_button_clicked(GtkButton *button, gpointer user_data)
{
    (void)user_data;
    gtk_button_set_label(button, "Clicked");
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "My Application");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);

    GtkWidget *button = gtk_button_new_with_label("Click Me");
    gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);
    gtk_window_set_child(GTK_WINDOW(window), button);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char *argv[])
{
    GtkApplication *app = gtk_application_new("com.example.myapp", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
)";

const char* const kGtkBox = R"(GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
gtk_widget_set_margin_start(box, 12);
gtk_widget_set_margin_end(box, 12);
gtk_widget_set_margin_top(box, 12);
gtk_widget_set_margin_bottom(box, 12);

GtkWidget *label = gtk_label_new("Name:");
GtkWidget *entry = gtk_entry_new();
gtk_box_append(GTK_BOX(box), label);
gtk_box_append(GTK_BOX(box), entry);
// Then give the box to a window: gtk_window_set_child(GTK_WINDOW(window), box);
)";

std::vector<Snippet> makeAll() {
    std::vector<Snippet> out;
    auto sfml = [&](int major, const char* id, const char* title, bool program, const char* code,
                    const char* include, const char* component) {
        Snippet s;
        s.id = "sfml" + std::to_string(major) + "-" + id;
        s.library = "SFML " + std::to_string(major);
        s.title = title;
        s.program = program;
        s.code = code;
        if (!program) s.includes = {include};
        s.sfmlComponents = {component};
        s.sfmlMajor = major;
        out.push_back(std::move(s));
    };
    sfml(3, "window", "Window and Game Loop", true, kSfml3Window, "", "Graphics");
    sfml(3, "game", "Moving Shape with Keyboard and Frame Time", true, kSfml3Game, "", "Graphics");
    sfml(3, "sprite", "Sprite from an Image", false, kSfml3Sprite, "<SFML/Graphics.hpp>", "Graphics");
    sfml(3, "text", "Text in a Font", false, kSfml3Text, "<SFML/Graphics.hpp>", "Graphics");
    sfml(3, "sound", "Play a Sound", false, kSfmlSound, "<SFML/Audio.hpp>", "Audio");
    sfml(2, "window", "Window and Game Loop", true, kSfml2Window, "", "Graphics");
    sfml(2, "game", "Moving Shape with Keyboard and Frame Time", true, kSfml2Game, "", "Graphics");
    sfml(2, "sprite", "Sprite from an Image", false, kSfml2Sprite, "<SFML/Graphics.hpp>", "Graphics");
    sfml(2, "text", "Text in a Font", false, kSfml2Text, "<SFML/Graphics.hpp>", "Graphics");
    sfml(2, "sound", "Play a Sound", false, kSfmlSound, "<SFML/Audio.hpp>", "Audio");

    auto gtk = [&](const char* id, Language language, const char* title, bool program, const char* code) {
        const bool cpp = language == Language::Cpp;
        Snippet s;
        s.id = id;
        s.library = cpp ? "GTK 4 with gtkmm" : "GTK 4";
        s.title = title;
        s.language = language;
        s.program = program;
        s.code = code;
        if (!program) s.includes = {cpp ? "<gtkmm.h>" : "<gtk/gtk.h>"};
        s.pkgConfig = {cpp ? "gtkmm-4.0" : "gtk4"};
        out.push_back(std::move(s));
    };
    gtk("gtkmm-window", Language::Cpp, "Application and Window", true, kGtkmmWindow);
    gtk("gtkmm-button", Language::Cpp, "Button with a Click Handler", false, kGtkmmButton);
    gtk("gtkmm-dialog", Language::Cpp, "Message Dialog", false, kGtkmmDialog);
    gtk("gtk-window", Language::C, "Application and Window", true, kGtkWindow);
    gtk("gtk-box", Language::C, "Box with a Label and an Entry", false, kGtkBox);
    return out;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (true) {
        const size_t nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

bool blank(std::string_view line) {
    return line.find_first_not_of(" \t\r") == std::string_view::npos;
}

std::string_view indentOf(std::string_view line) {
    const size_t end = line.find_first_not_of(" \t");
    return line.substr(0, end == std::string_view::npos ? line.size() : end);
}

// TextEdit columns count UTF-16 code units.
int utf16Length(std::string_view line) {
    int n = 0;
    for (unsigned char c : line) {
        if ((c & 0xC0) == 0x80) continue; // a continuation byte
        n += c >= 0xF0 ? 2 : 1;
    }
    return n;
}

// The code with every line that has text indented, and no newline after the last.
std::string indented(std::string_view code, std::string_view indent) {
    std::string out;
    const auto lines = splitLines(code);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i + 1 == lines.size() && lines[i].empty()) break;
        if (i) out += '\n';
        if (!blank(lines[i])) out.append(indent).append(lines[i]);
    }
    return out;
}

} // namespace

const std::vector<Snippet>& all() {
    static const std::vector<Snippet> snippets = makeAll();
    return snippets;
}

std::vector<const Snippet*> offered(int sfmlMajor) {
    std::vector<const Snippet*> out;
    for (const Snippet& s : all())
        if (s.sfmlMajor == 0 || s.sfmlMajor == sfmlMajor) out.push_back(&s);
    return out;
}

const Snippet* find(std::string_view id) {
    for (const Snippet& s : all())
        if (s.id == id) return &s;
    return nullptr;
}

std::optional<Language> languageOf(const std::filesystem::path& file) {
    const std::string ext = file.extension().string();
    if (ext == ".c") return Language::C;
    for (const char* e : {".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h"})
        if (ext == e) return Language::Cpp;
    return std::nullopt;
}

const char* languageName(Language language) {
    return language == Language::Cpp ? "C++" : "C";
}

Insertion insertion(std::string_view text, int cursorLine, const Snippet& snippet) {
    Insertion result;
    const auto lines = splitLines(text);
    const int last = static_cast<int>(lines.size()) - 1;
    cursorLine = std::clamp(cursorLine, 0, last);

    if (snippet.program && text.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        result.edits.push_back({0, 0, last, utf16Length(lines[last]), snippet.code});
        return result;
    }

    // Includes first in the list: edits apply last first, and these are above the code.
    int includeLines = 0;
    std::string missing;
    for (const auto& include : snippet.includes) {
        const bool there = std::any_of(lines.begin(), lines.end(), [&](std::string_view line) {
            const size_t at = line.find("#include");
            return at != std::string_view::npos && blank(line.substr(0, at)) &&
                   line.find(include, at) != std::string_view::npos;
        });
        if (!there) missing += "#include " + include + "\n";
        includeLines += !there;
    }
    if (!missing.empty()) {
        int lastInclude = -1;
        for (int i = 0; i <= last; ++i) {
            const size_t at = lines[i].find("#include");
            if (at != std::string_view::npos && blank(lines[i].substr(0, at))) lastInclude = i;
        }
        if (lastInclude >= 0 && lastInclude < last) {
            result.edits.push_back({lastInclude + 1, 0, lastInclude + 1, 0, missing});
        } else if (lastInclude == last) { // the file ends in an #include without a newline
            missing.pop_back();
            const int end = utf16Length(lines[last]);
            result.edits.push_back({last, end, last, end, "\n" + missing});
        } else {
            // No includes yet: at the top, after #pragma once or a leading comment's lines if any.
            int at = 0;
            while (at < last && (lines[at].starts_with("//") || lines[at].starts_with("#pragma"))) ++at;
            result.edits.push_back({at, 0, at, 0, missing + (blank(lines[at]) ? "" : "\n")});
            includeLines += !blank(lines[at]);
        }
    }

    const std::string_view line = lines[cursorLine];
    if (blank(line)) {
        // A piece on a blank line with no indent of its own lines up with the code above it, one
        // step in when that opens a block.
        std::string indent(indentOf(line));
        if (indent.empty() && !snippet.program) {
            for (int i = cursorLine - 1; i >= 0; --i) {
                if (blank(lines[i])) continue;
                indent = indentOf(lines[i]);
                const size_t end = lines[i].find_last_not_of(" \t\r");
                if (lines[i][end] == '{') indent += "    ";
                break;
            }
        }
        if (snippet.program) indent.clear();
        result.edits.push_back({cursorLine, 0, cursorLine, utf16Length(line), indented(snippet.code, indent)});
        result.cursorLine = cursorLine;
    } else {
        const std::string_view indent = snippet.program ? std::string_view{} : indentOf(line);
        const int end = utf16Length(line);
        result.edits.push_back({cursorLine, end, cursorLine, end, "\n" + indented(snippet.code, indent)});
        result.cursorLine = cursorLine + 1;
    }
    // The includes went in above the code (they never go below the cursor's line unless the cursor
    // is among the includes; then the code still follows its own line).
    if (!result.edits.empty() && result.edits.front().startLine <= cursorLine && result.edits.size() > 1)
        result.cursorLine += includeLines;
    return result;
}

} // namespace kevc::boilerplate
