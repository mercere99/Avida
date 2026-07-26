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
#include <functional>  // std::function
#include <iostream>    // std::cout, std::ostream
#include <sstream>     // std::istringstream
#include <type_traits> // std::is_arithmetic_v, std::invoke_result_t

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
#include "QueryValue.hpp"
#include "QueryManager.hpp"

namespace fs = std::filesystem;

/// Main Avida-control object.
///
/// Within a single signal, modules are called from left to right in the order they are listed
/// in this class's template arguments.  Use separate lifecycle signals when one module must
/// finish a phase before another module begins the next phase.
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
    struct stub_organism_t {};
    using organism_t = stub_organism_t;
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
  using org_ref_t = OrgRef<biota_t>;                   // Stable, validity-aware organism handle
  using org_set_t = OrgSet<biota_t>;                   // Epoch-bound collection of organisms
  using query_value_t = QueryValue<biota_t>;           // Runtime value returned by queries
  using query_manager_t = QueryManager<this_t>;        // Compile and evaluate query strings
  using query_context_t = QueryContext<this_t>;        // Optional organism/collection scope
  using compiled_query_t = CompiledQuery<this_t>;      // A reusable, parsed query

  // Make sure all of the components fit the proper concepts.
  static_assert(concepts::Genome<genome_t>);
  static_assert(concepts::Organism<organism_t>);

private:
  TraitManager<this_t> trait_man;
  emp::RobinHoodMap<emp::String, size_t> task_ids;
  emp::vector<emp::String> task_names;  // Task name by ID (index == task ID).

  emp::SettingsManager settings; // Collection of all configurable settings
  fs::path data_dir = "data/";   // Directory for all data files
  size_t update = 0;             // Times update was run on this population
  emp::Random random{0};         // Central random number generator
  biota_t biota{};               // Collection of all current organisms

  // EXITING = stop has been requested; finish the current update, then tear down.
  // COMPLETE = end-of-run teardown has happened (organisms cleared); nothing left to run.
  enum class RunState { INITIALIZING, PAUSED, RUNNING, EXITING, COMPLETE, ERROR };
  RunState run_state = RunState::INITIALIZING;

  query_manager_t query_man;
  PlugInManager<this_t, PLUG_IN_Ts<this_t>...> plug_ins;

