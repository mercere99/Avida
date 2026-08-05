/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <emscripten.h>

#include "emp/base/vector.hpp"
#include "emp/web/Animate.hpp"
#include "emp/web/Button.hpp"
#include "emp/web/Canvas.hpp"
#include "emp/web/Div.hpp"
#include "emp/web/Document.hpp"
#include "emp/web/Image.hpp"
#include "emp/web/Selector.hpp"
#include "emp/web/Text.hpp"
#include "emp/web/emfunctions.hpp"

#include "core/Avida.hpp"
#include "core/PopulationViewOptions.hpp"

#include "Modules/DriverBuffered.hpp"
#include "Modules/EnvironmentLogic.hpp"
#include "Modules/EventManager.hpp"
#include "Modules/MutationsDivideSub.hpp"
#include "Modules/OrgTypeAvidian.hpp"
#include "Modules/PopGrid.hpp"
#include "Modules/ReactionsManager.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"
#include "Modules/TrackMetabolism.hpp"

namespace UI = emp::web;

using avida_t = Avida<
  OrgTypeAvidian,
  PopGrid,
  DriverBuffered,
  MutationsDivideSub,
  TrackGeneration,
  TrackGenotypes,
  EventManager,
  EnvironmentLogic,
  ReactionsManager,
  TrackMetabolism
>;

constexpr uint32_t PackRGBA(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint32_t>(red)
    | (static_cast<uint32_t>(green) << 8)
    | (static_cast<uint32_t>(blue) << 16)
    | (static_cast<uint32_t>(255) << 24);
}

EM_JS(void, RenderPopulationPixels,
      (const uint32_t * pixels, int width, int height), {
  const render = () => {
    const canvas = document.getElementById('population_canvas');
    if (!canvas) {
      requestAnimationFrame(render);
      return;
    }

    const byteLength = width * height * 4;
    const wasmPixels = new Uint8ClampedArray(HEAPU8.buffer, pixels, byteLength);
    const image = new ImageData(new Uint8ClampedArray(wasmPixels), width, height);
    const context = canvas.getContext('2d');
    context.imageSmoothingEnabled = false;
    context.putImageData(image, 0, 0);
  };
  render();
});

class AvidaWebApp {
private:
  enum class RunMode { PAUSED, PLAY, FAST_FORWARD };

  static constexpr double PLAY_INTERVAL_MS = 100.0;
  static constexpr double FAST_FORWARD_FRAME_BUDGET_MS = 12.0;
  static constexpr size_t FAST_FORWARD_REDRAW_UPDATES = 10;

  static constexpr uint32_t EMPTY_COLOR = PackRGBA(12, 30, 46);
  static constexpr uint32_t DEFAULT_ORG_COLOR = PackRGBA(110, 205, 224);
  static constexpr std::array<uint32_t, 12> CATEGORY_COLORS{
    PackRGBA(186, 84, 198),
    PackRGBA(73, 210, 218),
    PackRGBA(246, 211, 70),
    PackRGBA(239, 115, 91),
    PackRGBA(126, 105, 208),
    PackRGBA(74, 176, 124),
    PackRGBA(240, 153, 55),
    PackRGBA(86, 139, 219),
    PackRGBA(214, 91, 143),
    PackRGBA(152, 199, 74),
    PackRGBA(120, 215, 179),
    PackRGBA(192, 139, 225)
  };

  avida_t avida;
  PopulationViewOptions<avida_t> population_view_options;

  UI::Document document{"emp_base"};
  UI::Animate animation;
  UI::Button step_button;
  UI::Button play_button;
  UI::Button fast_forward_button;
  UI::Selector color_selector{"population_color_mode"};
  UI::Text update_text{"update_value"};
  UI::Text org_count_text{"organism_count_value"};
  UI::Text run_mode_text{"run_mode_value"};

  RunMode run_mode = RunMode::PAUSED;
  size_t active_color_mode = PopulationViewOptions<avida_t>::NO_CATEGORY;
  size_t last_grid_redraw_update = 0;
  double play_elapsed_ms = 0.0;
  emp::vector<uint32_t> population_pixels;

  [[nodiscard]] auto & Grid() { return avida.GetPlugIn<PopGrid>(); }

  void CollectPopulationViewOptions() {
    avida.TriggerSignal([this](auto & module) {
      if constexpr (requires { module.SetupPopulationView(population_view_options); }) {
        module.SetupPopulationView(population_view_options);
      }
    });
  }

  [[nodiscard]] emp::String GetRunModeLabel() const {
    if (avida.IsComplete()) return "Complete";
    switch (run_mode) {
      case RunMode::PAUSED: return "Paused";
      case RunMode::PLAY: return "Playing at up to 10 updates/second";
      case RunMode::FAST_FORWARD: return "Fast-forwarding";
    }
    return "Paused";
  }

