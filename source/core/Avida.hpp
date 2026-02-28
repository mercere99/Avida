#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  This is the main controller class for Avida.
 * 
 *  DEVELOPER notes:
 *  - Add module TrackGenotype
 *  - Add module PopGrid
 *  - Add module PrintGrid that prints grid every X updates AND requires PopGrid module
 *  - Add module EnvironmentLogic9 (it should register tasks and trigger OnTask as needed)
 *  - Add module OutputManager for printing to screen or files
 *  - Add module Reactions that listens for OnTask to provide rewards
 *  - Add module LimitedResources that listens for OnTask
 *  - Add module PopulationDemes
 *  - Add module PhylogenyTracker
 *  - Add module EventManager for dynamic events (population bottlenecks, environment changes, etc.)
 *
 *  - Improve command-line options; pass on to modules to deal with too.
 *  - Help should have an optional argument to get help for/from specific modules.
 *  - Make a master list of module TYPES with rules on how each type should be managed (e.g., one pop manager, any number of analysis modules)
 *  - Allow hardware variant to form special type of organism that can use any listed hardware.
 *  - Configuration options need to be, well, configurable
 *  - Test out alternative VM
 * 
 *  - Longer term: Simple language that compiles to Avida modules.
 */

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/bits/BitVector.hpp"
#include "emp/config/command_line.hpp"
#include "emp/datastructs/map_utils.hpp"
#include "emp/datastructs/UnorderedIndexMap.hpp"
#include "emp/meta/TypePack.hpp"

#include "../Hardware/AvidaVM.hpp"
#include "../Hardware/HardwareManager.hpp"

#include "concepts.hpp"
#include "ModuleBase.hpp"
#include "Phenotype.hpp"
#include "PlugInManager.hpp"

template <concepts::Hardware HW_T> struct hardware_filter { };

/// Main Avida-control object.
template <typename HARDWARE_T, template <typename> typename... PLUG_IN_Ts>
class Avida {
public:
  static_assert(sizeof...(PLUG_IN_Ts) > 0, "At least one Avida plug-in required to manage run.");
  using this_t = Avida<HARDWARE_T, PLUG_IN_Ts...>;
  using plug_in_pack_t = emp::TypePack<PLUG_IN_Ts<this_t>...>;

  using pheno_pack_t = plug_in_pack_t::template filter<pheno_member>::template wrap<pheno_member>;
  using phenotype_t = merge_from_TypePack<pheno_pack_t>::merged_t;
  using hardware_t = HARDWARE_T;                  // Type of general hardware for all organisms
  using hw_manager_t = HardwareManager<hardware_t>;  // Type of manager to recycle hardware
  using organism_t = Organism<hardware_t, phenotype_t>;        // Type for each individual organism
  using biota_t = emp::vector<organism_t>;        // Collection of all current organisms in Avida
  using genome_t = hardware_t::genome_t;          // Type of genomes used in organisms

private:
  PlugInManager<this_t, PLUG_IN_Ts<this_t>...> plug_ins;
  hw_manager_t hw_manager;
  TraitManager<this_t> trait_man;

  size_t update = 0;          // Times update was run on this population
  emp::Random random;         // Central random number generator
  biota_t biota{};            // Collection of all current organisms
  emp::BitVector occupied{};  // Which organisms in biota are active?
  size_t org_count = 0;       // Current number of active organisms

  enum class RunState { INITIALIZING, RUNNING, PAUSED, EXITING, ERROR };
  RunState run_state = RunState::INITIALIZING;

