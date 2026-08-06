#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Optional, platform-independent population display capabilities supplied by modules.
 */

#include <cstddef>
#include <functional>
#include <utility>

#include "emp/base/assert.hpp"
#include "emp/base/vector.hpp"
#include "emp/tools/String.hpp"

template <typename AVIDA_T>
class PopulationViewOptions {
public:
  using organism_t = typename AVIDA_T::organism_t;
  using category_fun_t = std::function<size_t(const organism_t &)>;
  using value_fun_t = std::function<double(const organism_t &)>;
  using statistic_fun_t = std::function<emp::String()>;

  static constexpr size_t NO_CATEGORY = static_cast<size_t>(-1);

  struct CategoricalColorMode {
    emp::String id;
    emp::String label;
    emp::String description;
    category_fun_t get_category;
  };

  struct ContinuousColorMode {
    emp::String id;
    emp::String label;
    emp::String description;
    value_fun_t get_value;
  };

  struct Statistic {
    emp::String id;
    emp::String label;
    emp::String description;
    statistic_fun_t get_value;
  };

private:
  emp::vector<CategoricalColorMode> categorical_color_modes;
  emp::vector<ContinuousColorMode> continuous_color_modes;
  emp::vector<Statistic> statistics;

  void ValidateColorMode(const emp::String & id) const {
    emp_always_assert(id.size(), "Population color modes require a non-empty ID.");
    for (const auto & mode : categorical_color_modes) {
      emp_always_assert(mode.id != id, "Duplicate population color mode ID.", id);
    }
    for (const auto & mode : continuous_color_modes) {
      emp_always_assert(mode.id != id, "Duplicate population color mode ID.", id);
    }
  }

  void ValidateStatistic(const emp::String & id) const {
    emp_always_assert(id.size(), "Population statistics require a non-empty ID.");
    for (const auto & statistic : statistics) {
      emp_always_assert(statistic.id != id, "Duplicate population statistic ID.", id);
    }
  }

public:
  void AddCategoricalColorMode(emp::String id,
                               emp::String label,
                               emp::String description,
                               category_fun_t get_category) {
    ValidateColorMode(id);
    emp_always_assert(get_category, "Population color modes require a category function.");
    categorical_color_modes.push_back({
      .id = std::move(id),
      .label = std::move(label),
      .description = std::move(description),
      .get_category = std::move(get_category)
    });
  }

  void AddContinuousColorMode(emp::String id,
                              emp::String label,
                              emp::String description,
                              value_fun_t get_value) {
    ValidateColorMode(id);
    emp_always_assert(get_value, "Population color modes require a value function.");
    continuous_color_modes.push_back({
      .id = std::move(id),
      .label = std::move(label),
      .description = std::move(description),
      .get_value = std::move(get_value)
    });
  }

  void AddStatistic(emp::String id,
                    emp::String label,
                    emp::String description,
                    statistic_fun_t get_value) {
    ValidateStatistic(id);
    emp_always_assert(get_value, "Population statistics require a value function.");
    statistics.push_back({
      .id = std::move(id),
      .label = std::move(label),
      .description = std::move(description),
      .get_value = std::move(get_value)
    });
  }

  [[nodiscard]] const auto & GetCategoricalColorModes() const {
    return categorical_color_modes;
  }

  [[nodiscard]] const auto & GetContinuousColorModes() const {
    return continuous_color_modes;
  }

  [[nodiscard]] const auto & GetStatistics() const { return statistics; }
};
