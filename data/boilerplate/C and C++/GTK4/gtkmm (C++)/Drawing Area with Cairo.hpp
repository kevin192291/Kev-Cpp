---
order: 17
section: widgets
description: A Gtk::DrawingArea class that draws with Cairo and adds a dot wherever it's clicked
file: {{ClassName}}.hpp
whole-file: yes
ask: ClassName = Canvas
packages: gtkmm-4.0
---
// A widget that draws itself with Cairo. Clicks add dots; queue_draw() asks GTK to redraw it.
// Use it like any widget: a member {{ClassName}} canvas_; then set_child(canvas_) or box.append(canvas_).
#pragma once

#include <gtkmm.h>

#include <numbers>
#include <vector>

class {{ClassName}} : public Gtk::DrawingArea
{
public:
    {{ClassName}}()
    {
        set_content_width(400);
        set_content_height(300);
        set_expand(true);
        set_draw_func(sigc::mem_fun(*this, &{{ClassName}}::on_draw));

        auto click = Gtk::GestureClick::create();
        click->signal_pressed().connect(sigc::mem_fun(*this, &{{ClassName}}::on_pressed));
        add_controller(click);
    }

    void clear()
    {
        dots_.clear();
        queue_draw();
    }

private:
    // Called whenever GTK needs the picture; width and height are the widget's current size.
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int height)
    {
        cr->set_source_rgb(0.12, 0.12, 0.16);
        cr->paint();

        // A frame 10 px inside the edges; 0.5 puts 1 px lines on whole pixels so they're sharp.
        cr->set_source_rgb(0.5, 0.5, 0.6);
        cr->set_line_width(1.0);
        cr->rectangle(10.5, 10.5, width - 21.0, height - 21.0);
        cr->stroke();

        cr->set_source_rgb(0.35, 0.7, 1.0);
        for (const Dot& dot : dots_)
        {
            cr->arc(dot.x, dot.y, 8.0, 0.0, 2.0 * std::numbers::pi);
            cr->fill();
        }
        {{cursor}}
    }

    void on_pressed(int /*pressCount*/, double x, double y)
    {
        dots_.push_back({x, y});
        queue_draw();
    }

    struct Dot
    {
        double x;
        double y;
    };
    std::vector<Dot> dots_;
};
