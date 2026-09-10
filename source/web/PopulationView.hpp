#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Definitions included after AvidaWebApp; the web application uses one translation unit.

void AvidaWebApp::CollectPopulationViewOptions() {
  population_view_options = PopulationViewOptions<avida_t>{};
  population_view_options.AddStatistic(
    "update", "Update", "Current population update.",
    [this](){ return emp::MakeString(Avida().GetUpdate()); }
  );
  population_view_options.AddStatistic(
    "organisms", "Organisms", "Number of living organisms.",
    [this](){ return emp::MakeString(Avida().GetNumOrgs()); }
  );
  Avida().TriggerSignal([this](auto & module) {
    if constexpr (requires { module.SetupPopulationView(population_view_options); }) {
      module.SetupPopulationView(population_view_options);
    }
  });
  continuous_color_ranges.clear();
  continuous_color_ranges.resize(
    population_view_options.GetContinuousColorModes().size()
  );
  categorical_color_maps.clear();
  categorical_color_maps.resize(
    population_view_options.GetCategoricalColorModes().size()
  );
}

void AvidaWebApp::RefreshReadouts() {
  for (auto & text : statistic_texts) text.Redraw();
  if (active_side_panel == SidePanel::ORGANISM) {
    UpdateActiveCellHighlight();
    org_stats_content.Redraw();
  }
}

emp::String AvidaWebApp::GetStatisticValue(size_t statistic_id) const {
  if (has_final_snapshot) {
    emp_assert(statistic_id < final_statistic_values.size());
    return final_statistic_values[statistic_id];
  }
  return population_view_options.GetStatistics()[statistic_id].get_value();
}

void AvidaWebApp::CaptureFinalView() {
  const auto & statistics = population_view_options.GetStatistics();
  final_statistic_values.clear();
  final_statistic_values.reserve(statistics.size());
  for (const auto & statistic : statistics) {
    final_statistic_values.push_back(statistic.get_value());
  }

  // Preserve the final colored population before Avida clears the biota and trait registry.
  DrawPopulation();
  final_color_legend_html = BuildPopulationColorLegendHTML();
  has_final_snapshot = true;
}

uint32_t AvidaWebApp::BlendColors(uint32_t first, uint32_t second, double amount) {
  const auto blend_channel = [amount](uint32_t a, uint32_t b, size_t shift) {
    const double first_channel = static_cast<double>((a >> shift) & 0xffU);
    const double second_channel = static_cast<double>((b >> shift) & 0xffU);
    return static_cast<uint8_t>(first_channel + (second_channel - first_channel) * amount);
  };
  return PackRGBA(
    blend_channel(first, second, 0),
    blend_channel(first, second, 8),
    blend_channel(first, second, 16)
  );
}

uint32_t AvidaWebApp::GetContinuousColor(double value,
                                                  double minimum,
                                                  double maximum) {
  if (!std::isfinite(value)) return DEFAULT_ORG_COLOR;
  const double normalized = maximum > minimum
    ? std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0)
    : 0.5;
  if (normalized < 0.5) {
    return BlendColors(LOW_FITNESS_COLOR, MID_FITNESS_COLOR, normalized * 2.0);
  }
  return BlendColors(MID_FITNESS_COLOR, HIGH_FITNESS_COLOR, normalized * 2.0 - 1.0);
}

