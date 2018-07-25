#include <glibmm/main.h>
#include "limiter.hpp"
#include "util.hpp"

namespace {

void on_post_messages_changed(GSettings* settings, gchar* key, Limiter* l) {
    auto post = g_settings_get_boolean(settings, key);

    if (post) {
        if (!l->input_level_connection.connected()) {
            l->input_level_connection = Glib::signal_timeout().connect(
                [l]() {
                    float inL, inR;

                    g_object_get(l->limiter, "meter-inL", &inL, nullptr);
                    g_object_get(l->limiter, "meter-inR", &inR, nullptr);

                    std::array<double, 2> in_peak = {inL, inR};

                    l->input_level.emit(in_peak);

                    return true;
                },
                100);
        }

        if (!l->output_level_connection.connected()) {
            l->output_level_connection = Glib::signal_timeout().connect(
                [l]() {
                    float outL, outR;

                    g_object_get(l->limiter, "meter-outL", &outL, nullptr);
                    g_object_get(l->limiter, "meter-outR", &outR, nullptr);

                    std::array<double, 2> out_peak = {outL, outR};

                    l->output_level.emit(out_peak);

                    return true;
                },
                100);
        }

        if (!l->attenuation_connection.connected()) {
            l->attenuation_connection = Glib::signal_timeout().connect(
                [l]() {
                    float att;

                    g_object_get(l->limiter, "att", &att, nullptr);

                    l->attenuation.emit(att);

                    return true;
                },
                100);
        }

    } else {
        l->input_level_connection.disconnect();
        l->output_level_connection.disconnect();
        l->attenuation_connection.disconnect();
    }
}

void on_loudness_changed(GObject* gobject, GParamSpec* pspec, Limiter* l) {
    double loudness;

    g_object_get(l->ebur, "loudness", &loudness, nullptr);

    l->on_new_sample_peak(loudness);
}

}  // namespace

Limiter::Limiter(const std::string& tag, const std::string& schema)
    : PluginBase(tag, "limiter", schema) {
    limiter = gst_element_factory_make("calf-sourceforge-net-plugins-Limiter",
                                       nullptr);

    if (is_installed(limiter)) {
        auto audioconvert = gst_element_factory_make("audioconvert", nullptr);
        ebur = gst_element_factory_make("peebur", nullptr);

        gst_bin_add_many(GST_BIN(bin), audioconvert, limiter, ebur, nullptr);
        gst_element_link_many(audioconvert, limiter, ebur, nullptr);

        auto pad_sink = gst_element_get_static_pad(audioconvert, "sink");
        auto pad_src = gst_element_get_static_pad(ebur, "src");

        gst_element_add_pad(bin, gst_ghost_pad_new("sink", pad_sink));
        gst_element_add_pad(bin, gst_ghost_pad_new("src", pad_src));

        gst_object_unref(GST_OBJECT(pad_sink));
        gst_object_unref(GST_OBJECT(pad_src));

        g_object_set(limiter, "bypass", false, nullptr);

        bind_to_gsettings();

        g_signal_connect(settings, "changed::post-messages",
                         G_CALLBACK(on_post_messages_changed), this);

        g_signal_connect(ebur, "notify::loudness",
                         G_CALLBACK(on_loudness_changed), this);

        // useless write just to force callback call

        auto enable = g_settings_get_boolean(settings, "state");

        g_settings_set_boolean(settings, "state", enable);
    }
}

Limiter::~Limiter() {
    util::debug(log_tag + name + " destroyed");
}

void Limiter::bind_to_gsettings() {
    g_settings_bind_with_mapping(
        settings, "input-gain", limiter, "level-in", G_SETTINGS_BIND_DEFAULT,
        util::db20_gain_to_linear, util::linear_gain_to_db20, nullptr, nullptr);

    g_settings_bind_with_mapping(settings, "limit", limiter, "limit",
                                 G_SETTINGS_BIND_GET, util::db20_gain_to_linear,
                                 nullptr, nullptr, nullptr);

    // calf limiter does automatic makeup gain by the same amount given as
    // limit. See https://github.com/calf-studio-gear/calf/issues/162
    // that is why we reduce the output level accordingly

    g_settings_bind_with_mapping(settings, "limit", limiter, "level-out",
                                 G_SETTINGS_BIND_GET, util::db20_gain_to_linear,
                                 nullptr, nullptr, nullptr);

    g_settings_bind_with_mapping(settings, "lookahead", limiter, "attack",
                                 G_SETTINGS_BIND_GET, util::double_to_float,
                                 nullptr, nullptr, nullptr);

    g_settings_bind_with_mapping(settings, "release", limiter, "release",
                                 G_SETTINGS_BIND_GET, util::double_to_float,
                                 nullptr, nullptr, nullptr);

    g_settings_bind(settings, "asc", limiter, "asc", G_SETTINGS_BIND_DEFAULT);

    g_settings_bind_with_mapping(settings, "asc-level", limiter, "asc-coeff",
                                 G_SETTINGS_BIND_GET, util::double_to_float,
                                 nullptr, nullptr, nullptr);

    g_settings_bind(settings, "oversampling", limiter, "oversampling",
                    G_SETTINGS_BIND_DEFAULT);

    // ebur

    g_settings_bind(settings, "autovolume-state", ebur, "post-messages",
                    G_SETTINGS_BIND_DEFAULT);

    g_settings_bind_with_mapping(settings, "autovolume-window", ebur,
                                 "interval", G_SETTINGS_BIND_GET,
                                 util::ms_to_ns, nullptr, nullptr, nullptr);
}

void Limiter::on_new_sample_peak(const double& peak) {
    float gain;

    auto target = g_settings_get_int(settings, "autovolume-target");
    // auto tolerance = g_settings_get_int(settings, "autovolume-tolerance");

    g_object_get(limiter, "level-in", &gain, nullptr);

    gain = util::linear_to_db(gain);

    if (peak > target) {
        if (gain - 1 >= -36) {  // -36 = minimum input gain
            gain--;
        }
    } else if (peak < target) {
        if (gain + 1 <= 36) {  // 36 = maximum input gain
            gain++;
        }
    }

    gain = util::db_to_linear(gain);

    g_object_set(limiter, "level-in", gain, nullptr);
}
