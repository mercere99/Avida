#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  This is the main controller class for Avida.
 */

#include <filesystem>  // std::filesystem::path
#include <fstream>     // std::ifstream, std::ofstream

#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/bits/BitVector.hpp"
#include "emp/config/command_line.hpp"
#include "emp/config/SettingsManager.hpp"
#include "emp/datastructs/map_utils.hpp"
#include "emp/datastructs/UnorderedIndexMap.hpp"
#include "emp/meta/TypePack.hpp"
#include "emp/serialize/SerialPod.hpp"
#include "emp/tools/Timer.hpp"

#include "Biota.hpp"
#include "concepts.hpp"
#include "ModuleBase.hpp"
#include "Organism.hpp"
#include "Phenotype.hpp"
#include "PlugInManager.hpp"

namespace fs = std::filesystem;

/// Main Avida-control object.
template <template <typename> typename... PLUG_IN_Ts>
class Avida {
public:
  using this_t = Avida<PLUG_IN_Ts...>;

  // Helper to merge a TypePack of types into a single merged type.
  template <typename T> struct merge_from_TypePack;

  template <typename... Ts>
  struct merge_from_TypePack<emp::TypePack<Ts...>> {
    struct merged_t : Ts... { };
  };

  // Pre-compute GlobalTypes using a stub type before instantiating modules with this_t.
  // Makes genome_t available when modules are instantiated (e.g., so a member can be genome_t).
  struct avida_stub_t {
    struct stub_genome_t {};
    using genome_t = stub_genome_t;
  };
  template <typename T> using to_global_types = typename T::GlobalTypes;
  using stub_pack_t = emp::TypePack<PLUG_IN_Ts<avida_stub_t>...>;
  using global_types_pack_t = stub_pack_t::template filter<to_global_types>::template wrap<to_global_types>;
  using global_types_t = merge_from_TypePack<global_types_pack_t>::merged_t;
  using genome_t = global_types_t::genome_t;  // Available as this_t::genome_t hereafter.

  // Instantiate modules with this_t (genome_t is already defined above).
  using plug_in_pack_t = emp::TypePack<PLUG_IN_Ts<this_t>...>;

  // Merge the Phenotype members from the modules.
  template <typename T> using to_pheno_member = typename T::Phenotype;
  using pheno_pack_t = plug_in_pack_t::template filter<to_pheno_member>::template wrap<to_pheno_member>;
  using phenotype_t = merge_from_TypePack<pheno_pack_t>::merged_t;

  // Isolate the types used in this Avida instance.
  using organism_t = Organism<genome_t, phenotype_t>;  // Type for each individual organism
  using biota_t = Biota<organism_t>;                   // All current organisms in Avida

  // Make sure all of the components fit the proper concepts.
  static_assert(concepts::Genome<genome_t>);
  static_assert(concepts::Organism<organism_t>);

private:
  TraitManager<this_t> trait_man;
  emp::RobinHoodMap<emp::String, size_t> task_ids;
  emp::vector<emp::String> task_names;  // Task name by ID (index == task ID).
  PlugInManager<this_t, PLUG_IN_Ts<this_t>...> plug_ins;

  emp::SettingsManager settings; // Collection of all configurable settings
  fs::path data_dir = "data/";   // Directory for all data files
  size_t update = 0;             // Times update was run on this population
  emp::Random random{0};         // Central random number generator
  biota_t biota{};               // Collection of all current organisms