uint32_t AvidaWebApp::MakeDistinctCategoryColor(size_t category, size_t attempt) {
  uint64_t value = static_cast<uint64_t>(category)
    + 0x9e3779b97f4a7c15ULL * static_cast<uint64_t>(attempt + 1);
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  value ^= value >> 31;

  const double hue = static_cast<double>(value & 0xffffU) / 65536.0 * 6.0;
  const double saturation = 0.58
    + static_cast<double>((value >> 16) & 0xffU) / 255.0 * 0.28;
  const double brightness = 0.78
    + static_cast<double>((value >> 24) & 0xffU) / 255.0 * 0.18;
  const double chroma = brightness * saturation;
  const double second = chroma * (1.0 - std::abs(std::fmod(hue, 2.0) - 1.0));
  const double match = brightness - chroma;
  const size_t sector = static_cast<size_t>(hue);
  const std::array<std::array<double, 3>, 6> channels{{
    {{chroma, second, 0.0}}, {{second, chroma, 0.0}}, {{0.0, chroma, second}},
    {{0.0, second, chroma}}, {{second, 0.0, chroma}}, {{chroma, 0.0, second}}
  }};
  const auto & color = channels[std::min(sector, channels.size() - 1)];
  return PackRGBA(
    static_cast<uint8_t>(std::round((color[0] + match) * 255.0)),
    static_cast<uint8_t>(std::round((color[1] + match) * 255.0)),
    static_cast<uint8_t>(std::round((color[2] + match) * 255.0))
  );
}

uint32_t AvidaWebApp::GetDistinctCategoryColor(size_t category) {
  if (active_color_mode >= categorical_color_maps.size()) return DEFAULT_ORG_COLOR;
  auto & color_map = categorical_color_maps[active_color_mode];
  if (const auto iterator = color_map.find(category); iterator != color_map.end()) {
    return iterator->second;
  }

  for (size_t attempt = 0; ; ++attempt) {
    const uint32_t candidate = MakeDistinctCategoryColor(category, attempt);
    const bool already_used = std::any_of(
      color_map.cbegin(), color_map.cend(),
      [candidate](const auto & entry) { return entry.second == candidate; }
    );
    if (!already_used) {
      color_map.emplace(category, candidate);
      return candidate;
    }
  }
}

uint32_t AvidaWebApp::GetCategoricalColor(const avida_t::organism_t & organism) {
  const auto & color_modes = population_view_options.GetCategoricalColorModes();
  if (active_color_mode >= color_modes.size()) return DEFAULT_ORG_COLOR;

  const size_t category = color_modes[active_color_mode].get_category(organism);
  if (category == PopulationViewOptions<avida_t>::NO_CATEGORY) return DEFAULT_ORG_COLOR;
  if (color_modes[active_color_mode].distinct_colors) {
    return GetDistinctCategoryColor(category);
  }
  return CATEGORY_COLORS[category % CATEGORY_COLORS.size()];
}

emp::String AvidaWebApp::ColorToCSS(uint32_t color) {
  return emp::MakeFormatted(
    "#{:02x}{:02x}{:02x}",
    color & 0xffU,
    (color >> 8) & 0xffU,
    (color >> 16) & 0xffU
  );
}

emp::String AvidaWebApp::FormatColorScaleValue(double value) {
  return std::isfinite(value) ? emp::MakeFormatted("{:.4g}", value) : "--";
}

emp::String AvidaWebApp::FormatLegendCount(size_t count) {
  return emp::MakeString(count, count == 1 ? " org" : " orgs");
}

emp::String AvidaWebApp::GetCategoricalLegendLabel(
  const avida_t::organism_t & organism,
  size_t category
) const {
  if (category == PopulationViewOptions<avida_t>::NO_CATEGORY) {
    return active_color_mode_id == "genotype" ? "Other genotypes" : "Other";
  }

  if (active_color_mode_id == "genotype") {
    return emp::MakeString("Genotype ", organism.GetPhenotype().genotype_id);
  }

  if (active_color_mode_id == "phenotype") {
    const auto & parent_counts = organism.GetPhenotype().parent_task_counts;
    emp::vector<emp::String> task_names;
    for (const auto & reaction : reaction_configs) {
      const size_t task_id = Avida().GetTaskID(reaction.task_name);
      if (task_id >= parent_counts.size() || parent_counts[task_id] == 0) continue;
      if (std::find(task_names.cbegin(), task_names.cend(), reaction.task_name)
          == task_names.cend()) {
        task_names.push_back(reaction.task_name);
      }
    }
    if (task_names.empty()) return "No tasks performed";

    emp::String label;
    for (size_t task_id = 0; task_id < task_names.size(); ++task_id) {
      if (task_id) label += ", ";
      label += task_names[task_id];
    }
    return label;
  }

  return emp::MakeString("Category ", category);
}