  void RefreshReadouts() {
    update_text.Redraw();
    org_count_text.Redraw();
    run_mode_text.Redraw();
  }

  void UpdateControls() {
    const bool complete = avida.IsComplete();
    step_button.SetDisabled(complete || run_mode != RunMode::PAUSED);
    play_button.SetDisabled(complete);
    fast_forward_button.SetDisabled(complete);

    play_button.SetLabel(run_mode == RunMode::PLAY ? "Pause" : "Play");
    fast_forward_button.SetLabel(
      run_mode == RunMode::FAST_FORWARD ? "Pause" : "Fast-forward"
    );
    play_button.SetAttr("aria-pressed", run_mode == RunMode::PLAY ? "true" : "false");
    fast_forward_button.SetAttr(
      "aria-pressed",
      run_mode == RunMode::FAST_FORWARD ? "true" : "false"
    );
  }

  void SetRunMode(RunMode new_mode) {
    if (avida.IsComplete()) new_mode = RunMode::PAUSED;
    run_mode = new_mode;
    play_elapsed_ms = 0.0;

    if (run_mode == RunMode::PAUSED) {
      if (animation.GetActive()) animation.Stop();
    } else if (!animation.GetActive()) {
      animation.Start();
    }

    UpdateControls();
    RefreshReadouts();
  }

  [[nodiscard]] uint32_t GetOrganismColor(const avida_t::organism_t & organism) const {
    const auto & color_modes = population_view_options.GetCategoricalColorModes();
    if (active_color_mode >= color_modes.size()) return DEFAULT_ORG_COLOR;

    const size_t category = color_modes[active_color_mode].get_category(organism);
    if (category == PopulationViewOptions<avida_t>::NO_CATEGORY) return DEFAULT_ORG_COLOR;
    return CATEGORY_COLORS[category % CATEGORY_COLORS.size()];
  }

  void DrawPopulation() {
    const std::span<const size_t> cells = Grid().GetCells();
    population_pixels.resize(cells.size(), EMPTY_COLOR);

    for (size_t cell_id = 0; cell_id < cells.size(); ++cell_id) {
      const size_t org_id = cells[cell_id];
      if (org_id != PopGrid<avida_t>::EMPTY_CELL && avida.IsOccupied(org_id)) {
        population_pixels[cell_id] = GetOrganismColor(avida.GetOrg(org_id));
      }
    }

    RenderPopulationPixels(
      population_pixels.data(),
      static_cast<int>(Grid().GetWidth()),
      static_cast<int>(Grid().GetHeight())
    );
    last_grid_redraw_update = avida.GetUpdate();
  }

  void FinishUpdate(bool can_continue, bool redraw_population) {
    if (redraw_population) DrawPopulation();
    RefreshReadouts();
    if (!can_continue) SetRunMode(RunMode::PAUSED);
  }

  void StepPopulation() {
    emp_assert(run_mode == RunMode::PAUSED);
    FinishUpdate(avida.AdvanceUpdate(), true);
    UpdateControls();
  }

  void OnAnimationFrame(const UI::Animate & frame) {
    if (run_mode == RunMode::PLAY) {
      play_elapsed_ms += frame.GetStepTime();
      if (play_elapsed_ms < PLAY_INTERVAL_MS) return;

      play_elapsed_ms = 0.0;  // Do not catch up after a delayed or backgrounded frame.
      FinishUpdate(avida.AdvanceUpdate(), true);
      return;
    }

    if (run_mode != RunMode::FAST_FORWARD) return;

    const double frame_start = emp::GetTime();
    bool can_continue = true;
    do {
      can_continue = avida.AdvanceUpdate();
    } while (can_continue && emp::GetTime() - frame_start < FAST_FORWARD_FRAME_BUDGET_MS);

    const bool redraw_population = !can_continue
      || avida.GetUpdate() - last_grid_redraw_update >= FAST_FORWARD_REDRAW_UPDATES;
    FinishUpdate(can_continue, redraw_population);
  }

  void SetupColorSelector() {
    color_selector.SetOption("Uniform", [this]() {
      active_color_mode = PopulationViewOptions<avida_t>::NO_CATEGORY;
      DrawPopulation();
    });

    const auto & color_modes = population_view_options.GetCategoricalColorModes();
    for (size_t mode_id = 0; mode_id < color_modes.size(); ++mode_id) {
      color_selector.SetOption(color_modes[mode_id].label, [this, mode_id]() {
        active_color_mode = mode_id;
        DrawPopulation();
      });
    }

    if (color_modes.size()) {
      active_color_mode = 0;
      color_selector.SelectID(1);
    }
    color_selector.SetAttr("aria-label", "Population color mode");
  }

