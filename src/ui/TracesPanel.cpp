#include "ui/TracesPanel.h"

#include <iomanip>
#include <sstream>

namespace kevc {

namespace {

constexpr int kRowHeight = 22;
constexpr int kLabelWidth = 300;

std::string duration(double ms) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(ms < 10 ? 1 : 0) << ms << " ms";
    return s.str();
}

} // namespace

TracesPanel::TracesPanel() : Gtk::Paned(Gtk::Orientation::HORIZONTAL) {
    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    bar->set_margin(6);
    status_.set_xalign(0.0f);
    status_.set_hexpand(true);
    status_.set_ellipsize(Pango::EllipsizeMode::END);
    status_.add_css_class("dim-label");
    auto* clear = Gtk::make_managed<Gtk::Button>("Clear");
    clear->signal_clicked().connect([this] { clear_.emit(); });
    bar->append(status_);
    bar->append(*clear);
    left_.append(*bar);

    list_.set_selection_mode(Gtk::SelectionMode::SINGLE);
    list_.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (auto it = rowIds_.find(row); it != rowIds_.end()) select(it->second);
    });
    hint_.set_markup(
        "<span alpha=\"75%\">No traces yet. Kev-C++ collects SvelteKit's OpenTelemetry spans while it runs the app "
        "(Run or Debug). To send them: add <tt>tracing: { server: true }</tt> to your SvelteKit config "
        "(<tt>sveltekit({ … })</tt> in vite.config.ts on SvelteKit 3, svelte.config.js before), use an adapter such as "
        "<tt>@sveltejs/adapter-node</tt> (adapter-auto refuses instrumentation), and create "
        "<tt>src/instrumentation.server.ts</tt> that starts the OpenTelemetry <tt>NodeSDK</tt> with an "
        "<tt>OTLPTraceExporter</tt> from <tt>@opentelemetry/exporter-trace-otlp-http</tt>. It sends to the address "
        "Kev-C++ puts in <tt>OTEL_EXPORTER_OTLP_ENDPOINT</tt>.</span>");
    hint_.set_wrap(true);
    hint_.set_margin(12);
    hint_.set_xalign(0.0f);
    list_.set_placeholder(hint_);
    listScroller_.set_child(list_);
    listScroller_.set_vexpand(true);
    left_.append(listScroller_);
    left_.set_size_request(260, -1);

    canvas_.set_draw_func(sigc::mem_fun(*this, &TracesPanel::draw));
    canvas_.set_content_width(600);
    canvasScroller_.set_child(canvas_);
    set_start_child(left_);
    set_end_child(canvasScroller_);
    set_resize_start_child(false);
    set_position(380);
}

void TracesPanel::setStatus(const Glib::ustring& text) {
    status_.set_text(text);
}

void TracesPanel::setTraces(const TraceStore& store) {
    traces_ = store.traces();
    rowIds_.clear();
    while (auto* row = list_.get_row_at_index(0)) list_.remove(*row);
    Gtk::ListBoxRow* toSelect = nullptr;
    for (const auto& trace : store.traces()) {
        bool error = false;
        for (const auto& s : trace.spans) error = error || s.error;
        auto* label = Gtk::make_managed<Gtk::Label>();
        label->set_markup((error ? "<span foreground=\"#ff6b6b\">●</span> " : "") + Glib::Markup::escape_text(trace.title()) +
                          "  <span alpha=\"55%\">" + duration(trace.ms()) + " · " + std::to_string(trace.spans.size()) + " spans</span>");
        label->set_xalign(0.0f);
        label->set_ellipsize(Pango::EllipsizeMode::END);
        label->set_margin_start(6);
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_child(*label);
        list_.append(*row);
        rowIds_[row] = trace.id;
        if (trace.id == selected_) toSelect = row;
    }
    if (!toSelect && !store.traces().empty()) toSelect = list_.get_row_at_index(0);
    if (toSelect) {
        list_.select_row(*toSelect);
        select(rowIds_[toSelect]); // the selection may be the same row, which emits nothing
    } else {
        current_ = {};
        canvas_.queue_draw();
    }
}

void TracesPanel::select(const std::string& id) {
    selected_ = id;
    current_ = {};
    for (const auto& t : traces_)
        if (t.id == id) current_ = t;
    canvas_.queue_draw();
}

void TracesPanel::draw(const Cairo::RefPtr<Cairo::Context>& cr, int width, int) {
    const auto rows = waterfall(current_);
    canvas_.set_content_height(static_cast<int>(rows.size()) * kRowHeight + 30);
    auto layout = create_pango_layout("");
    const double barsLeft = kLabelWidth + 10;
    const double barsWidth = std::max(100.0, width - barsLeft - 90);
    // Time axis
    cr->set_source_rgba(0.64, 0.69, 0.72, 1.0);
    layout->set_text("0 ms");
    cr->move_to(barsLeft, 4);
    layout->show_in_cairo_context(cr);
    layout->set_text(duration(current_.ms()));
    int tw = 0, th = 0;
    layout->get_pixel_size(tw, th);
    cr->move_to(barsLeft + barsWidth - tw, 4);
    layout->show_in_cairo_context(cr);

    double y = 24;
    for (const auto& row : rows) {
        // Name, indented by depth
        cr->set_source_rgba(0.90, 0.92, 0.93, 1.0);
        layout->set_text(row.span->name);
        layout->set_width(static_cast<int>((kLabelWidth - row.depth * 14) * PANGO_SCALE));
        layout->set_ellipsize(Pango::EllipsizeMode::END);
        cr->move_to(6 + row.depth * 14, y + 3);
        layout->show_in_cairo_context(cr);
        layout->set_width(-1);
        // Bar
        const double x = barsLeft + row.offset * barsWidth;
        const double w = std::max(2.0, row.width * barsWidth);
        if (row.span->error) cr->set_source_rgba(0.85, 0.33, 0.33, 1.0);
        else cr->set_source_rgba(0.24 + 0.08 * (row.depth % 3), 0.43 + 0.06 * (row.depth % 3), 0.58, 1.0);
        cr->rectangle(x, y + 3, w, kRowHeight - 7);
        cr->fill();
        cr->set_source_rgba(0.64, 0.69, 0.72, 1.0);
        layout->set_text(duration(row.span->ms()));
        cr->move_to(x + w + 6, y + 3);
        layout->show_in_cairo_context(cr);
        y += kRowHeight;
    }
}

} // namespace kevc
