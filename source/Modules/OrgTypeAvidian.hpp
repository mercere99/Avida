#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Track the current generation of each organism in the population.
 */

#include <cstddef>   // for size_t
#include <expected>
#include <iostream>
#include <optional>

#include "../core/Avida.hpp"
#include "../Hardware/AvidaVM.hpp"

template <typename AVIDA_T>
class OrgTypeAvidian : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  AvidaVM::inst_set_t inst_set;           // Live instruction set (used during evolution).
  AvidaVM::inst_set_t analysis_inst_set;  // Parallel set for isolated tracing/analysis.
  double offspring_size_range = 2.0;      // Offspring genome must be within this factor of parent size.
  size_t trace_cycles = 200;              // Number of CPU cycles to show in a requested trace.

  [[nodiscard]] std::expected<AvidaVM::genome_t, emp::String>
  ParseGenomeSequence(emp::String sequence) const {
    if (sequence.IsLiteralString("\"'")) sequence = sequence.ConvertStringFromLiteral("\"'");
    if (sequence.empty()) return std::unexpected("Genome sequence cannot be empty.");

    for (size_t pos = 0; pos < sequence.size(); ++pos) {
      if (inst_set.GetID(sequence[pos]) == AvidaVM::inst_set_t::NULL_ID) {
        return std::unexpected(emp::MakeString(
          "Unknown instruction symbol ", sequence[pos], " at position ", pos, "."
        ));
      }
    }
    return inst_set.BuildGenome(sequence);
  }

  [[nodiscard]] std::optional<size_t>
  ResolveTraceOrg(const emp::vector<emp::String> & args, const emp::String & command) {
    if (args.empty()) emp::notify::Error(command, " requires an organism query.");

    emp::String query = args[0];
    for (size_t i = 1; i < args.size(); ++i) query += args[i];

    const auto compiled_query = avida.CompileQuery(query);
    if (compiled_query.GetType() != QueryValueType::ORG_REF) {
      emp::notify::Error(
        command, " query ", query.AsLiteral(), " does not return an organism."
      );
    }

    const auto result = compiled_query.Evaluate();
    if (result.IsNull()) {
      emp::notify::Warning(
        command, " query ", query.AsLiteral(),
        " did not select a valid organism; skipping trace."
      );
      return std::nullopt;
    }
    const auto & ref = result.template Get<typename AVIDA_T::org_ref_t>();
    if (ref.GetBiota() != &avida.GetBiota() || !ref.IsValid()) {
      emp::notify::Warning(
        command, " query ", query.AsLiteral(),
        " did not select a valid organism; skipping trace."
      );
      return std::nullopt;
    }

    return ref.GetBiotaID();
  }

  void TraceQuery(const emp::vector<emp::String> & args, std::ostream & os) {
    const auto ref = ResolveTraceOrg(args, "trace");
    if (!ref) return;

    std::println(os,
      "Tracing genome from organism {} from the beginning for {} CPU cycles:",
      *ref, trace_cycles
    );
    avida.TraceOrg(*ref, trace_cycles, os);
  }

  void TraceForwardQuery(const emp::vector<emp::String> & args, std::ostream & os) {
    const auto ref = ResolveTraceOrg(args, "trace_forward");
    if (!ref) return;

    std::println(os,
      "Tracing organism {} forward from its current state for {} CPU cycles:",
      *ref, trace_cycles
    );
    avida.TraceOrgForward(*ref, trace_cycles, os);
  }

  void TraceGenomeSequence(const emp::vector<emp::String> & args, std::ostream & os) {
    if (args.empty()) emp::notify::Error("trace_genome requires a genome sequence.");

    emp::String sequence = args[0];
    for (size_t i = 1; i < args.size(); ++i) sequence += args[i];

    auto genome = ParseGenomeSequence(sequence);
    if (!genome) {
      emp::notify::Error("Cannot trace genome ", sequence.AsLiteral(), ": ", genome.error());
      return;
    }

    std::println(os,
      "Tracing genome {} from the beginning for {} CPU cycles:",
      inst_set.ToSequence(*genome).AsLiteral(), trace_cycles
    );
    avida.TraceGenome(*genome, trace_cycles, os);
  }

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
    avida.AddSetting(
      "AvidaGP.trace_cycles", trace_cycles,
      "Number of CPU cycles printed by the trace command."
    );
    avida.GetSettings().Metadata("AvidaGP.offspring_size_range").AddTag("advanced");
    avida.GetSettings().Metadata("AvidaGP.trace_cycles")
      .AddTag("advanced")
      .AddTag("local only");
    avida.AddOutputKeyword(
      "trace",
      [this](const emp::vector<emp::String> & args, std::ostream & os){ TraceQuery(args, os); },
      "Trace an organism's genome from the beginning: trace <organism_query>; "
      "redirect with > filename or >> filename"
    );
    avida.AddOutputKeyword(
      "trace_genome",
      [this](const emp::vector<emp::String> & args, std::ostream & os){
        TraceGenomeSequence(args, os);
      },
      "Trace an instruction sequence from the beginning: trace_genome <sequence>; "
      "redirect with > filename or >> filename"
    );
    avida.AddOutputKeyword(
      "trace_forward",
      [this](const emp::vector<emp::String> & args, std::ostream & os){
        TraceForwardQuery(args, os);
      },
      "Trace an organism forward from its current state: trace_forward <organism_query>; "
      "redirect with > filename or >> filename"
    );
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
      [this](AvidaVM & hardware){
        avida.SignalAnalyzeOutput(hardware, hardware.GetOutput());
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
    static std::array<std::function<void(AvidaVM &)>, MAX_CALLBACKS> callbacks;
    emp_assert(id < GetNumCallbacks());
    return callbacks[id];
  }

  // Simple template functions that forward to the std::function in the matching storage array.
  // These let a plain function pointer (not a std::function) be stored in the InstSet.
  template <size_t ID>
  static void DoCallback(AvidaVM & vm) {
    emp_always_assert(vm.HasLiveBiotaID(), "Live callback invoked by a VM outside the Biota.");
    GetCallbackStorage(ID)(vm.GetBiotaID());
  }
  template <size_t ID>
  static void DoAnalysisCallback(AvidaVM & vm) {
    emp_always_assert(vm.IsAnalysis(), "Analysis callback invoked by a live VM.");
    GetAnalysisCallbackStorage(ID)(vm);
  }

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
                   const std::function<void(AvidaVM &)> & analysis_fun = [](AvidaVM &){}) {
    emp_always_assert(GetNumCallbacks() < MAX_CALLBACKS, "Too many callbacks; failed to add '", name, "'");
    const size_t id = GetNumCallbacks()++;  // Claim slot and increment before GetCallbackStorage.
    GetCallbackStorage(id) = callback;
    GetAnalysisCallbackStorage(id) = analysis_fun;
    AvidaVM::AddCallback(inst_set, name, GetRedirectTable()[id]);
    AvidaVM::AddCallback(analysis_inst_set, name, GetAnalysisRedirectTable()[id]);
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  void SetupHardware(ORG_T & org) {
    org.GetPhenotype().hardware.SetInstSet(inst_set);
    org.GetPhenotype().hardware.SetAnalysisInstSet(analysis_inst_set);
  }

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    SetupHardware(org);
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & /*parent*/) {
    SetupHardware(offspring);
  }

  template <concepts::Organism ORG_T>
  void OnAnalysisOrganism(ORG_T & org, emp::Random & /* analysis_random */) {
    SetupHardware(org);
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