  // EXITING = stop has been requested; finish the current update, then tear down.
  // COMPLETE = end-of-run teardown has happened (organisms cleared); nothing left to run.
  enum class RunState { INITIALIZING, PAUSED, RUNNING, EXITING, COMPLETE, ERROR };
  RunState run_state = RunState::INITIALIZING;

public:
  Avida() : plug_ins(*this) {
    AddSetting("base.random_seed",
      [this](){ return random.GetSeed(); },
      [this](size_t new_seed){ random.ResetSeed(new_seed); },
      "Main random number seed", 's', "0");
    AddSetting("base.config_dir",
      [this](){ return settings.GetConfigDir().string(); },
      [this](const emp::String & s){ settings.SetConfigDir(s); },
      "Default directory to find configuration files.");
    AddSetting("base.data_dir",
      [this](){ return data_dir.string(); },
      [this](const emp::String & s){ data_dir = s.str(); },
      "Default directory to write data files.", 'd');

    AddKeyword("help",
      [this](emp::vector<emp::String> kw_args) {
        std::println("Avida v5.0.0\n");
        settings.PrintHelp(kw_args);
        AVIDA_SIGNAL(OnHelp());  // Allow plug-ins to provide help information.      
        Exit();
      }, "Print help info for this program", 'h', /*max_args=*/ 1);

    AddKeyword("config",
      [this](emp::vector<emp::String> kw_args) {
        emp::String filename = kw_args.size() ? kw_args[0] : "Avida.cfg";
        std::println("Loading config file '{}'...", filename);
        settings.Load(filename);
      }, "Load a config file with the provided name (default: Avida.cfg)", 'c', /*max_args=*/ 1);

    AddKeyword("generate",
      [this](emp::vector<emp::String> kw_args) {
        emp::String filename = kw_args.size() ? kw_args[0] : "Avida.cfg";
        std::println("Generating config file '{}'...", filename);
        settings.Save(filename);
        Exit();
      }, "Generate a config file with the provided name (default: Avida.cfg) and stop", 'g', /*max_args=*/ 1);

    // Allow plug-ins to register anything that they need to...
    AVIDA_SIGNAL(RegisterTraits());    // Set up any traits in the phenotype.
    AVIDA_SIGNAL(RegisterSettings());  // Set up parameters for the config file.
    AVIDA_SIGNAL(RegisterCallbacks()); // Set up new instructions for the instruction set.
  }
  Avida(emp::vector<emp::String> args) : Avida() { settings.LoadArgs(args); }
  ~Avida() {
    Shutdown();
  }

  // === Basic Accessors ===

  [[nodiscard]] size_t GetUpdate() const { return update; }
  [[nodiscard]] emp::Random & GetRandom() { return random; }
  [[nodiscard]] const fs::path & GetDataDir() { return data_dir; }  
  [[nodiscard]] auto & GetOrg(this auto & self, size_t id) { return self.biota[id]; }
  [[nodiscard]] auto & GetOrgs(this auto & self) { return self.biota.GetOrgs(); }
  [[nodiscard]] auto & GetBiota(this auto & self) { return self.biota; }
  [[nodiscard]] bool IsOccupied(size_t id) const { return biota.IsActive(id); }
  [[nodiscard]] uint32_t GetNumOrgs() const { return biota.GetNumOrgs(); }
  [[nodiscard]] size_t GetBiotaSize() const { return biota.GetSize(); }
  [[nodiscard]] size_t GetTotalOrgs() const { return biota.GetTotalOrgs(); }
  [[nodiscard]] emp::vector<size_t> GetActiveIDs() const { return biota.GetActiveIDs(); }
  [[nodiscard]] emp::BitVector GetActiveBits() const { return biota.GetActiveBits(); }

  [[nodiscard]] auto & GetFirstOrg(this auto & self) {
    const size_t id = self.biota.FindFirstActive();
    return self.biota[id];
  }

  [[nodiscard]] const auto & GetTrait(const emp::String & name) const {
    return trait_man.Get(name);
  }

  [[nodiscard]] bool HasTrait(const emp::String & name) const { return trait_man.Has(name); }

  // Typed trait lookup: returns a Trait<TRAIT_T> reference with direct organism-level accessors,
  // bypassing virtual dispatch. Use this in performance-sensitive loops.
  template <typename TRAIT_T>
  [[nodiscard]] const auto & GetTypedTrait(const emp::String & name) const {
    return trait_man.template GetTyped<TRAIT_T>(name);
  }