public:
  Avida() : query_man(*this), plug_ins(*this) {
    settings.SetOutputPathResolver([this](const fs::path & path) {
      return path.is_absolute() ? path : data_dir / path;
    });

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
    AddValue("base.update", [this](){ return update; }, "Current population update");

    AddOutputKeyword("print",
      [this](const emp::vector<emp::String> & args, std::ostream & os) {
        PrintQueries(args, os);
      },
      "Print comma-separated query expressions; supports trailing > filename or >> filename");

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
        std::ofstream ofs{filename};
        settings.Save(ofs);
        AVIDA_SIGNAL(OnConfigWrite(ofs));  // Set up any traits in the phenotype.
        Exit();
      }, "Generate a config file with the provided name (default: Avida.cfg) and stop", 'g', /*max_args=*/ 1);

    // Allow plug-ins to register anything that they need to...
    AVIDA_SIGNAL(RegisterTraits());    // Set up any traits in the phenotype.
    AVIDA_SIGNAL(RegisterSettings());  // Set up parameters for the config file.
    AVIDA_SIGNAL(RegisterCallbacks()); // Set up new instructions for the instruction set.
  }
  Avida(emp::vector<emp::String> args) : Avida() { settings.LoadArgs(args); }
  Avida(const Avida &) = delete;
  Avida(Avida &&) = delete;
  Avida & operator=(const Avida &) = delete;
  Avida & operator=(Avida &&) = delete;

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
  [[nodiscard]] org_ref_t GetOrgRef(size_t id) const { return org_ref_t{biota, id}; }
  [[nodiscard]] org_set_t GetActiveOrgSet() const { return org_set_t::All(biota); }

  [[nodiscard]] const query_manager_t & GetQueryManager() const { return query_man; }
  [[nodiscard]] query_manager_t & GetQueryManager() { return query_man; }
  [[nodiscard]] compiled_query_t CompileQuery(const emp::String & query) const {
    return query_man.Compile(query);
  }
  [[nodiscard]] query_value_t EvaluateQuery(const emp::String & query) const {
    return query_man.Evaluate(query);
  }

  /// Convert any query result into a visible representation suitable for the print keyword.
  [[nodiscard]] emp::String FormatQueryValue(const query_value_t & value) const {
    if (value.IsNull()) return "null";
    if (value.IsOrgRef()) {
      const org_ref_t & ref = value.template Get<org_ref_t>();
      if (ref.GetBiota() != &biota || !ref.IsValid()) return "invalid_org";
      return emp::MakeString("org(", ref.GetBiotaID(), ')');
    }
    if (value.IsOrgSet()) {
      const org_set_t & set = value.template Get<org_set_t>();
      if (set.GetBiota() != &biota || !set.IsValid()) return "invalid_org_set";
      return emp::MakeString("org_set(size=", set.GetSize(), ')');
    }
    return value.AsString();
  }

  /// Evaluate and concatenate comma-separated query expressions, followed by one newline.
  void PrintQueries(const emp::vector<emp::String> & tokens, std::ostream & os = std::cout) const {
    if (tokens.empty()) emp::notify::Error("print requires at least one query expression.");

    emp::vector<emp::String> queries;
    emp::String query;
    size_t paren_depth = 0;
    size_t brace_depth = 0;

    const auto store_query = [&]() {
      if (query.empty()) emp::notify::Error("print contains an empty query expression.");
      queries.push_back(std::move(query));
      query.clear();
    };

    for (const emp::String & token : tokens) {
      if (token == "," && paren_depth == 0 && brace_depth == 0) {
        store_query();
        continue;
      }

      if (token == ")" && paren_depth == 0) {
        emp::notify::Error("print contains an unmatched ')'.");
      }
      if (token == "}" && brace_depth == 0) {
        emp::notify::Error("print contains an unmatched '}'.");
      }

      query += token;

      if (token == "(") ++paren_depth;
      else if (token == ")") --paren_depth;
      else if (token == "{") ++brace_depth;
      else if (token == "}") --brace_depth;
    }

    if (paren_depth || brace_depth) {
      emp::notify::Error("print contains an unmatched grouping delimiter.");
    }
    store_query();

    for (const emp::String & source : queries) {
      os << FormatQueryValue(EvaluateQuery(source));
    }
    os << '\n';
  }

  /// Parse and execute one or more SettingsManager commands from an in-memory string.
  void ExecuteCommand(const emp::String & command) {
    if (command.empty()) emp::notify::Error("Cannot execute an empty SettingsManager command.");
    std::istringstream input{command.str() + '\n'};
    settings.Load(input);
  }

  [[nodiscard]] auto & GetFirstOrg(this auto & self) {
    if (self.GetNumOrgs() == 0) emp::notify::Error("Cannot select from an empty population.");
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

  [[nodiscard]] double CalcTraitSum(const emp::String & name) const {
    const auto & trait = GetTrait(name);
    return biota.CalcSum([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
  }

  [[nodiscard]] auto & FindOrg_MinTrait(this auto & self, const emp::String & name) {
    if (self.GetNumOrgs() == 0) emp::notify::Error("Cannot select from an empty population.");
    const auto & trait = self.GetTrait(name);
    const size_t id = self.biota.FindMinimumID([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
    return self.biota[id];
  }

  [[nodiscard]] auto & FindOrg_MaxTrait(this auto & self, const emp::String & name) {
    if (self.GetNumOrgs() == 0) emp::notify::Error("Cannot select from an empty population.");
    const auto & trait = self.GetTrait(name);
    const size_t id = self.biota.FindMaximumID([&trait](const organism_t & org){
      return trait.AsDouble(org);
    });
    return self.biota[id];
  }

  // Find an organism by a description: "<trait>:min", "<trait>:max", ":first", or ":<id>".
  [[nodiscard]] auto & FindOrg(this auto & self, emp::String desc) {
    const emp::String original_desc = desc;
    if (desc.Count(':') != 1) {
      emp::notify::Error("Invalid organism selector '", original_desc,
        "'; expected '<trait>:min', '<trait>:max', ':first', or ':<id>'.");
    }

    const size_t colon_pos = desc.find(':');
    emp::String trait_name = desc.substr(0, colon_pos);
    desc.erase(0, colon_pos + 1);
    if (trait_name.empty()) {
      if (desc == "first") return self.GetFirstOrg();

      // If not ':first' it better be ':<id>'
      if (!desc.OnlyDigits()) {
        emp::notify::Error("Invalid organism selector '", original_desc,
          "'; expected ':first' or a numeric organism ID such as ':1038'.");
      }

      size_t org_id = desc.As<size_t>();
      if (org_id >= self.GetBiotaSize() || !self.IsOccupied(org_id)) {
        emp::notify::Error(
          "Organism selector '", original_desc, "' refers to inactive ID ", org_id, ".");
      }
      return self.GetOrg(org_id);
    }

    if (desc == "max") return self.FindOrg_MaxTrait(trait_name);
    if (desc == "min") return self.FindOrg_MinTrait(trait_name);
    emp::notify::Error(
      "Invalid organism selector '", original_desc, "'; expected '<trait>:min' or '<trait>:max'.");
  }

  // ====== Output Management ======

  // Announce an output column to every output-interface module (file, web, console, ...).
  // The receiver can decide how they want to interpret the return value from fun.
  // (E.g., creating CSV outputs, graphs, etc.)
  template <typename FUN_T>
  void AddOutput(const emp::String & filename, const emp::String & output_name, FUN_T fun) {
    AVIDA_SIGNAL(OnDeclareOutput(filename, output_name, fun));
  }

  // Collect trait information from the active population.
  // Use "mean" value by default.  Trait name can be followed with a specifier:
  //   trait:min   -- Calculate this trait for all organisms and return lowest.
  //   trait:mean  -- Calculate this trait for all organisms and return average.
  //   trait:max   -- Calculate this trait for all organisms and return highest.
  //   trait:sum   -- Calculate this trait for all organisms and return total.
  //   trait:first -- Calculate this trait for only the first organism and it.
  void AddOutputTrait(const emp::String & filename,
                      const emp::String & output_name,
                      emp::String trait_info) {
    emp::String trait = trait_info.Pop(':');
    if (trait_info.empty() || trait_info == "mean" || trait_info == "ave") {
      AddOutput(filename, output_name, [this, trait](){ return CalcTraitAve(trait); });
    } else if (trait_info == "min") {
      AddOutput(filename, output_name, [this, trait](){ return CalcTraitMin(trait); });
    } else if (trait_info == "max") {
      AddOutput(filename, output_name, [this, trait](){ return CalcTraitMax(trait); });
    } else if (trait_info == "sum") {
      AddOutput(filename, output_name, [this, trait](){ return CalcTraitSum(trait); });
    } else if (trait_info == "first") {
      AddOutput(filename, output_name, [this, trait](){
        return GetTrait(trait).AsString( GetFirstOrg() );
      });
    } else {
      emp::notify::Error("Unknown stat '", trait_info, "' for trait '", trait, "'.");
    }
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

  template <typename T>
  void AddSetting(const emp::String & name, T & value, emp::String desc,
                  char flag = '\0', emp::String default_val = "") {
    settings.AddSetting(name, value, std::move(desc), flag, std::move(default_val));
    using value_t = std::remove_cvref_t<T>;
    using query_t = std::conditional_t<std::same_as<value_t, std::string>, emp::String, value_t>;
    query_man.RegisterValue(name, [this, name](){ return settings.template Get<query_t>(name); });
  }

  template <typename GETTER_T, typename SETTER_T,
            typename VALUE_T = std::remove_cvref_t<std::invoke_result_t<GETTER_T>>>
    requires std::invocable<GETTER_T>
      && std::invocable<SETTER_T, VALUE_T>
  void AddSetting(const emp::String & name, GETTER_T getter, SETTER_T setter,
                  emp::String desc, char flag = '\0', emp::String default_val = "") {
    settings.AddSetting(
      name, std::move(getter), std::move(setter), std::move(desc), flag, std::move(default_val)
    );
    using query_t = std::conditional_t<std::same_as<VALUE_T, std::string>, emp::String, VALUE_T>;
    query_man.RegisterValue(name, [this, name](){ return settings.template Get<query_t>(name); });
  }

  template <typename T>
    requires (!std::invocable<std::remove_cvref_t<T>>)
  void AddValue(const emp::String & name, T && value, emp::String desc = "") {
    settings.AddValue(name, std::forward<T>(value), std::move(desc));
    using value_t = std::conditional_t<
      std::is_convertible_v<T, const char *>, emp::String, std::remove_cvref_t<T>
    >;
    using query_t = std::conditional_t<std::same_as<value_t, std::string>, emp::String, value_t>;
    query_man.RegisterValue(name, [this, name](){ return settings.template Get<query_t>(name); });
  }

  template <typename GETTER_T,
            typename VALUE_T = std::remove_cvref_t<std::invoke_result_t<GETTER_T>>>
    requires std::invocable<GETTER_T>
  void AddValue(const emp::String & name, GETTER_T getter, emp::String desc = "") {
    settings.AddValue(name, std::move(getter), std::move(desc));
    using query_t = std::conditional_t<std::same_as<VALUE_T, std::string>, emp::String, VALUE_T>;
    query_man.RegisterValue(name, [this, name](){ return settings.template Get<query_t>(name); });
  }

  template <typename GETTER_T, typename SETTER_T,
            typename VALUE_T = std::remove_cvref_t<std::invoke_result_t<GETTER_T>>>
    requires std::invocable<GETTER_T>
      && std::invocable<SETTER_T, VALUE_T>
  void AddValue(const emp::String & name, GETTER_T getter, SETTER_T setter, emp::String desc = "") {
    settings.AddValue(name, std::move(getter), std::move(setter), std::move(desc));
    using query_t = std::conditional_t<std::same_as<VALUE_T, std::string>, emp::String, VALUE_T>;
    query_man.RegisterValue(name, [this, name](){ return settings.template Get<query_t>(name); });
  }
  template <typename... ARG_Ts>
  void AddKeyword(ARG_Ts &&... args) { settings.AddKeyword(std::forward<ARG_Ts>(args)...); }

  template <typename... ARG_Ts>
  void AddOutputKeyword(ARG_Ts &&... args) {
    settings.AddOutputKeyword(std::forward<ARG_Ts>(args)...);
  }

  /// Register a read-only value that can be accessed from an organism query reference.
  template <typename GETTER_T>
    requires std::invocable<GETTER_T, const organism_t &>
  void RegisterOrganismProperty(const emp::String & name, GETTER_T getter) {
    query_man.RegisterOrganismProperty(name, std::move(getter));
  }

  void AddCallback(const emp::String & name, std::function<void(size_t)> callback) {
    AVIDA_SIGNAL( AddCallback(name, callback) );
  }

  template <typename TRAIT_T>
  void RegisterTrait(const emp::String & name, const emp::String & desc,
                     const emp::String & filename, size_t line,
                     auto get_fun, auto cget_fun)
  {
    trait_man.template Register<TRAIT_T>(name, desc, filename, line, get_fun, cget_fun);
    query_man.template RegisterTrait<TRAIT_T>(name, std::move(cget_fun));
  }

  size_t RegisterTask(const emp::String & name) {
    if (task_ids.contains(name)) {
      emp::notify::Error("Duplicate task registration for '", name, "'.");
    }
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
    auto it = task_ids.find(name);
    if (it == task_ids.end()) emp::notify::Error("Requesting unknown task '", name, "'.");
    return it->second;
  }

  // Look up the name of a task by its ID.
  [[nodiscard]] const emp::String & GetTaskName(size_t task_id) const {
    if (task_id >= task_names.size()) emp::notify::Error("Invalid task ID ", task_id, ".");
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

  // Broadcast an output produced by isolated analysis hardware.  No mutable reference to the
  // source organism is provided; a responder must explicitly reach into Avida to alter live state.
  template <typename HARDWARE_T>
  void SignalAnalyzeOutput(HARDWARE_T & hardware, uint32_t output) {
    AVIDA_SIGNAL(OnAnalyzeOutput(hardware, output));
  }

  // ====== Organism Management ======

  organism_t & Inject(organism_t & inject_org) {
    inject_org.ResetHardware();                 // Reset organism as placed into pop.
    AVIDA_SIGNAL(OnInjectReady(inject_org));    // Trigger for injections only
    AVIDA_SIGNAL(BeforePlacement(inject_org));  // Trigger to set up organisms for activation
    AVIDA_SIGNAL(OnPlacement(inject_org));      // Trigger to activate organism in populations
    return inject_org;
  }

  /// Finish setting up an isolated organism for tracing or other analyses.  Analysis setup has its
  /// own signal so modules can initialize organism-local state without placement, logging, or
  /// population side effects.  A copy of the central RNG provides reproducible inputs without
  /// advancing the live run's random-number stream.
  void SetupAnalysisOrganism(organism_t & analysis_org) {
    emp_always_assert(
      !analysis_org.HasLiveBiotaID(),
      "SetupAnalysisOrganism cannot be used on an organism in the live Biota."
    );
    emp::Random analysis_random = random;
    AVIDA_SIGNAL(OnAnalysisOrganism(analysis_org, analysis_random));
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

  /// Test if an offspring genome is okay to turn into a new organism.
  bool TestOffspringGenome(const organism_t & parent, const genome_t & genome) const {
    return genome.size() > 0 && AVIDA_TEST(TestOffspringGenome(parent, genome));
  }

  /// Collect and place an offspring from a designated parent organism.
  void DivideOrg(size_t parent_id) {
    organism_t & parent = biota[parent_id];
    genome_t offspring_genome = GetOffspringGenome(parent);
    if (TestOffspringGenome(parent, offspring_genome)) {
      organism_t & offspring = BuildOffspring(parent, std::move(offspring_genome));
      PlaceOffspring(offspring);
    }
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

  template <typename... Ts>
  bool TriggerTests(Ts &&... args) {
    return plug_ins.TriggerTests(std::forward<Ts>(args)...);
  }

  template <typename... Ts>
  bool TriggerTests(Ts &&... args) const {
    return plug_ins.TriggerTests(std::forward<Ts>(args)...);
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

  /// Trace a fresh analysis organism built from a genome.
  void TraceGenome(const genome_t & genome, uint32_t num_cycles, std::ostream & os = std::cout) {
    organism_t analysis_org(genome);
    SetupAnalysisOrganism(analysis_org);
    auto analysis_hardware = analysis_org.Hardware().MakeAnalysisCopy();
    analysis_hardware.Trace(num_cycles, os);
  }

  /// Trace an organism's genome from the beginning of execution.
  void TraceOrg(size_t id, uint32_t num_cycles, std::ostream & os = std::cout) {
    emp_assert(biota.IsActive(id));
    TraceGenome(biota[id].GetGenome(), num_cycles, os);
  }

  /// Trace an organism forward from its current hardware state.
  void TraceOrgForward(size_t id, uint32_t num_cycles, std::ostream & os = std::cout) {
    emp_assert(biota.IsActive(id));
    auto analysis_hardware = biota[id].Hardware().MakeAnalysisCopy();
    analysis_hardware.Trace(num_cycles, os);
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
    // Validate all configured cross-module names and cache their resolved IDs/accessors before
    // reservation, output setup, population creation, or random-number use.
    AVIDA_SIGNAL(ValidateConfig());
    if (run_state != RunState::INITIALIZING) return;

    auto reserve_counts = AVIDA_COLLECT(size_t, GetOrgReserveCount());
    auto reserve_total = std::accumulate(reserve_counts.begin(), reserve_counts.end(), size_t{0});
    biota.Reserve(reserve_total + 1);
    SetupDataDir();
    AddOutput("stats.csv", "Organism Count", [this](){ return GetNumOrgs(); });
    AddOutput("stats.csv", "Organism Total", [this](){ return GetTotalOrgs(); });
    AddOutput("stats.csv", "Average Genome Length", [this](){
      return biota.CalcAverage([](const organism_t & org){ return org.GetGenome().size(); });
    });
    AddOutput(">", "PopSize", [this](){ return GetNumOrgs(); });
    AddOutput(">", "\n    First Genome", [this](){ return std::format("[{}]", GetFirstOrg().GetGenomeSequence()); });

    // Phase 1: configure modules and declare outputs without assuming a population exists.
    AVIDA_SIGNAL(BeforeStart());
    if (run_state != RunState::INITIALIZING) return;

    settings.PrintStatus();

    // Phase 2: create and inject the initial population.  Modules must not read the population
    // here since other OnStart listeners may not have run yet.
    AVIDA_SIGNAL(OnStart());
    if (run_state != RunState::INITIALIZING) return;

    if (GetNumOrgs() == 0) emp::notify::Error("Avida initialization produced no organisms.");

    // Phase 3: the complete initial population is now available to all modules.
    AVIDA_SIGNAL(OnPopulationReady());
  }

  void Run() {
    emp_assert(run_state != RunState::COMPLETE, "Run() should not be called on finished run.");
    if (run_state == RunState::INITIALIZING) Initialize();
    if (run_state < RunState::EXITING) {
      run_state = RunState::RUNNING;
      while (run_state == RunState::RUNNING) DoUpdate();
    }
    if (run_state == RunState::EXITING) Shutdown();
  }

  // Request that the run stop.  Teardown is deferred to Shutdown() (called once the current
  // update finishes) so the final update's signal handlers still see a populated biota.
  void Exit() {
    if (run_state != RunState::COMPLETE) run_state = RunState::EXITING;
  }

  // End-of-run teardown.  Idempotent: the body runs at most once.
  void Shutdown() {
    if (run_state == RunState::COMPLETE) return;
    run_state = RunState::COMPLETE;
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