emp::String AvidaWebApp::BuildPopulationColorLegendHTML() {
  if (has_final_snapshot) return final_color_legend_html;

  emp::String out =
    "<section class='population-color-legend' aria-labelledby='color_legend_title'>"
    "<div class='color-legend-heading'><h3 id='color_legend_title'>Population colors</h3>";
  if (active_color_scale == ColorScale::BLANK) {
    out += "<span>Blank</span></div>"
      "<p class='color-legend-empty'>No organism colors are displayed.</p></section>";
    return out;
  }

  if (active_color_scale == ColorScale::CONTINUOUS) {
    const auto & modes = population_view_options.GetContinuousColorModes();
    if (active_color_mode >= modes.size()) return {};
    const auto & mode = modes[active_color_mode];
    const auto & range = continuous_color_ranges[active_color_mode];
    out += emp::MakeString(
      "<span>", emp::MakeWebSafe(mode.label), "</span></div><p class='color-legend-description'>",
      emp::MakeWebSafe(mode.description), "</p><div class='continuous-color-legend'>",
      "<div class='continuous-color-bar' style='--legend-low:",
      ColorToCSS(LOW_FITNESS_COLOR), ";--legend-mid:", ColorToCSS(MID_FITNESS_COLOR),
      ";--legend-high:", ColorToCSS(HIGH_FITNESS_COLOR),
      "' role='img' aria-label='Color scale from low to high'></div>"
      "<div class='continuous-color-values'><span>",
      FormatColorScaleValue(range.minimum), "</span><span>",
      FormatColorScaleValue((range.minimum + range.maximum) / 2.0), "</span><span>",
      FormatColorScaleValue(range.maximum), "</span></div></div>"
    );
    if (!std::isfinite(range.minimum) || !std::isfinite(range.maximum)) {
      out += "<p class='color-legend-empty'>Start the population to establish the scale.</p>";
    }
    out += "</section>";
    return out;
  }

  const auto & modes = population_view_options.GetCategoricalColorModes();
  if (active_color_mode >= modes.size()) return {};
  const auto & mode = modes[active_color_mode];
  out += emp::MakeString(
    "<span>", emp::MakeWebSafe(mode.label), "</span></div><p class='color-legend-description'>",
    emp::MakeWebSafe(mode.description), "</p>"
  );

  if (!run_started) {
    if (placed_organisms.empty()) {
      out += "<p class='color-legend-empty'>Start the population to see color groups.</p>";
    } else {
      out += emp::MakeString(
        "<ol class='categorical-color-list'><li><span class='color-swatch' style='background:",
        ColorToCSS(STAGED_ORG_COLOR), "'></span><span class='color-label'>Staged organisms</span>",
        "<span class='color-count'>", FormatLegendCount(placed_organisms.size()),
        "</span></li></ol>"
      );
    }
    out += "</section>";
    return out;
  }

  std::map<size_t, CategoricalLegendEntry> entries;
  const auto cells = Population().GetCells();
  for (const size_t organism_id : cells) {
    if (organism_id == avida_web::EMPTY_CELL || !Avida().IsOccupied(organism_id)) {
      continue;
    }
    const auto & organism = Avida().GetOrg(organism_id);
    const size_t category = mode.get_category(organism);
    auto [iterator, inserted] = entries.try_emplace(category);
    auto & entry = iterator->second;
    ++entry.count;
    if (inserted) {
      entry.category = category;
      entry.color = GetCategoricalColor(organism);
      entry.label = GetCategoricalLegendLabel(organism, category);
    }
  }

  emp::vector<CategoricalLegendEntry> ranked_entries;
  ranked_entries.reserve(entries.size());
  for (auto & [category, entry] : entries) ranked_entries.push_back(std::move(entry));
  std::sort(
    ranked_entries.begin(), ranked_entries.end(),
    [](const auto & first, const auto & second) {
      return first.count != second.count
        ? first.count > second.count
        : first.category < second.category;
    }
  );

  if (ranked_entries.empty()) {
    out += "<p class='color-legend-empty'>No organisms are currently in the population.</p>";
    out += "</section>";
    return out;
  }

  static constexpr size_t MAX_LEGEND_CATEGORIES = 8;
  const size_t shown_count = std::min(MAX_LEGEND_CATEGORIES, ranked_entries.size());
  out += "<ol class='categorical-color-list'>";
  for (size_t entry_id = 0; entry_id < shown_count; ++entry_id) {
    const auto & entry = ranked_entries[entry_id];
    out += emp::MakeString(
      "<li><span class='color-swatch' style='background:", ColorToCSS(entry.color),
      "'></span><span class='color-label'>", emp::MakeWebSafe(entry.label),
      "</span><span class='color-count'>", FormatLegendCount(entry.count), "</span></li>"
    );
  }
  out += "</ol>";
  if (shown_count < ranked_entries.size()) {
    size_t other_organism_count = 0;
    for (size_t entry_id = shown_count; entry_id < ranked_entries.size(); ++entry_id) {
      other_organism_count += ranked_entries[entry_id].count;
    }
    out += emp::MakeString(
      "<p class='color-legend-more'>+", ranked_entries.size() - shown_count,
      " more groups (", other_organism_count, " organisms)</p>"
    );
  }
  out += "</section>";
  return out;
}