  [[nodiscard]] double CalcTraitMin(const emp::String & name) const {
    const auto & trait = GetTrait(name);
    return biota.CalcMinimum([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
  }

  [[nodiscard]] double CalcTraitMax(const emp::String & name) const {
    const auto & trait = GetTrait(name);
    return biota.CalcMaximum([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
  }

  [[nodiscard]] double CalcTraitAve(const emp::String & name) const {
    const auto & trait = GetTrait(name);
    return biota.CalcAverage([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
  }

  [[nodiscard]] auto & FindOrg_MinTrait(const emp::String & name) const {
    emp_assert(biota.GetNumOrgs() > 0);
    const auto & trait = GetTrait(name);
    const size_t id = biota.FindMinimumID([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
    return biota[id];
  }

  [[nodiscard]] auto & FindOrg_MaxTrait(const emp::String & name) const {
    emp_assert(biota.GetNumOrgs() > 0);
    const auto & trait = GetTrait(name);
    const size_t id = biota.FindMaximumID([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
    return biota[id];
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

  void AddCallback(const emp::String & name, std::function<void(size_t)> callback) {
    AVIDA_SIGNAL( AddCallback(name, callback) );
  }

  template <typename TRAIT_T>
  void RegisterTrait(const emp::String & name, const emp::String & desc,
                     const emp::String & filename, size_t line,
                     auto get_fun, auto cget_fun)
  {
    trait_man.template Register<TRAIT_T>(name, desc, filename, line, get_fun, cget_fun);
  }

  size_t RegisterTask(const emp::String & name) {
    emp_assert(!task_ids.contains(name), "Registering same task twice", name);
    const size_t task_id = task_names.size();
    task_ids[name] = task_id;
    task_names.push_back(name);
    return task_id;
  }

  // === Task Lookups ===

  [[nodiscard]] size_t GetNumTasks() const { return task_names.size(); }
  [[nodiscard]] bool HasTask(const emp::String & name) const { return task_ids.contains(name); }

  // Look up the unique ID for an already-registered task by name.
  [[nodiscard]] size_t GetTaskID(const emp::String & name) const {
    emp_assert(task_ids.contains(name), "Requesting an unknown task name", name);
    return task_ids.FindValue(name, 0);
  }

  // Look up the name of a task by its ID.
  [[nodiscard]] const emp::String & GetTaskName(size_t task_id) const {
    emp_assert(task_id < task_names.size(), "Requesting an invalid task ID", task_id);
    return task_names[task_id];
  }

  // Broadcast that an organism just performed a task so other modules can respond.
  void SignalTask(organism_t & org, size_t task_id) {
    AVIDA_SIGNAL(OnTaskComplete(org, task_id));
  }

  // Broadcast a value an organism sent to output so other modules can respond.
  void SignalOutput(organism_t & org, uint32_t output) {
    AVIDA_SIGNAL(OnOutputValue(org, output));
  }

  // ====== Organism Management ======

  organism_t & Inject(organism_t & inject_org) {
    inject_org.ResetHardware();                 // Reset organism as placed into pop.
    AVIDA_SIGNAL(OnInjectReady(inject_org));    // Trigger for injections only
    AVIDA_SIGNAL(BeforePlacement(inject_org));  // Trigger to set up organisms for activation
    AVIDA_SIGNAL(OnPlacement(inject_org));      // Trigger to activate organism in populations
    return inject_org;
  }

  void Inject(const genome_t & genome, size_t count=1) {
    emp_assert(count > 0);
    for (size_t i = 0; i < count; ++i) {
      organism_t & inject_org = biota.ReserveOrganism(genome);
      Inject(inject_org);
    }
  }
  
  organism_t & Inject(genome_t && genome) {
    organism_t & inject_org = biota.ReserveOrganism(std::move(genome));
    return Inject(inject_org);
  }
  
  /// @brief Inject an organism using a genome loaded from a file.
  /// @param filepath - Path to the file with the genome information.
  organism_t & Inject(const fs::path & filepath) {
    auto exp_genome = AVIDA_HANDLE(genome_t, LoadGenome(filepath));
    if (!exp_genome) emp::notify::Error("Failed to inject from file '", filepath.string(), "'.");
    return Inject(std::move(*exp_genome));
  }


  genome_t GetOffspringGenome(organism_t & parent) {
    emp_assert(IsOccupied(parent.GetBiotaID()), "parent is not active", parent.GetBiotaID());
    AVIDA_SIGNAL(BeforeRepro(parent));
    return parent.GetOffspringGenome();
  }

  /// Place an offspring into the population.
  /// Parent may still be running, so should not reallocate memory.
  organism_t & BuildOffspring(organism_t & parent, genome_t && offspring_genome) {
    emp_assert(biota.GetNumOrgs() < biota.GetCapacity(), "Biota is not large enough to add offspring");
    emp_assert(IsOccupied(parent.GetBiotaID()), "parent is not active", parent.GetBiotaID());
    emp_assert(offspring_genome.size() > 0);
    organism_t & offspring = biota.ReserveOrganism(std::move(offspring_genome));
    AVIDA_SIGNAL(OnOffspringInit(offspring, parent));   // Trigger: set up offspring (e.g., mutations)
    offspring.ResetHardware();
    AVIDA_SIGNAL(OnOffspringReady(offspring, parent));  // Trigger: offspring is all set up
    return offspring;
  }

  void PlaceOffspring(organism_t & offspring) {
    AVIDA_SIGNAL(BeforePlacement(offspring));           // Trigger: set up ANY organism for activation
    AVIDA_SIGNAL(OnPlacement(offspring));               // Trigger: activate organism in populations
  }

  /// Collect an offspring from a designated parent organism.
  void DivideOrg(size_t parent_id) {
    organism_t & parent = biota[parent_id];
    genome_t offspring_genome = GetOffspringGenome(parent);
    if (offspring_genome.size() == 0) return;               // Stop early if no genome was provided.
    organism_t & offspring = BuildOffspring(parent, std::move(offspring_genome));
    PlaceOffspring(offspring);
  }

  using PendingOffspring = ::PendingOffspring<genome_t>;

  /// Build and place offspring from a SET of designated parent organisms.
  /// No organisms should be running when this is called; any orgs may be removed.
  void AddOffspringSet(std::span<PendingOffspring> pending_set) {
    std::vector<emp::Ptr<organism_t>> new_orgs;              // Pointers to track new organisms
    new_orgs.reserve(pending_set.size());
    biota.Reserve(biota.GetNumOrgs() + pending_set.size());  // Expand biota to fit offspring

    // Phase 1: build all offspring before any placements (so parents stay alive).
    for (auto & [parent_id, genome] : pending_set) {
      new_orgs.push_back(&BuildOffspring(biota[parent_id], std::move(genome)));
    }

    // Phase 2: place all offspring (may kill organisms, including other parents).
    for (emp::Ptr<organism_t> org_ptr : new_orgs) PlaceOffspring(*org_ptr);
  }

  // Delete an organism at a specific position in the biota.
  void DeleteOrg(size_t delete_id) {
    emp_assert(biota.IsActive(delete_id));

    AVIDA_SIGNAL(BeforeDeath(biota[delete_id]));  // Notify plug-ins of impending death.
    biota[delete_id].SignalDeath();               // Notify organism before deletion.
    biota.Remove(delete_id);
  }

  // ====== Run Management ======

  template <typename... Ts>
  void TriggerSignal(Ts &&... args) { plug_ins.TriggerSignal(std::forward<Ts>(args)...); }

  template <typename RETURN_T, typename... Ts>
  auto TriggerHandler(Ts &&... args) {
    return plug_ins.template TriggerHandler<RETURN_T>(std::forward<Ts>(args)...);
  }

  template <typename RETURN_T, typename... Ts>
  auto TriggerCollector(Ts &&... args) {
    return plug_ins.template TriggerCollector<RETURN_T>(std::forward<Ts>(args)...);
  }

  // Process a single update for Avida
  void DoUpdate() {
    emp_assert(GetNumOrgs() > 0, "Running DoUpdate() with no organisms.");
    ++update;
    AVIDA_SIGNAL(OnUpdateStart(update));  // Set up for a new update
    AVIDA_SIGNAL(OnUpdate(update));       // Run organisms
    AVIDA_SIGNAL(OnUpdateEnd(update));    // Report stats / check stop condition
  }

  void ProcessOrg(size_t id, uint32_t num_cycles) {
    auto & hw = biota[id].Hardware();
    for (size_t i = 0; i < num_cycles; ++i) {
      hw.ProcessStep();
    }
  }

  void SetupDataDir() {
    fs::create_directories(data_dir);
    if (!fs::is_directory(data_dir)) {
      if (fs::exists(data_dir)) {
        emp::notify::Error("Specified data_dir '", data_dir, "' exists, but is not a directory.");
      } else {
        emp::notify::Error("Failed to create data directory '", data_dir, "'.");
      }
    }
  }

  void Initialize() {
    auto reserve_counts = AVIDA_COLLECT(size_t, GetOrgReserveCount());
    auto reserve_total = std::accumulate(reserve_counts.begin(), reserve_counts.end(), size_t{0});
    biota.Reserve(reserve_total + 1);
    SetupDataDir();
    AVIDA_SIGNAL(BeforeStart()); // Trigger plug-ins to initialize.
    settings.PrintStatus();
    AVIDA_SIGNAL(OnStart());     // Trigger injection of start organisms.
  }

  void Run() {
    emp_assert(run_state != RunState::COMPLETE, "Run() should not be called on finished run.");
    if (run_state < RunState::EXITING) {
      if (run_state == RunState::INITIALIZING) Initialize();
      run_state = RunState::RUNNING;
      while (run_state == RunState::RUNNING) DoUpdate();
    }
    if (run_state == RunState::EXITING) Shutdown();
  }

  // Request that the run stop.  Teardown is deferred to Shutdown() (called once the current
  // update finishes) so the final update's signal handlers still see a populated biota.
  void Exit() {
    if (run_state == RunState::EXITING || run_state == RunState::COMPLETE) return;
    run_state = RunState::EXITING;
  }

  // End-of-run teardown.  Idempotent: the body runs at most once.
  void Shutdown() {
    if (run_state == RunState::COMPLETE) return;
    run_state = RunState::COMPLETE;
    SaveState("final_save");
    AVIDA_SIGNAL(BeforeExit());                 // Notify plug-ins of impending exit (biota intact)
    biota.Clear();                              // Clean up organisms
    trait_man.Clear();                          // Clean up traits
  }

  void Serialize(emp::SerialPod & pod) {
    // Note: `trait_man` is constructed at the start and should re-build correctly. 
    pod(plug_ins,
        settings,
        data_dir,
        update,
        random,
        biota,
        run_state
       );

    // Use trait manager to save/load phenotypes.
    biota.ForEachOrg([&](auto & org){ trait_man.SerializeOrg(pod, org); });
  }

  void SaveState(const emp::String & filename) {
    std::ofstream ofs{filename.str()};
    emp::SerialPod pod{ofs};
    pod(*this);
  }

  void LoadState(const emp::String & filename) {
    std::ifstream ifs{filename.str()};
    emp::SerialPod pod{ifs};
    pod(*this);
  }

    bool OK() { return biota.OK(); }
};