  // ===== Helper Functions =====
  void PlaceOrganism(organism_t && org_to_place) {
    emp_assert(occupied.size() == biota.size());
    emp_assert(occupied.CountOnes() == org_count);

    plug_ins.BeforePlacement(org_to_place);

    size_t index = occupied.FindZero();    // Find empty position in biota.
    if (index == emp::BitVector::npos) {   // If no empty position available, add one.
      index = occupied.PushBack(true);
      biota.emplace_back(std::move(org_to_place));
    } else {
      occupied.Set(index);
      biota[index] = std::move(org_to_place);
    }

    ++org_count;
    organism_t & placed_org = biota[index];  // org_to_place was moved out; grab new location.
    placed_org.SetPosition(index);
    plug_ins.OnPlacement(placed_org);
  }

public:
  Avida() : plug_ins(*this) { plug_ins.RegisterTraits(); }
  Avida(emp::vector<emp::String> args) : plug_ins(*this) {
    plug_ins.RegisterTraits();
    for (size_t i = 1; i < args.size(); ++i) {
      if (args[i] == "-h" || args[i] == "--help") { PrintHelp(args[0], std::cout); }
      else if (args[i] == "-s" || args[i] == "--seed") {
        // Collect the seed.
        ++i;
        if (i >= args.size()) { emp::notify::Error("Must specify a random number seed."); }
        if (!args[i].OnlyDigits()) { emp::notify::Error("Random seed must be set to a whole number."); }
        random.ResetSeed(args[i].ConvertFromLiteral<uint64_t>());
      }
      else {
        emp::notify::Error("Unknown argument: ", args[i]);
      }
    }
  }
  ~Avida() { Exit(); }

  // === Basic Accessors ===

