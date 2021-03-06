#ifndef CRYSTALIZER_UI_HPP
#define CRYSTALIZER_UI_HPP

#include <gtkmm/grid.h>
#include "plugin_ui_base.hpp"

class CrystalizerUi : public Gtk::Grid, public PluginUiBase {
 public:
  CrystalizerUi(BaseObjectType* cobject,
                const Glib::RefPtr<Gtk::Builder>& builder,
                const std::string& settings_name);
  virtual ~CrystalizerUi();

 private:
  Gtk::Grid* bands_grid;

  Glib::RefPtr<Gtk::Adjustment> input_gain, output_gain;

  void build_bands(const int& nbands);
};

#endif