void AvidaWebApp::DrawPopulation() {
  const size_t width = PopulationWidth();
  const size_t height = PopulationHeight();
  ResizePopulationDisplay(static_cast<int>(width), static_cast<int>(height));
  if (active_color_scale == ColorScale::BLANK) return;
  population_pixel_width = width;
  population_pixel_height = height;

  if (has_final_snapshot) {
    RenderPopulationPixels(
      population_pixels.data(),
      static_cast<int>(width),
      static_cast<int>(height)
    );
    UpdateActiveCellHighlight();
    population_color_legend.Redraw();
    return;
  }

  const auto cells = Population().GetCells();
  if (!run_started || cells.size() != width * height) {
    population_pixels.assign(width * height, EMPTY_COLOR);
    if (!run_started) {
      for (const auto & placement : placed_organisms) {
        if (placement.cell_id < population_pixels.size()) {
          population_pixels[placement.cell_id] = STAGED_ORG_COLOR;
        }
      }
    }
    continuous_values.clear();
    RenderPopulationPixels(
      population_pixels.data(), static_cast<int>(width), static_cast<int>(height)
    );
    UpdateActiveCellHighlight();
    last_grid_redraw_update = 0;
    population_color_legend.Redraw();
    return;
  }

  population_pixels.resize(cells.size());
  continuous_values.resize(cells.size(), std::numeric_limits<double>::quiet_NaN());

  const auto & continuous_modes = population_view_options.GetContinuousColorModes();
  if (active_color_scale == ColorScale::CONTINUOUS
      && active_color_mode < continuous_modes.size()) {
    auto & color_range = continuous_color_ranges[active_color_mode];
    for (size_t cell_id = 0; cell_id < cells.size(); ++cell_id) {
      const size_t org_id = cells[cell_id];
      if (org_id == avida_web::EMPTY_CELL || !Avida().IsOccupied(org_id)) continue;

      const double value = continuous_modes[active_color_mode].get_value(Avida().GetOrg(org_id));
      continuous_values[cell_id] = value;
      color_range.Include(value);
    }
  }

  for (size_t cell_id = 0; cell_id < cells.size(); ++cell_id) {
    const size_t org_id = cells[cell_id];
    if (org_id != avida_web::EMPTY_CELL && Avida().IsOccupied(org_id)) {
      if (active_color_scale == ColorScale::CATEGORICAL) {
        population_pixels[cell_id] = GetCategoricalColor(Avida().GetOrg(org_id));
      } else if (active_color_scale == ColorScale::CONTINUOUS) {
        const auto & color_range = continuous_color_ranges[active_color_mode];
        population_pixels[cell_id] = GetContinuousColor(
          continuous_values[cell_id], color_range.minimum, color_range.maximum
        );
      } else {
        population_pixels[cell_id] = DEFAULT_ORG_COLOR;
      }
    } else {
      // Deletions must clear an existing pixel; resize alone preserves its previous color.
      population_pixels[cell_id] = EMPTY_COLOR;
    }
  }

  RenderPopulationPixels(
    population_pixels.data(),
    static_cast<int>(width),
    static_cast<int>(height)
  );
  UpdateActiveCellHighlight();
  last_grid_redraw_update = Avida().GetUpdate();
  population_color_legend.Redraw();
}