  [[nodiscard]] size_t GetUpdate() const { return update; }
  [[nodiscard]] emp::Random & GetRandom() { return random; }
  [[nodiscard]] auto & GetOrg(this auto & self, size_t id) {
    emp_assert(self.IsOccupied(id));
    return self.biota[id];
  }
  [[nodiscard]] auto & GetFirstOrg(this auto & self) {
    emp_assert(self.GetNumOrgs() > 0);
    return self.biota[self.occupied.FindOne()];
  }
  [[nodiscard]] bool IsOccupied(size_t id) const { return id < occupied.size() && occupied[id]; }
  [[nodiscard]] int32_t GetNumOrgs() const { return org_count; }

  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] double GetAveTrait(const FUN_T & fun) const {
    double total = 0.0;    
    for (size_t index : occupied) total += fun(biota[index]);
    return total / GetNumOrgs();
  }

  [[nodiscard]] double GetAveTrait(const emp::String & name) const {
    const auto & trait = trait_man.Get(name);
    return GetAveTrait([&trait](const organism_t & org){ return trait.AsDouble(org.GetPhenotype()); });
  }

  // Get a plug-in by realized type.
  template <typename PLUG_IN_T>
  [[nodiscard]] auto & GetPlugIn() { return plug_ins.template Get<PLUG_IN_T>(); }

  // Get a plug-in by template type.
  template <template <typename> typename PLUG_IN_T>
  [[nodiscard]] auto & GetPlugIn() { return plug_ins.template Get<PLUG_IN_T>(); }

  // Get a plug-in by position.
  template <size_t INDEX>
  [[nodiscard]] auto & GetPlugIn() { return plug_ins.template Get<INDEX>(); }

  void PrintHelp(const emp::String & exec_name, std::ostream & os) {
    os << "Format: " << exec_name << " [flags]\n"
        << "Allowed flags include:\n"
        << "  --help (or -h) : Print this message.\n"
        << "\n"
        << "Current plug-ins: " << emp::MakeList(plug_ins.GetNames(), ", ")
        << std::endl;
    
    plug_ins.OnHelp();  // Allow plug-ins to provide help information.      
    Exit();
  }

  void Trace(AvidaVM & vm, size_t cpu_cycles=200) {
    for (size_t i = 0; i < cpu_cycles; ++i) {
      std::cout << "STEP " << i << ":\n" << vm.StatusString() << std::endl;
      vm.ProcessStep();
    }
    std::cout << "STEP " << cpu_cycles << ":\n" << vm.StatusString() << std::endl;
  }

  // Generate many random genomes of a given size and try running them.
  template <typename MANAGER_T=AvidaVM>
  bool Test(const size_t genome_size = 256, const size_t num_trials = 5000000, size_t run_time=200) {
    // Create the hardware manager.
    auto inst_set = MANAGER_T::BuildInstSet();

    // Load in the default ancestor genome.
    auto exp_genome = inst_set.LoadGenome("../config/ancestor.org");
    if (!exp_genome) return false;
    organism_t org(hw_manager, *exp_genome);
    for (size_t trial = 0; trial < num_trials; ++trial) {
      if (trial % 100000 == 0) std::cout << "Trial: " << trial << std::endl; 
      // Build a random genome and run it.
      inst_set.BuildGenome(*exp_genome, genome_size, random);
      org.Process(run_time);
    }
    return true;
  }


  // ====== Configuration Management ======

  // Initialize the hardware manager (with AvidVM) and the biota (with the default) ancestor.
  void Setup() {
    if (run_state != RunState::INITIALIZING) return;

    // Add callbacks for the current hardware manager.
    hw_manager.AddCallback("DivideCell", [this](OrganismBase & org){ DivideOrg(org); });

    // Inject a single individual of the default ancestor.
    Inject("../config/ancestor.org");
  }

  template <typename TRAIT_T>
  void RegisterTrait(const emp::String & name, const emp::String & desc,
                     const emp::String & filename, size_t line,
                     auto get_fun, auto cget_fun)
  {
    trait_man.template Register<TRAIT_T>(name, desc, filename, line, get_fun, cget_fun);
  }

  // ====== Organism Management ======

  void Inject(genome_t && genome) {
    organism_t inject_org{hw_manager, std::move(genome)};
    plug_ins.OnInjectReady(inject_org);
    return PlaceOrganism(std::move(inject_org));
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param filename - The name of the file with the genome information.
  void Inject(const emp::String & filename) {
    auto exp_genome = hw_manager.LoadGenome(filename);
    if (!exp_genome) {
      emp::notify::Error("Failed to inject from file '", filename, "'.");
    }

    return Inject(std::move(*exp_genome));
  }

  // Collect an offspring from a designated parent organism.
  void DivideOrg(organism_t & parent_org) {
    emp_assert(parent_org.OK());
    plug_ins.BeforeRepro(parent_org);
    genome_t offspring_genome = parent_org.DivideGenome(random);
    if (offspring_genome.size()) {
      organism_t offspring{parent_org, std::move(offspring_genome)};
      plug_ins.OnOffspringReady(offspring, parent_org);
      PlaceOrganism( std::move(offspring) );
    }
  }
  void DivideOrg(OrganismBase & parent_org) { DivideOrg( static_cast<organism_t&>(parent_org) ); }

  // Delete an organism at a specific position in the biota.
  void DeleteOrg(size_t delete_id) {
    emp_assert(IsOccupied(delete_id));

    plug_ins.BeforeDeath(biota[delete_id]); // Notify plug-ins of impending death.
    biota[delete_id].SignalDeath();       // Notify organism before deletion.
    occupied.Clear(delete_id);
    --org_count;
  }

  // Delete a random (occupied) organism position.
  void DeleteOrg() {
    emp_assert(GetNumOrgs() > 0); // Must have something to delete!
    size_t delete_id = random.GetUInt32(biota.size());
    if (IsOccupied(delete_id)) DeleteOrg(delete_id);
    else DeleteOrg(); // Our random choice did not work... try again!
  }


  // ====== Run Management ======

  // Process a single update for Avida
  void DoUpdate() {
    emp_assert(GetNumOrgs() > 0, "Running DoUpdate() with no organisms.");
    plug_ins.OnUpdateEnd(update);
    ++update;
    plug_ins.OnUpdateStart(update);
  }

  void ProcessOrg(size_t id, int32_t num_cycles) {
    biota[id].Process(num_cycles);
  }

  void Run() {
    if (run_state == RunState::INITIALIZING) {
      Setup();
      run_state = RunState::RUNNING;
    }
    while (run_state == RunState::RUNNING) DoUpdate();
  }

  void Exit() {
    if (run_state == RunState::EXITING) return; // Already exiting.

    // If plug-ins have been initialized, notify them of impending exit.
    if (run_state > RunState::INITIALIZING) plug_ins.BeforeExit();
    biota.clear();
    hw_manager.Clear();
    trait_man.Clear();
    run_state = RunState::EXITING;
  }
};
