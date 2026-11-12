---
order: 2
section: programs
description: A gtkmm application with a menu bar, shortcuts, app actions (Quit, About) and window actions
file: main.cpp
whole-file: yes
ask: AppName = MyApp
packages: gtkmm-4.0
---
// A gtkmm application with a menu bar. Menu items name actions: "app.quit" runs one of the application's,
// "win.clear" one of the active window's. An action that's disabled greys out its menu items and shortcuts.
#include <gtkmm.h>

#include <memory>

class {{AppName:pascal}}Window : public Gtk::ApplicationWindow
{
public:
    {{AppName:pascal}}Window()
    {
        set_title("{{AppName}}");
        set_default_size(800, 600);
        set_show_menubar(true);

        add_action("clear", sigc::mem_fun(*this, &{{AppName:pascal}}Window::on_clear));
        // An action with a true/false state shows as a check item.
        wrap_ = add_action_bool("wrap", sigc::mem_fun(*this, &{{AppName:pascal}}Window::on_wrap), true);

        text_.set_wrap_mode(Gtk::WrapMode::WORD);
        scroller_.set_child(text_);
        set_child(scroller_);
    }

private:
    void on_clear()
    {
        text_.get_buffer()->set_text("");
        {{cursor}}
    }

    void on_wrap()
    {
        bool wrap = false;
        wrap_->get_state(wrap);
        wrap = !wrap;
        wrap_->change_state(wrap);
        text_.set_wrap_mode(wrap ? Gtk::WrapMode::WORD : Gtk::WrapMode::NONE);
    }

    Gtk::ScrolledWindow scroller_;
    Gtk::TextView text_;
    Glib::RefPtr<Gio::SimpleAction> wrap_;
};

class {{AppName:pascal}}App : public Gtk::Application
{
public:
    static Glib::RefPtr<{{AppName:pascal}}App> create()
    {
        return Glib::make_refptr_for_instance<{{AppName:pascal}}App>(new {{AppName:pascal}}App());
    }

protected:
    {{AppName:pascal}}App() : Gtk::Application("org.example.{{AppName:pascal}}") {}

    void on_startup() override
    {
        Gtk::Application::on_startup();

        add_action("quit", sigc::mem_fun(*this, &{{AppName:pascal}}App::on_quit));
        add_action("about", sigc::mem_fun(*this, &{{AppName:pascal}}App::on_about));
        set_accel_for_action("app.quit", "<Primary>q");
        set_accel_for_action("win.clear", "<Primary>l");

        // Sections are separated by lines; an underscore marks the letter Alt opens it with.
        auto fileMenu = Gio::Menu::create();
        auto editSection = Gio::Menu::create();
        editSection->append("_Clear", "win.clear");
        fileMenu->append_section(editSection);
        auto quitSection = Gio::Menu::create();
        quitSection->append("_Quit", "app.quit");
        fileMenu->append_section(quitSection);

        auto viewMenu = Gio::Menu::create();
        viewMenu->append("_Wrap Lines", "win.wrap");

        auto helpMenu = Gio::Menu::create();
        helpMenu->append("_About", "app.about");

        auto menuBar = Gio::Menu::create();
        menuBar->append_submenu("_File", fileMenu);
        menuBar->append_submenu("_View", viewMenu);
        menuBar->append_submenu("_Help", helpMenu);
        set_menubar(menuBar);
    }

    void on_activate() override
    {
        // Starting the program again while it runs activates this copy: show its window.
        if (auto* window = get_active_window())
        {
            window->present();
            return;
        }
        auto* window = new {{AppName:pascal}}Window();
        add_window(*window);
        window->signal_hide().connect([window] { delete window; });
        window->present();
    }

private:
    void on_quit()
    {
        // Hiding the windows deletes them (see on_activate); run() then returns.
        for (auto* window : get_windows())
        {
            window->set_visible(false);
        }
    }

    void on_about()
    {
        if (!about_)
        {
            about_ = std::make_unique<Gtk::AboutDialog>();
            about_->set_program_name("{{AppName}}");
            about_->set_version("1.0");
            about_->set_comments("Made with gtkmm.");
            about_->set_modal(true);
            about_->set_hide_on_close(true);
        }
        if (auto* window = get_active_window())
        {
            about_->set_transient_for(*window);
        }
        about_->present();
    }

    std::unique_ptr<Gtk::AboutDialog> about_;
};

int main(int argc, char* argv[])
{
    return {{AppName:pascal}}App::create()->run(argc, argv);
}
