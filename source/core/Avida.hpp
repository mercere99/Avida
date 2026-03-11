#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  This is the main controller class for Avida.
 * 
 *  DEVELOPER notes:
 *  - Add module PopGrid
 *  - Add module GridFacing (with dependency on PopGrid)
 *  - Add module PrintGrid that prints grid every X updates (with dependency on PopGrid)
 *  - Add module TrackGenotype
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
 *  - Specialized base classes for module types: populations (1), drivers (1), analyzers, environments
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

#include "concepts.hpp"
#include "ModuleBase.hpp"
#include "Organism.hpp"
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
  using hardware_t = HARDWARE_T;                         // Type of general hardware for all organisms
  using inst_set_t = hardware_t::inst_set_t;             // Type of inst set used by this hardware.
  using organism_t = Organism<hardware_t, phenotype_t>;  // Type for each individual organism
  using biota_t = emp::vector<organism_t>;               // Collection of all current organisms in Avida
  using genome_t = hardware_t::genome_t;                 // Type of genomes used in organisms

  // Make sure all of the components fit the proper concepts.
  static_assert(concepts::Genome<genome_t>);
  static_assert(concepts::Hardware<hardware_t>);
  static_assert(concepts::Organism<organism_t>);

private:
  inst_set_t inst_set;
  TraitManager<this_t> trait_man;
  PlugInManager<this_t, PLUG_IN_Ts<this_t>...> plug_ins;

  size_t update = 0;          // Times update was run on this population
  emp::Random random;         // Central random number generator
  biota_t biota{};            // Collection of all current organisms
  emp::BitVector occupied{};  // Which organisms in biota are active?
  uint32_t num_orgs = 0;      // Current number of active organisms
  size_t total_orgs = 0;      // Total orgs that have ever existed (used for global IDs)

  enum class RunState { INITIALIZING, RUNNING, PAUSED, EXITING, ERROR };
  RunState run_state = RunState::INITIALIZING;

  // ===== Helper Functions =====

  // Set up a new organism in the biota, returning a reference to it.
  organism_t & ReserveOrganism(genome_t && new_genome) {
    size_t index = occupied.FindZero();    // Find empty position in biota.
    if (index == emp::BitVector::npos) {   // If no empty position available, add one.
      index = occupied.PushBack(true);
      biota.emplace_back(inst_set, std::move(new_genome));
    } else {
      occupied.Set(index);
      biota[index].Reset(std::move(new_genome));
    }
    ++num_orgs;
    organism_t & reserved_org = biota[index];
    reserved_org.SetBiotaID(index);
    reserved_org.SetGlobalID(total_orgs++);

    return reserved_org;
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
  [[nodiscard]] uint32_t GetNumOrgs() const { return num_orgs; }
  [[nodiscard]] size_t GetTotalOrgs() const { return total_orgs; }
  [[nodiscard]] const inst_set_t & GetInstSet() const { return inst_set; }

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

  // ====== Configuration Management ======

  // Initialize AvidaVM and the biota with the (default) ancestor.
  void Setup() {
    if (run_state != RunState::INITIALIZING) return;

    // Add callbacks for the current hardware manager.
    inst_set.AddCallbackInst("DivideCell", [this](size_t biota_id){ DivideOrg(biota[biota_id]); });

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

  organism_t & Inject(genome_t && genome) {
    organism_t & inject_org = ReserveOrganism(std::move(genome));
    plug_ins.OnInjectReady(inject_org);    // Trigger for injections only
    plug_ins.BeforePlacement(inject_org);  // Trigger to set up organisms for activation
    plug_ins.OnPlacement(inject_org);      // Trigger to activate organism in populations
    return inject_org;
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param filename - The name of the file with the genome information.
  organism_t & Inject(const emp::String & filename) {
    auto exp_genome = inst_set.LoadGenome(filename);
    if (!exp_genome) emp::notify::Error("Failed to inject from file '", filename, "'.");
    return Inject(std::move(*exp_genome));
  }

  /// Collect an offspring from a designated parent organism.
  void DivideOrg(organism_t & parent) {
    emp_assert(parent.OK());
    plug_ins.BeforeRepro(parent);
    genome_t offspring_genome = parent.DivideGenome();

    if (offspring_genome.size()) {
      organism_t & offspring = ReserveOrganism(std::move(offspring_genome));
      plug_ins.OnOffspringInit(offspring, parent);   // Trigger: set up offspring (e.g., mutations)
      plug_ins.OnOffspringReady(offspring, parent);  // Trigger: offspring is all set up
      plug_ins.BeforePlacement(offspring);           // Trigger: set up ANY organism for activation
      plug_ins.OnPlacement(offspring);               // Trigger: activate organism in populations
    }
  }
  void DivideOrg(OrganismBase & parent) { DivideOrg( static_cast<organism_t&>(parent) ); }

  // Delete an organism at a specific position in the biota.
  void DeleteOrg(size_t delete_id) {
    emp_assert(IsOccupied(delete_id));

    plug_ins.BeforeDeath(biota[delete_id]); // Notify plug-ins of impending death.
    biota[delete_id].SignalDeath();       // Notify organism before deletion.
    occupied.Clear(delete_id);
    --num_orgs;
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

  void ProcessOrg(size_t id, uint32_t num_cycles) {
    for (size_t i = 0; i < num_cycles; ++i) {
      biota[id].GetHardware().ProcessStep();
    }
  }

  void Run() {
    if (run_state == RunState::INITIALIZING) Setup();
    run_state = RunState::RUNNING;
    while (run_state == RunState::RUNNING) DoUpdate();
  }

  void Exit() {
    if (run_state == RunState::EXITING) return; // Already exiting.

    // If plug-ins have been initialized, notify them of impending exit.
    if (run_state > RunState::INITIALIZING) plug_ins.BeforeExit();
    biota.clear();
    trait_man.Clear();
    run_state = RunState::EXITING;
  }

  bool OK() {
    emp_assert(occupied.size() == biota.size());
    emp_assert(occupied.CountOnes() == num_orgs);

    bool orgs_ok = true;
    for (size_t index : occupied) orgs_ok = orgs_ok && biota[index].OK();

    return orgs_ok;
  }

};
