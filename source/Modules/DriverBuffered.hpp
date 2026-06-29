#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module buffers callbacks to be triggered on update boundaries, allowing organisms
 *  to be executed in order for better cache performance.
 *  - The scheduler still allocates based on metabolic rate, but using a binomial distribution.
 *  - A regular limit on the number of updates run is still used.
 */

#include <cstddef>    // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class DriverBuffered : public ModuleBase<AVIDA_T> {
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
  bool divide_pending = false;  // Set once the organism being processed has produced an offspring.

  void PrintStats(size_t ud) {
    std::cout << "UD:" << ud
              << "  PopSize:" << avida.GetNumOrgs()
              << "  Generation: " << avida.CalcTraitAve("generation")
              << "  Genome0:[" << avida.GetFirstOrg().GetGenomeSequence() << "]"
              << std::endl;
  }

  // Recompute every active organism's scheduling speed from its CURRENT metabolic rate, so a
  // bonus earned mid-life (e.g. by completing a task) immediately affects its share of CPU
  // cycles.  This gives direct, within-life selection on task performance instead of an effect
  // that only reaches the next generation via parent_bonus.
  void RecalcSpeeds() {
    total_speed = 0.0;
    for (double & s : speed_map) s = 0.0;  // Clear stale entries (e.g. now-empty slots).
    avida.GetBiota().ForEachOrg([this](auto & org) {
      const double rate = org.GetPhenotype().MetabolicRate(org.GetGenome().size());
      speed_map[org.GetBiotaID()] = rate;
      total_speed += rate;
    });
  }

public:
  DriverBuffered(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "DriverBuffered", "Execution",
        "Execute organisms in order based on metabolic rate (buffering callbacks).") { }
  ~DriverBuffered() { }

  void Serialize(emp::SerialPod & pod) {
    // All settings in the SettingsManager are automatically synced.
    emp_assert(pending_offspring.size() == 0);
    pod(speed_map, total_speed, cycles_executed);
  }

  // === Phenotypic Traits ===

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
    avida.AddCallback("DivideCell", [this](size_t biota_id) {
      auto & parent = avida.GetOrg(biota_id);
      auto genome = avida.GetOffspringGenome(parent);
      if (avida.TestOffspringGenome(parent, genome)) {
        pending_offspring.emplace_back(biota_id, std::move(genome));
        divide_pending = true;  // One birth per organism per update: end its turn (see OnUpdate).
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
    // Refresh speeds from current metabolic rates so a bonus earned mid-life takes effect now.
    RecalcSpeeds();

    // Execute all organisms for this update.
    const int32_t total_cycles = avida.GetNumOrgs() * ave_cycles_per_org;

    // Loop through organisms in order.
    size_t ud_cycles = 0;
    for (size_t org_id = 0; org_id < speed_map.size(); ++org_id) {
      if (speed_map[org_id] == 0.0) continue; // Nothing to execute.

      double p = speed_map[org_id] / total_speed;
      const size_t cur_cycles = std::max<size_t>(1, p*total_cycles+0.5);
      // avida.GetRandom().GetBinomial(total_cycles, p);

      // Run the organism, but stop as soon as it produces an offspring -- a single gestation
      // per update -- so one gestation's stats map cleanly to one buffered offspring.
      divide_pending = false;
      auto & hw = avida.GetOrg(org_id).Hardware();
      size_t cycle = 0;
      for (; cycle < cur_cycles && !divide_pending; ++cycle) hw.ProcessStep();
      ud_cycles += cycle;
    }
    cycles_executed += ud_cycles;

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

  template <concepts::Organism ORG_T>
  void OnPlacement(ORG_T & org) {
    // Lock in metabolic rate as organism speed.
    const size_t org_id = org.GetBiotaID();
    const double rate = org.GetPhenotype().MetabolicRate(org.GetGenome().size());

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
