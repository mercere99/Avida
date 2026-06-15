#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module maintains a grid-based population.
 * 
 *  Neighbor layout:
 *    7 0 1
 *    6 X 2
 *    5 4 3
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class PopGrid : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  size_t width = 100;
  size_t height = 100;
  size_t num_cells = width * height;
  static constexpr size_t npos = static_cast<size_t>(-1);

  // emp::array<size_t, num_cells> org_grid{};
  emp::vector<size_t> org_grid;

  // === Helper Functions ===
  template <concepts::Organism ORG_T>
  void SetPos(ORG_T & org, size_t x, size_t y) {
    org.GetPhenotype().pop_pos = (x % width) + (y % height) * width;
  }

public:
  PopGrid(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "PopGrid", "PopManager", "Manage a grid-based population")
  { }
  ~PopGrid() { }

  void Serialize(emp::SerialPod & pod) {
    pod(org_grid);
  }

  // === Phenotypic Traits ===

  struct Phenotype {
    size_t pop_pos = npos;
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(pop_pos, "Position in the grid population of this organism.");
  }

  void RegisterSettings() {
    avida.AddSetting("grid.width", width, "Number of columns in population grid");
    avida.AddSetting("grid.height", height, "Number of rows in population grid");
  }

  size_t GetOrgReserveCount() const { return num_cells; }

  // === Signal Listeners ===
  void OnStart() {
    num_cells = width * height;
    org_grid.resize(num_cells, npos);
  }


  // When an offspring is ready, need to determine where to place it.
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    // Pick future position for offspring.
    const size_t parent_pos = parent.GetPhenotype().pop_pos;
    const size_t parent_x = parent_pos % width;
    const size_t parent_y = parent_pos / width;
    switch (avida.GetRandom().GetUInt(8)) {
      case 0: SetPos(offspring, parent_x,             parent_y - 1 + height); break;
      case 1: SetPos(offspring, parent_x + 1,         parent_y - 1 + height); break;
      case 2: SetPos(offspring, parent_x + 1,         parent_y             ); break;
      case 3: SetPos(offspring, parent_x + 1,         parent_y + 1         ); break;
      case 4: SetPos(offspring, parent_x,             parent_y + 1         ); break;
      case 5: SetPos(offspring, parent_x - 1 + width, parent_y + 1         ); break;
      case 6: SetPos(offspring, parent_x - 1 + width, parent_y             ); break;
      case 7: SetPos(offspring, parent_x - 1 + width, parent_y - 1 + height); break;
    }
  }

  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    size_t & org_pos = org.GetPhenotype().pop_pos;

    // If placed organism does not have a position, it is being injected; pick a random position
    if (org_pos == npos) org_pos = avida.GetRandom().GetUInt(num_cells);

    // See if we must delete an organism to make room for the new one.
    if (org_grid[org_pos] != npos) {
      avida.DeleteOrg(org_grid[org_pos]);
      org_grid[org_pos] = npos;
    }
  }

  template <concepts::Organism ORG_T>
  void OnPlacement([[maybe_unused]] ORG_T & org) {
    const size_t pop_pos = org.GetPhenotype().pop_pos;
    emp_assert(pop_pos != npos, "Orgs must know where to be placed.");
    emp_assert(org_grid[pop_pos] == npos, "Org must be placed into empty cells");

    org_grid[pop_pos] = org.GetBiotaID();
  }
};
