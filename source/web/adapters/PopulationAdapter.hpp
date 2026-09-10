#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <utility>

#include "Modules/PopGrid.hpp"
#include "Modules/PopWellMixed.hpp"
#include "SelectAdapter.hpp"

namespace avida_web {

inline constexpr size_t EMPTY_CELL = static_cast<size_t>(-1);

template <typename MODULE_T>
struct PopulationAdapter {};

template <typename AVIDA_T>
class PopulationAdapter<PopGrid<AVIDA_T>> {
  AVIDA_T & avida;
  auto & Module() const { return avida.template GetPlugIn<PopGrid>(); }

public:
  using supported_t = void;
  static constexpr const char * CHECKPOINT_PROFILE = "PopGrid-v1";
  static constexpr const char * CONFIG_PATH = "/config/Avida-web.cfg";
  static constexpr const char * DESCRIPTION = "Spatial grid population";
  static constexpr const char * PLACEMENT_HELP =
    "Drag organisms between the grid and freezer. Right-click an occupied cell for actions.";

  explicit PopulationAdapter(AVIDA_T & avida) : avida(avida) { }
  size_t GetWidth() const { return Module().GetWidth(); }
  size_t GetHeight() const { return Module().GetHeight(); }
  auto GetCells() const { return Module().GetCells(); }
  bool DeleteOrganismAt(size_t cell_id) { return Module().DeleteOrganismAt(cell_id); }

  bool InjectGenome(typename AVIDA_T::genome_t genome, size_t cell_id) {
    if (cell_id >= GetWidth() * GetHeight()) return false;
    auto & organism = avida.GetBiota().ReserveOrganism(std::move(genome));
    organism.GetPhenotype().pop_pos = cell_id;
    avida.Inject(organism);
    return true;
  }

  emp::String DescribeCell(size_t cell_id) const {
    return emp::MakeString("Cell ", cell_id % GetWidth(), ", ", cell_id / GetWidth());
  }
  emp::String DescribeInjection(size_t cell_id) const {
    return emp::MakeString("into cell ", cell_id);
  }
  static bool AffectsLayout(const emp::String & setting) {
    return setting == "grid.width" || setting == "grid.height";
  }
};

template <typename AVIDA_T>
class PopulationAdapter<PopWellMixed<AVIDA_T>> {
  AVIDA_T & avida;

  size_t SlotCount() const {
    // Avida reserves one extra slot for the incoming organism before replacement. Keep that
    // slot visible and the layout stable as the population fills, dies, and reuses slots.
    return avida.template GetPlugIn<PopWellMixed>().GetOrgReserveCount() + 1;
  }

public:
  using supported_t = void;
  static constexpr const char * CHECKPOINT_PROFILE = "PopWellMixed-v1";
  static constexpr const char * CONFIG_PATH = "/config/Avida-web-well-mixed.cfg";
  static constexpr const char * DESCRIPTION = "Well-mixed population — display slots are not positions";
  static constexpr const char * PLACEMENT_HELP =
    "Select any organism to inspect it. Drag from the freezer to add organisms. "
    "Display slots have no effect on placement or reproduction.";

  explicit PopulationAdapter(AVIDA_T & avida) : avida(avida) { }
  size_t GetWidth() const { return static_cast<size_t>(std::ceil(std::sqrt(SlotCount()))); }
  size_t GetHeight() const {
    const size_t slots = std::max(SlotCount(), avida.GetBiotaSize());
    return (slots + GetWidth() - 1) / GetWidth();
  }

  auto GetCells() const {
    // A lazy, random-access view avoids copying a population-sized index vector on redraw.
    // Capture the simulation itself: this adapter is a short-lived facade.
    return std::views::iota(size_t{0}, GetWidth() * GetHeight())
      | std::views::transform([&simulation = avida](size_t id) {
          return id < simulation.GetBiotaSize() && simulation.IsOccupied(id) ? id : EMPTY_CELL;
        });
  }

  bool DeleteOrganismAt(size_t cell_id) {
    if (cell_id >= avida.GetBiotaSize() || !avida.IsOccupied(cell_id)) return false;
    avida.DeleteOrg(cell_id);
    return true;
  }

  bool InjectGenome(typename AVIDA_T::genome_t genome, size_t cell_id) {
    if (cell_id >= GetWidth() * GetHeight()) return false;
    // Display coordinates must not influence well-mixed replacement or the simulation RNG.
    auto & organism = avida.GetBiota().ReserveOrganism(std::move(genome));
    avida.Inject(organism);
    return true;
  }

  emp::String DescribeCell(size_t cell_id) const {
    return emp::MakeString("Display slot ", cell_id, " (no spatial position)");
  }
  emp::String DescribeInjection(size_t) const { return "into the well-mixed population"; }
  static bool AffectsLayout(const emp::String & setting) { return setting == "pop.cap"; }
};

}