void AvidaWebApp::SetupColorSelector() {
  color_selector = UI::Selector{"population_color_mode"};
  active_color_scale = ColorScale::BLANK;
  active_color_mode = 0;
  size_t selected_option = 0;
  bool selection_found = active_color_mode_id == "blank";
  color_selector.SetOption("Blank", [this]() {
    if (SimulationWorkerBusy()) return;
    active_color_scale = ColorScale::BLANK;
    active_color_mode = 0;
    active_color_mode_id = "blank";
    ClearPopulationDisplay();
    population_color_legend.Redraw();
  });

  const auto & categorical_modes = population_view_options.GetCategoricalColorModes();
  for (size_t mode_id = 0; mode_id < categorical_modes.size(); ++mode_id) {
    const auto & mode = categorical_modes[mode_id];
    color_selector.SetOption(mode.label, [this, mode_id]() {
      if (SimulationWorkerBusy()) return;
      active_color_scale = ColorScale::CATEGORICAL;
      active_color_mode = mode_id;
      active_color_mode_id = population_view_options.GetCategoricalColorModes()[mode_id].id;
      DrawPopulation();
    });
    if (mode.id == active_color_mode_id) {
      active_color_scale = ColorScale::CATEGORICAL;
      active_color_mode = mode_id;
      selected_option = mode_id + 1;
      selection_found = true;
    }
  }

  const auto & continuous_modes = population_view_options.GetContinuousColorModes();
  for (size_t mode_id = 0; mode_id < continuous_modes.size(); ++mode_id) {
    const auto & mode = continuous_modes[mode_id];
    color_selector.SetOption(mode.label, [this, mode_id]() {
      if (SimulationWorkerBusy()) return;
      active_color_scale = ColorScale::CONTINUOUS;
      active_color_mode = mode_id;
      active_color_mode_id = population_view_options.GetContinuousColorModes()[mode_id].id;
      DrawPopulation();
    });
    if (mode.id == active_color_mode_id) {
      active_color_scale = ColorScale::CONTINUOUS;
      active_color_mode = mode_id;
      selected_option = categorical_modes.size() + mode_id + 1;
      selection_found = true;
    }
  }

  if (!selection_found) {
    const auto phenotype = std::find_if(
      categorical_modes.cbegin(), categorical_modes.cend(),
      [](const auto & mode){ return mode.id == "phenotype"; }
    );
    if (phenotype != categorical_modes.cend()) {
      active_color_scale = ColorScale::CATEGORICAL;
      active_color_mode = static_cast<size_t>(phenotype - categorical_modes.cbegin());
      active_color_mode_id = "phenotype";
      selected_option = active_color_mode + 1;
    } else {
      active_color_scale = ColorScale::BLANK;
      active_color_mode = 0;
      active_color_mode_id = "blank";
      selected_option = 0;
    }
  }
  color_selector.SelectID(selected_option);
  if (active_color_scale == ColorScale::BLANK) ClearPopulationDisplay();
  color_selector.SetAttr("aria-label", "Population color mode");
  color_selector.SetTitle("Color organisms by");
}
