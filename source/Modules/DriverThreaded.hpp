#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module executes organisms in parallel, buffering division callbacks to be processed
 *  in bulk at each update boundary for deterministic, reproducible results.
 *  - Cycle allocation is proportional to metabolic rate.
 *  - A regular limit on the number of updates run is still used.
 */

#include <algorithm>  // std::sort
#include <cstddef>    // size_t
#include <execution>  // std::execution::par
#include <iostream>
#include <mutex>      // std::mutex, std::lock_guard
#include <numeric>    // std::transform_reduce

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class DriverThreaded : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  size_t max_updates = 10000;                               // Update to end run
  std::filesystem::path ancestor_filename{"ancestor.org"};  // Ancestor genome filename

  // CPU Execution Management
  emp::vector<double> speed_map;                    // Relative speed of each virtual machine.
  double total_speed = 0.0;                         // Cumulative speed of all organisms.
  static constexpr int32_t ave_cycles_per_org = 30; // Average cycles to execute per org per update.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

  emp::vector<PendingOffspring<typename AVIDA_T::genome_t>> pending_offspring;
  // unique_ptr keeps DriverThreaded movable (std::mutex itself is not movable).
  std::unique_ptr<std::mutex> offspring_mutex = std::make_unique<std::mutex>();

  void PrintStats(size_t ud) {
    std::cout << "UD:" << ud
              << "  PopSize:" << avida.GetNumOrgs()
              << "  Generation: " << avida.GetAveTrait("generation")
              << "  Genome0:[" << avida.GetFirstOrg().GetGenomeSequence() << "]"
              << std::endl;
  }

public:
  DriverThreaded(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "DriverThreaded", "Execution",
        "Execute organisms in order based on metabolic rate (buffering callbacks).") { }


  void Serialize(emp::SerialPod & pod) {
    // All settings in the SettingsManager are automatically synced.
    emp_assert(pending_offspring.size() == 0);
    pod(speed_map, total_speed, cycles_executed);
  }

  // === Phenotypic Traits ===

  struct Phenotype {
    double metabolic_base = 1.0;
    double metabolic_mult = 1.0;
    double MetabolicBonus() { return metabolic_base * metabolic_mult; }
  };

  template <concepts::Organism ORG_T>
  static double CalcMetabolicRate(ORG_T & org) {
    return org.GetPhenotype().MetabolicBonus() * org.GetGenome().size();
  }

  void RegisterTraits() {
    // AVIDA_REQUIRE_TRAIT(size_t, generation);
    AVIDA_REGISTER_TRAIT(metabolic_base, "Relative base speed of the virtual CPU for this organism.");
    AVIDA_REGISTER_TRAIT(metabolic_mult, "Bonus speed multiple from tasks.");
  }

  void RegisterSettings() {
    avida.AddSetting("base.max_updates", max_updates, "Maximum number of updates to run", 'U');
    avida.AddSetting("base.ancestor_filename",
      [this](){ return ancestor_filename.string(); },
      [this](std::string s){ ancestor_filename = s; },
      "Filename for the initial ancestor.");
  }

  void RegisterCallbacks() {
    // Buffer division requests; births are processed in bulk at end of each update.
    // Genome must be extracted immediately (AvidaVM heads are reset after this call).
    // Mutex guards pending_offspring against concurrent appends during parallel execution.
    avida.AddCallback("DivideCell", [this](size_t biota_id){
      auto genome = avida.GetOffspringGenome(avida.GetOrg(biota_id));
      if (genome.size() > 0) {
        std::lock_guard lock(*offspring_mutex);
        pending_offspring.emplace_back(biota_id, std::move(genome));
      }
    });
  }

  // === Signal Listeners ===

  void OnStart() {
    std::println("Random seed = {}", avida.GetRandom().GetSeed());
    avida.Inject(avida.GetSettings().GetConfigDir() / ancestor_filename);
    PrintStats(0);  // Report initial state before any organisms run.
  }

  void OnUpdate(size_t update) {
    const int32_t total_cycles = avida.GetNumOrgs() * ave_cycles_per_org;

    // Execute all organisms in parallel; inactive slots have speed 0.0 and are skipped.
    const size_t ud_cycles = std::transform_reduce(
      std::execution::par,
      avida.GetOrgs().begin(), avida.GetOrgs().end(),
      size_t{0},
      std::plus<>{},
      [&](auto & org) -> size_t {
        const size_t org_id = org.GetBiotaID();
        if (speed_map[org_id] == 0.0) return 0;
        const double p = speed_map[org_id] / total_speed;
        const size_t cur_cycles = std::max<size_t>(1, p*total_cycles+0.5);
        avida.ProcessOrg(org_id, cur_cycles);
        return cur_cycles;
      }
    );
    cycles_executed += ud_cycles;

    // Sort by parent_id to make placement order deterministic across runs.
    std::sort(pending_offspring.begin(), pending_offspring.end(),
              [](const auto & a, const auto & b){ return a.parent_id < b.parent_id; });

    // Replicate all buffered parents (two-phase: build all offspring, then place all).
    avida.AddOffspringSet(pending_offspring);

    if (update % 100 == 0) {
      std::println("ud_cycles = {}; target = {}; parents = {}",
        ud_cycles, total_cycles, pending_offspring.size());
    }
    pending_offspring.clear();
  }

  void OnUpdateEnd(size_t update) {
    if (update % 100 == 0) PrintStats(update);
    if (update >= max_updates) avida.Exit();
  }

  // Recycled organism slots retain the previous occupant's phenotype, so reset our metabolic
  // traits to their defaults at birth.  ReactionsManager applies any task bonus afterward, in the
  // later OnOffspringReady phase.
  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().metabolic_base = 1.0;
    org.GetPhenotype().metabolic_mult = 1.0;
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & /*parent*/) {
    offspring.GetPhenotype().metabolic_base = 1.0;
    offspring.GetPhenotype().metabolic_mult = 1.0;
  }

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    // Lock in metabolic rate as organism speed.
    const size_t org_id = org.GetBiotaID();
    const double rate = CalcMetabolicRate(org);

    if (org_id >= speed_map.size()) speed_map.resize(org_id+1, 0.0);
    else total_speed -= speed_map[org_id];
    speed_map[org_id] = rate;
    total_speed += rate;
  }

  template <concepts::Organism ORG_T>
  void BeforeDeath(ORG_T & org) {
    total_speed -= speed_map[org.GetBiotaID()];
    speed_map[org.GetBiotaID()] = 0.0;  // Set old index speed to 0; don't shrink speed_map
  }
};
