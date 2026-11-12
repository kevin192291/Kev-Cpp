---
order: 1
section: programs
description: A gtkmm 4 application whose window class has a label and a button with a click handler
file: main.cpp
whole-file: yes
ask: AppName = MyApp
packages: gtkmm-4.0
---
// A gtkmm 4 application with one window.
#include <gtkmm.h>

class {{AppName:pascal}}Window : public Gtk::Window
{
public:
    {{AppName:pascal}}Window()
    {
        set_title("{{AppName}}");
        set_default_size(800, 600);

        box_.set_margin(12);
        box_.append(label_);
        box_.append(button_);
        set_child(box_);

        label_.set_vexpand(true);
        button_.set_halign(Gtk::Align::CENTER);
        button_.signal_clicked().connect(sigc::mem_fun(*this, &{{AppName:pascal}}Window::on_button_clicked));
    }

private:
    void on_button_clicked()
    {
        label_.set_text("You clicked the button.");
        {{cursor}}
    }

    Gtk::Box box_{Gtk::Orientation::VERTICAL, 6};
    Gtk::Label label_{"Hello, GTK!"};
    Gtk::Button button_{"Click Me"};
};

int main(int argc, char* argv[])
{
    // The ID must be unique to this program: a second copy started with the same ID only activates the first.
    auto app = Gtk::Application::create("org.example.{{AppName:pascal}}");
    return app->make_window_and_run<{{AppName:pascal}}Window>(argc, argv);
}