  void BuildInterface() {
    UI::Div app{"avida_app"};
    app.AddAttr("class", "avida-app");

    UI::Div header{"app_header"};
    header.AddAttr("class", "app-header");

    UI::Div brand{"brand"};
    brand.AddAttr("class", "brand");
    UI::Image logo{"assets/icons/LOGO.png", "avida_logo"};
    logo.Alt("Avida").AddAttr("class", "brand-logo");
    brand << logo
          << "<div><div class='brand-title'>Avida 5</div>"
             "<div class='brand-subtitle'>Digital Evolution Research Platform</div></div>";

    UI::Div tabs{"primary_tabs"};
    tabs.AddAttr("class", "primary-tabs");
    UI::Button population_tab{[](){}, "Population", "population_tab"};
    population_tab.AddAttr("class", "primary-tab is-active");
    population_tab.SetAttr("aria-current", "page");
    tabs << population_tab;

    UI::Div run_badge{"run_badge"};
    run_badge.AddAttr("class", "run-badge");
    run_badge << "<span class='run-dot'></span>" << run_mode_text;
    run_mode_text << UI::Live([this](){ return GetRunModeLabel(); });

    header << brand << tabs << run_badge;
    app << header;

    UI::Div workspace{"workspace"};
    workspace.AddAttr("class", "workspace");
    UI::Div population_card{"population_card"};
    population_card.AddAttr("class", "population-card");

    UI::Div population_header{"population_header"};
    population_header.AddAttr("class", "population-header");
    population_header
      << "<div><h1>Population</h1>"
         "<p>Each colored cell is one organism in the active grid.</p></div>";

    UI::Div view_controls{"view_controls"};
    view_controls.AddAttr("class", "view-controls");
    view_controls << "<label for='population_color_mode'>Color by</label>" << color_selector;
    population_header << view_controls;

    UI::Div canvas_frame{"canvas_frame"};
    canvas_frame.AddAttr("class", "canvas-frame");
    UI::Canvas population_canvas{
      static_cast<double>(Grid().GetWidth()),
      static_cast<double>(Grid().GetHeight()),
      "population_canvas"
    };
    population_canvas.AddAttr("class", "population-canvas");
    population_canvas.SetAttr("role", "img");
    population_canvas.SetAttr("aria-label", "Grid population of digital organisms");
    canvas_frame << population_canvas;

    population_card << population_header << canvas_frame;
    workspace << population_card;
    app << workspace;

    UI::Div transport{"transport"};
    transport.AddAttr("class", "transport");
    UI::Div transport_buttons{"transport_buttons"};
    transport_buttons.AddAttr("class", "transport-buttons");

    step_button = UI::Button([this](){ StepPopulation(); }, "Step", "step_button");
    play_button = UI::Button([this](){
      SetRunMode(run_mode == RunMode::PLAY ? RunMode::PAUSED : RunMode::PLAY);
    }, "Play", "play_button");
    fast_forward_button = UI::Button([this](){
      SetRunMode(
        run_mode == RunMode::FAST_FORWARD ? RunMode::PAUSED : RunMode::FAST_FORWARD
      );
    }, "Fast-forward", "fast_forward_button");

    step_button.AddAttr("class", "transport-button");
    play_button.AddAttr("class", "transport-button is-primary");
    fast_forward_button.AddAttr("class", "transport-button");
    step_button.SetAttr("aria-label", "Advance one population update");
    play_button.SetAttr("aria-label", "Play or pause at up to ten updates per second");
    fast_forward_button.SetAttr("aria-label", "Run as fast as possible or pause");
    transport_buttons << step_button << play_button << fast_forward_button;

    UI::Div readouts{"readouts"};
    readouts.AddAttr("class", "readouts");
    UI::Div update_readout{"update_readout"};
    update_readout.AddAttr("class", "readout");
    update_readout << "<span>Update</span>" << update_text;
    update_text << UI::Live([this](){ return avida.GetUpdate(); });

    UI::Div organism_readout{"organism_readout"};
    organism_readout.AddAttr("class", "readout");
    organism_readout << "<span>Organisms</span>" << org_count_text;
    org_count_text << UI::Live([this](){ return avida.GetNumOrgs(); });
    readouts << update_readout << organism_readout;

    transport << transport_buttons << readouts;
    app << transport;
    document << app;
  }

public:
  AvidaWebApp()
    : animation([this](const UI::Animate & frame){ OnAnimationFrame(frame); }) {
    avida.GetSettings().Set("base.config_dir", std::string{"/config"});
    avida.GetSettings().Set("base.data_dir", std::string{"/data"});
    CollectPopulationViewOptions();
  }

  void Initialize() {
    avida.InitializePaused();
    SetupColorSelector();
    BuildInterface();
    UpdateControls();
    DrawPopulation();
    RefreshReadouts();
  }
};

AvidaWebApp avida_web_app;

int emp_main() {
  avida_web_app.Initialize();
}
