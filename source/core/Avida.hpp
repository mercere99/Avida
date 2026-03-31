#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  This is the main controller class for Avida.
 */

#include <filesystem>  // std::filesystem::path

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/bits/BitVector.hpp"
#include "emp/config/command_line.hpp"
#include "emp/config/SettingsManager.hpp"
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

  emp::SettingsManager settings;  // Collection of all configurable settings
  size_t update = 0;              // Times update was run on this population
  emp::Random random{0};          // Central random number generator
  biota_t biota{};                // Collection of all current organisms
  emp::BitVector occupied{};      // Which organisms in biota are active?
  uint32_t num_orgs = 0;          // Current number of active organisms
  size_t total_orgs = 0;          // Total orgs that have ever existed (used for global IDs)

  enum class RunState { INITIALIZING, PAUSED, RUNNING, EXITING, ERROR };
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
      biota[index].SetGenome(std::move(new_genome));
    }
    ++num_orgs;
    organism_t & reserved_org = biota[index];
    reserved_org.SetBiotaID(index);
    reserved_org.SetGlobalID(total_orgs++);

    return reserved_org;
  }

public:
  Avida() : plug_ins(*this) {
    AddSetting("random.seed",
      [this](){ return random.GetSeed(); },
      [this](size_t new_seed){ random.ResetSeed(new_seed); },
      "Main random number seed", 's', "0");
    AddSetting("base.config_dir",
      [this](){ return settings.GetConfigDir().string(); },
      [this](const emp::String & s){ settings.SetConfigDir(s); },
      "Default directory to find configuration files.");

    AddKeyword("help",
      [this](emp::vector<emp::String> kw_args) {
        std::println("Avida v5.0.0\n");
        settings.PrintHelp(kw_args);
        plug_ins.OnHelp();  // Allow plug-ins to provide help information.      
        Exit();
      }, "Print help info for this program", 'h', /*max_args=*/ 1);

    AddKeyword("generate",
      [this](emp::vector<emp::String> kw_args) {
        emp::String filename = kw_args.size() ? kw_args[0] : "Avida.cfg";
        std::println("Generating config file '", filename, "'...");
        settings.Save(filename);
        Exit();
      }, "Generate a config file with the provided name (or Avida.cfg)", 'g', /*max_args=*/ 1);

    // Allow plug-ins to register anything that they need to...
    plug_ins.RegisterTraits();    // Set up any traits in the phenotype.
    plug_ins.RegisterSettings();  // Set up parameters for the config file.
    plug_ins.RegisterCallbacks(); // Set up new instructions for the instruction set.
  }
  Avida(emp::vector<emp::String> args) : Avida() { settings.LoadArgs(args); }
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
  [[nodiscard]] size_t GetBiotaSize() const { return biota.size(); }
  [[nodiscard]] size_t GetTotalOrgs() const { return total_orgs; }
  [[nodiscard]] const inst_set_t & GetInstSet() const { return inst_set; }

  template <std::invocable<const organism_t &> FUN_T>
  [[nodiscard]] double GetAveTrait(const FUN_T & fun) const {
    double total = 0.0;    
    for (size_t index : occupied) total += fun(biota[index]);
    return total / GetNumOrgs();
  }

  const auto & GetTrait(const emp::String & name) const {
    return trait_man.Get(name);
  }

  [[nodiscard]] double GetAveTrait(const emp::String & name) const {
    const auto & trait = GetTrait(name);
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

  // ====== Configuration Management ======

  const emp::SettingsManager & GetSettings() const { return settings; }
  emp::SettingsManager & GetSettings() { return settings; }

  template <typename... ARG_Ts>
  void AddSetting(ARG_Ts &&... args) { settings.AddSetting(std::forward<ARG_Ts>(args)...); }
  template <typename... ARG_Ts>
  void AddKeyword(ARG_Ts &&... args) { settings.AddKeyword(std::forward<ARG_Ts>(args)...); }
  template <typename... ARG_Ts>
  void AddCallback(ARG_Ts &&... args) { inst_set.AddCallbackInst(std::forward<ARG_Ts>(args)...); }

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
    inject_org.ResetHardware();
    plug_ins.OnInjectReady(inject_org);    // Trigger for injections only
    plug_ins.BeforePlacement(inject_org);  // Trigger to set up organisms for activation
    plug_ins.OnPlacement(inject_org);      // Trigger to activate organism in populations
    return inject_org;
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param filepath - Path to the file with the genome information.
  organism_t & Inject(const std::filesystem::path & filepath) {
    auto exp_genome = inst_set.LoadGenome(filepath);
    if (!exp_genome) emp::notify::Error("Failed to inject from file '", filepath.string(), "'.");
    return Inject(std::move(*exp_genome));
  }

  /// Collect an offspring from a designated parent organism.
  void DivideOrg(size_t parent_id) {
    emp_assert(IsOccupied(parent_id));
    emp_assert(biota[parent_id].OK());
    plug_ins.BeforeRepro(biota[parent_id]);
    genome_t offspring_genome = biota[parent_id].DivideGenome();

    if (offspring_genome.size()) {
      // ReserveOrganism may call biota.emplace_back, reallocating the vector and
      // invalidating any prior reference to biota elements.  Re-fetch parent after.
      organism_t & offspring = ReserveOrganism(std::move(offspring_genome));
      organism_t & parent = biota[parent_id];
      plug_ins.OnOffspringInit(offspring, parent);   // Trigger: set up offspring (e.g., mutations)
      offspring.ResetHardware();
      plug_ins.OnOffspringReady(offspring, parent);  // Trigger: offspring is all set up
      plug_ins.BeforePlacement(offspring);           // Trigger: set up ANY organism for activation
      plug_ins.OnPlacement(offspring);               // Trigger: activate organism in populations
    }
  }

  // Delete an organism at a specific position in the biota.
  void DeleteOrg(size_t delete_id) {
    emp_assert(IsOccupied(delete_id));

    plug_ins.BeforeDeath(biota[delete_id]); // Notify plug-ins of impending death.
    biota[delete_id].SignalDeath();         // Notify organism before deletion.
    occupied.Clear(delete_id);
    --num_orgs;
  }

  // ====== Run Management ======

  // Process a single update for Avida
  void DoUpdate() {
    emp_assert(GetNumOrgs() > 0, "Running DoUpdate() with no organisms.");
    ++update;
    plug_ins.OnUpdateStart(update);  // Run organisms
    plug_ins.OnUpdateEnd(update);    // Report stats / check stop condition
  }

  void ProcessOrg(size_t id, uint32_t num_cycles) {
    for (size_t i = 0; i < num_cycles; ++i) {
      biota[id].GetHardware().ProcessStep();
    }
  }

  void Run() {
    switch (run_state) {
    case RunState::INITIALIZING:
      plug_ins.OnStart(); // Trigger plug-ins to initialize.
      [[fallthrough]];
    case RunState::PAUSED: run_state = RunState::RUNNING; [[fallthrough]];
    case RunState::RUNNING: while (run_state == RunState::RUNNING) DoUpdate(); [[fallthrough]];
    case RunState::EXITING: case RunState::ERROR: // Do nothing.
    }
  }

  void Exit() {
    if (run_state == RunState::EXITING) return; // Already exiting.
    run_state = RunState::EXITING;              // Change run_state in case check by plug-ins
    plug_ins.BeforeExit();                      // Notify plug-ins of impending exit
    biota.clear();                              // Clean up organisms
    trait_man.Clear();                          // Clean up traits
  }

  bool OK() {
    emp_assert(occupied.size() == biota.size());
    emp_assert(occupied.CountOnes() == num_orgs);
    for (size_t index : occupied) if (!biota[index].OK()) return false;
    return true;
  }
};
