#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current generation of each organism in the population.
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"
#include "../Hardware/AvidaVM.hpp"

template <typename AVIDA_T>
class OrgTypeAvidian : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  AvidaVM::inst_set_t inst_set;           // Live instruction set (used during evolution).
  AvidaVM::inst_set_t analysis_inst_set;  // Parallel set for isolated tracing/analysis.
  double offspring_size_range = 2.0;  // Offspring genome must be within this factor of parent size.

public:
  OrgTypeAvidian(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "OrgTypeAvidian", "Representation",
        "Organism based on AvidaVM hardware.") {}
  ~OrgTypeAvidian() {}

  void Serialize(emp::SerialPod & /* pod */) {
    // Nothing extra to serialize; inst_set should be rebuilt correctly.
  }

  // === Phenotypic Traits ===

  struct Phenotype {
    AvidaVM hardware;  // Hardware run by this organism.
  };

  struct GlobalTypes {
    using hardware_t = AvidaVM;
    using genome_t   = AvidaVM::genome_t;
    using inst_set_t = AvidaVM::inst_set_t;
  };

  void RegisterSettings() {
    avida.AddSetting("AvidaGP.offspring_size_range", offspring_size_range,
      "Offspring genome size must be within this factor of the parent's (2.0 = half to double); "
      "divides outside the range fail.");
  }

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(hardware, "Virtual CPU for this organism");
  }

  void RegisterCallbacks() {
    // Build the standard AvidaVM instructions into BOTH sets.  They are built in lockstep (same
    // calls, same order), so every instruction -- including the callbacks added below -- gets an
    // identical ID in each set, and a genome decodes the same way under either.
    AvidaVM::BuildInstSet(inst_set);
    AvidaVM::BuildInstSet(analysis_inst_set);
    // Extract the (hardware-specific) output here, then let Avida broadcast it.
    // AVIDA_SIGNAL is kept in the Avida class: GCC 15 mis-evaluates its requires-clause
    // when expanded inside a stored callback lambda, silently dropping all responders.
    // The analysis variant routes through SignalAnalyzeOutput instead, so tracing/analysis can
    // still see which tasks an organism performs without rewarding it or perturbing the population.
    AddCallback("Output",
      [this](size_t biota_id){
        auto & org = avida.GetOrg(biota_id);
        avida.SignalOutput(org, org.Hardware().GetOutput());
      },
      [this](size_t biota_id){
        auto & org = avida.GetOrg(biota_id);
        avida.SignalAnalyzeOutput(org, org.Hardware().GetOutput());
      });
  }

  static constexpr size_t MAX_CALLBACKS = 32;  // Max number of callback instructions allowed.

  // Number of callback slots assigned so far (shared across all instances).
  [[nodiscard]] static size_t & GetNumCallbacks() {
    static size_t num_callbacks = 0;
    return num_callbacks;
  }

  // Callback functions stored by slot index (used during evolution).
  [[nodiscard]] static auto & GetCallbackStorage(size_t id) {
    static std::array<std::function<void(size_t)>, MAX_CALLBACKS> callbacks;
    emp_assert(id < GetNumCallbacks());
    return callbacks[id];
  }

  // Analysis-variant callbacks, stored by the SAME slot index (used only while tracing/analyzing).
  // Kept in a parallel array -- rather than a second field per slot -- so the evolutionary hot
  // path (GetCallbackStorage) is byte-for-byte unchanged and never touches this table.
  [[nodiscard]] static auto & GetAnalysisCallbackStorage(size_t id) {
    static std::array<std::function<void(size_t)>, MAX_CALLBACKS> callbacks;
    emp_assert(id < GetNumCallbacks());
    return callbacks[id];
  }

  // Simple template functions that forward to the std::function in the matching storage array.
  // These let a plain function pointer (not a std::function) be stored in the InstSet.
  template <size_t ID>
  static void DoCallback(AvidaVM & vm) { GetCallbackStorage(ID)(vm.GetBiotaID()); }
  template <size_t ID>
  static void DoAnalysisCallback(AvidaVM & vm) { GetAnalysisCallbackStorage(ID)(vm.GetBiotaID()); }

  // Pre-built tables of all MAX_CALLBACKS possible redirects, one per storage array.  Indexed by
  // slot number so AddCallback can look up the right trampoline in O(1).  Slot IDs stay aligned
  // across the two tables, so the live and analysis inst_sets agree on instruction IDs.
  [[nodiscard]] static const auto & GetRedirectTable() {
    static const auto table = []<size_t... Is>(std::index_sequence<Is...>) {
      return std::array<AvidaVM::callback_t, MAX_CALLBACKS>{ DoCallback<Is>... };
    }(std::make_index_sequence<MAX_CALLBACKS>{});
    return table;
  }
  [[nodiscard]] static const auto & GetAnalysisRedirectTable() {
    static const auto table = []<size_t... Is>(std::index_sequence<Is...>) {
      return std::array<AvidaVM::callback_t, MAX_CALLBACKS>{ DoAnalysisCallback<Is>... };
    }(std::make_index_sequence<MAX_CALLBACKS>{});
    return table;
  }

  // Register a new callback instruction.  `callback` runs during normal evolution; `analysis_fun`
  // runs instead while tracing/analyzing (default: a neutral no-op, so a callback that does not
  // opt in can never perturb the population during analysis).  Both variants share one slot and
  // are added to their respective inst_sets in lockstep, keeping the two sets' IDs aligned.
  void AddCallback(const emp::String & name,
                   const std::function<void(size_t)> & callback,
                   const std::function<void(size_t)> & analysis_fun = [](size_t){}) {
    emp_always_assert(GetNumCallbacks() < MAX_CALLBACKS, "Too many callbacks; failed to add '", name, "'");
    const size_t id = GetNumCallbacks()++;  // Claim slot and increment before GetCallbackStorage.
    GetCallbackStorage(id) = callback;
    GetAnalysisCallbackStorage(id) = analysis_fun;
    AvidaVM::AddCallback(inst_set, name, GetRedirectTable()[id]);
    AvidaVM::AddCallback(analysis_inst_set, name, GetAnalysisRedirectTable()[id]);
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().hardware.SetInstSet(inst_set);
    org.GetPhenotype().hardware.SetAnalysisInstSet(analysis_inst_set);
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & /*parent*/) {
    offspring.GetPhenotype().hardware.SetInstSet(inst_set);
    offspring.GetPhenotype().hardware.SetAnalysisInstSet(analysis_inst_set);
  }

  template <concepts::Organism ORG_T, concepts::Genome GENOME_T>
  bool TestOffspringGenome(const ORG_T & parent, const GENOME_T & genome) const {
    const size_t parent_size = parent.GetGenome().size();
    return (genome.size() >= parent_size / offspring_size_range) &&
           (genome.size() <= parent_size * offspring_size_range);
  }

  // === Handlers ===

  std::expected<typename GlobalTypes::genome_t, emp::String> LoadGenome(const std::filesystem::path & filepath) {
    return inst_set.LoadGenome(filepath);
  }


};
