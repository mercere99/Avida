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

#include <algorithm>
#include <cstddef>    // for size_t
#include <iostream>
#include <optional>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class DriverBuffered : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  size_t max_updates = 0;                                   // Update to end run (0 = no limit)
  std::filesystem::path ancestor_filename{"ancestor.org"};  // Ancestor genome filename
  bool inject_ancestor = true;                              // Seed the configured ancestor on start?

  // CPU Execution Management
  emp::vector<double> speed_map;                    // Relative speed of each virtual machine.
  double total_speed = 0.0;                         // Cumulative speed of all organisms.
  static constexpr int32_t ave_cycles_per_org = 30; // Average cycles to execute per org per update.
  int64_t cycles_executed = 0;                      // How many CPU cycles have been run so far?

  emp::vector<PendingOffspring<typename AVIDA_T::genome_t>> pending_offspring;
  bool divide_pending = false;  // Set once the organism being processed has produced an offspring.
  std::optional<typename AVIDA_T::genome_t> analysis_offspring;

  void PrintStats(size_t ud) {
    if (avida.GetNumOrgs() == 0) {
      std::cout << "UD:" << ud << "  PopSize:0" << std::endl;
      return;
    }
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
    if (speed_map.size() < avida.GetBiotaSize()) {
      speed_map.resize(avida.GetBiotaSize(), 0.0);
    }
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

  void SetInjectAncestor(bool value) { inject_ancestor = value; }

  void Serialize(emp::SerialPod & pod) {
    // All settings in the SettingsManager are automatically synced.
    emp_assert(pending_offspring.size() == 0);
    pod(speed_map, total_speed, cycles_executed);
  }

  [[nodiscard]] bool IsCheckpointSafe() const { return pending_offspring.empty(); }

  void AfterLoad() {
    pending_offspring.clear();
    divide_pending = false;
    analysis_offspring.reset();
    // Scheduling rates are a cache of active organism traits and are recomputed before every
    // update.  Rebuild them here as well so validation observes the same state that continuation
    // will use, rather than any harmless end-of-update drift from bulk placement.
    RecalcSpeeds();
  }

  [[nodiscard]] bool LoadedStateOK() const {
    if (!pending_offspring.empty()) return false;
    if (speed_map.size() < avida.GetBiotaSize()) return false;
    double expected_total = 0.0;
    for (size_t id = 0; id < speed_map.size(); ++id) {
      if (id < avida.GetBiotaSize() && avida.IsOccupied(id)) expected_total += speed_map[id];
      else if (speed_map[id] != 0.0) return false;
    }
    return expected_total == total_speed;
  }

  // === Phenotypic Traits ===

  void RegisterSettings() {
    avida.AddSetting(
      "base.max_updates", max_updates, "Maximum number of updates to run (0 = no limit)", 'U'
    );
    avida.GetSettings().Metadata("base.max_updates").AddTag("advanced");
    avida.AddSetting("base.ancestor_filename",
      [this](){ return ancestor_filename.string(); },
      [this](std::string s){ ancestor_filename = s; },
      "Filename for the initial ancestor.");
    avida.GetSettings().Metadata("base.ancestor_filename")
      .AddTag("startup only")
      .AddTag("local only");
  }

  void RegisterCallbacks() {
    // Buffer division requests; births are processed in bulk at end of each update.
    // Genome must be extracted immediately (AvidaVM heads are reset after this call).
    avida.AddCallback(
      "DivideCell",
      [this](size_t biota_id) {
#ifdef AVIDA_CHECKPOINT_DIAGNOSTICS
        emp_always_assert(
          avida.IsOccupied(biota_id),
          "DriverBuffered DivideCell callback received inactive organism ID ", biota_id,
          " at update ", avida.GetUpdate()
        );
#endif
        auto & parent = avida.GetOrg(biota_id);
        auto genome = avida.GetOffspringGenome(parent);
        if (avida.TestOffspringGenome(parent, genome)) {
          pending_offspring.emplace_back(biota_id, std::move(genome));
          divide_pending = true;  // One birth per organism per update: end its turn (see OnUpdate).
        }
      },
      [this](auto & hardware) {
        const size_t parent_size = hardware.GetGenome().size();
        auto genome = hardware.GetOffspringGenome();
        const double size_range =
          avida.GetSettings().template Get<double>("AvidaGP.offspring_size_range");
        if (genome.size() >= parent_size / size_range
            && genome.size() <= parent_size * size_range) {
          analysis_offspring = std::move(genome);
        }
      }
    );
  }

  void ClearAnalysisOffspring() { analysis_offspring.reset(); }

  [[nodiscard]] std::optional<typename AVIDA_T::genome_t> TakeAnalysisOffspring() {
    auto offspring = std::move(analysis_offspring);
    analysis_offspring.reset();
    return offspring;
  }

#ifdef AVIDA_CHECKPOINT_DIAGNOSTICS
  [[nodiscard]] bool CheckpointInjectAncestor() const { return inject_ancestor; }
  [[nodiscard]] int64_t CheckpointCyclesExecuted() const { return cycles_executed; }
  [[nodiscard]] size_t CheckpointSpeedMapSize() const { return speed_map.size(); }
  [[nodiscard]] double CheckpointTotalSpeed() const { return total_speed; }
  [[nodiscard]] double CheckpointActiveSpeedSum() const {
    double sum = 0.0;
    for (size_t id = 0; id < speed_map.size(); ++id) {
      if (id < avida.GetBiotaSize() && avida.IsOccupied(id)) sum += speed_map[id];
    }
    return sum;
  }
  [[nodiscard]] size_t CheckpointStaleSpeedCount() const {
    size_t count = 0;
    for (size_t id = 0; id < speed_map.size(); ++id) {
      if ((id >= avida.GetBiotaSize() || !avida.IsOccupied(id)) && speed_map[id] != 0.0) ++count;
    }
    return count;
  }
  [[nodiscard]] size_t CheckpointPendingOffspringCount() const {
    return pending_offspring.size();
  }
  [[nodiscard]] bool CheckpointDividePending() const { return divide_pending; }
  [[nodiscard]] bool CheckpointHasAnalysisOffspring() const {
    return analysis_offspring.has_value();
  }
#endif

  // === Signal Listeners ===

  void OnStart() {
    std::println("Random seed = {}", avida.GetRandom().GetSeed());
    if (inject_ancestor) avida.Inject(avida.GetSettings().GetConfigDir() / ancestor_filename);
  }

  void OnPopulationReady() {
    PrintStats(0);  // Report initial state before any organisms run.
  }

  void OnUpdate(size_t update) {
    RecalcSpeeds();
    const size_t target_cycles = avida.GetNumOrgs() * ave_cycles_per_org;
    size_t update_cycles = 0;
    for (size_t org_id = 0; org_id < speed_map.size(); ++org_id) {
      if (speed_map[org_id] == 0.0) continue;

      const double probability = speed_map[org_id] / total_speed;
      const size_t organism_cycles = std::max<size_t>(
        1, probability * target_cycles + 0.5
      );
      divide_pending = false;
#ifdef AVIDA_CHECKPOINT_DIAGNOSTICS
      emp_always_assert(
        avida.IsOccupied(org_id),
        "DriverBuffered scheduler selected inactive organism ID ", org_id,
        " at update ", update
      );
#endif
      auto & hardware = avida.GetOrg(org_id).Hardware();
      size_t cycle = 0;
      for (; cycle < organism_cycles && !divide_pending; ++cycle) hardware.ProcessStep();
      update_cycles += cycle;
    }
    cycles_executed += update_cycles;

    avida.AddOffspringSet(pending_offspring);
    if (update % 100 == 0) {
      std::println("ud_cycles = {}; target = {}; parents = {}",
        update_cycles, target_cycles, pending_offspring.size());
    }
    pending_offspring.clear();
  }

  void OnUpdateEnd(size_t update) {
    if (update % 100 == 0) PrintStats(update);
    if (max_updates && update >= max_updates) avida.Exit();
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
