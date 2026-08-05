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

  static constexpr size_t NO_CATEGORY = static_cast<size_t>(-1);

  struct CategoricalColorMode {
    emp::String id;
    emp::String label;
    emp::String description;
    category_fun_t get_category;
  };

private:
  emp::vector<CategoricalColorMode> categorical_color_modes;

public:
  void AddCategoricalColorMode(emp::String id,
                               emp::String label,
                               emp::String description,
                               category_fun_t get_category) {
    emp_always_assert(id.size(), "Population color modes require a non-empty ID.");
    emp_always_assert(get_category, "Population color modes require a category function.");
    for (const auto & mode : categorical_color_modes) {
      emp_always_assert(mode.id != id, "Duplicate population color mode ID.", id);
    }
    categorical_color_modes.push_back({
      .id = std::move(id),
      .label = std::move(label),
      .description = std::move(description),
      .get_category = std::move(get_category)
    });
  }

  [[nodiscard]] const auto & GetCategoricalColorModes() const {
    return categorical_color_modes;
  }
};
