#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  This module configures and runs an EC based on tournament selection.
 *  Handles its own population management.
 */

#include <cstddef>    // for size_t
#include <filesystem> // for path joining
#include <iostream>

#include "emp/base/vector.hpp"
#include "emp/data/DataOutput.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"

namespace fs = std::filesystem;

template <typename AVIDA_T>
class DriverTournament : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  emp::DataOutput output;
  size_t output_frequency = 100;

  size_t max_generations = 10000;             // How many updates should the run go for?
  size_t tourny_size = 7;
  size_t pop_size = 1000;
  emp::String fitness_name = "fitness";      // Which trait to determine winner of tournament?

  void PrintStats(size_t ud) {
    std::println("Generation: {} ; PopSize: {} ; Fitness0: {}\nGenome0:{}",
      ud, avida.GetNumOrgs(), avida.GetOrg(0).GetPhenotype().fitness,
      avida.GetFirstOrg().GetGenomeSequence()
    );
  }

public:
  DriverTournament(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("DriverTournament", "Execution", "Run an EA with Tournament Selection.")
    , avida(avida), output("Tournament.csv") { }
  ~DriverTournament() { }

  void Serialize(emp::SerialPod & /* pod */) {
    // Settings in the SettingsManager are automatically synced; nothing else to save.
  }

  void RegisterSettings() {
    avida.AddSetting("tourny.data_filename",
      [this](){ return output.GetFilename(); },
      [this](emp::String in){ output.SetFilename(in); },
      "File to output tournament data (placed in default data directory)");
    avida.AddSetting("tourny.output_frequency", output_frequency, "Updates between data outputs");
    avida.AddSetting("tourny.tourny_size", tourny_size, "How big should tournaments be?", 't');
    avida.AddSetting("tourny.pop_size", pop_size, "How big should populations be?", 'p');
    avida.AddSetting("tourny.max_generations", max_generations, "Maximum number of generations to run", 'G');
    avida.AddSetting("tourny.fitness_name", fitness_name, "Name of trait to use for fitness");
  }

  size_t GetOrgReserveCount() const { return pop_size * 2; }

  // === Signal Listeners ===

  void BeforeStart() {
    // If we have a filename, set up the date file columns.
    if (output.GetFilename().size()) {
      output.SetFilepath(avida.GetDataDir());
      output.AddColumn("Update", [this](){ return avida.GetUpdate(); });
      output.AddColumn("Max Fitness", [this](){ return avida.CalcTraitMax(fitness_name); });
      output.AddColumn("Average Fitness", [this](){ return avida.CalcTraitAve(fitness_name); });
    }
  }

  void OnStart() {
    PrintStats(0);  // Report initial state before any organisms run.
    output.DoOutput();
  }

  void OnUpdate(size_t /*update*/) {
    // Get the trait that we need for determining fitness.
    const auto & fit_trait = avida.GetTrait(fitness_name);

    // Collect the initial organisms in the population
    emp::vector<size_t> parent_ids = avida.GetActiveIDs();
    emp_assert(parent_ids.size() > 0);

    // Run Tournaments.
    emp::vector<size_t> tourny_ids(tourny_size); // Org IDs in a tournament.
    for (size_t tourny_round = 0; tourny_round < pop_size; ++tourny_round) {
      // Collect orgs for this tournament.
      for (size_t & id : tourny_ids) {
        id = parent_ids[avida.GetRandom().GetUInt(parent_ids.size())];
      }

      // Determine winner of this tournament.
      size_t best_id = tourny_ids[0];
      double best_fit = fit_trait.AsDouble(avida.GetOrg(tourny_ids[0]));

      for (size_t i = 1; i < tourny_size; ++i) {
        const double cur_fit = fit_trait.AsDouble(avida.GetOrg(tourny_ids[i]));
        if (best_fit < cur_fit) {
          best_id = tourny_ids[i];
          best_fit = cur_fit;
        }
      }

      avida.DivideOrg(best_id);  // Have the winner produce an offspring.
    }

    // Now that the offspring have been produced, remove the parents.
    for (size_t id : parent_ids) avida.DeleteOrg(id);
  }

  void OnUpdateEnd(size_t update) {
    if (update % output_frequency == 0) {
      PrintStats(update);
      output.DoOutput();
    }
    if (update >= max_generations) avida.Exit();
  }

};
