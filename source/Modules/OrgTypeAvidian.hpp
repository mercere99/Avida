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

  AvidaVM::inst_set_t inst_set;

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

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(hardware, "Virtual CPU for this organism");
  }

  void RegisterCallbacks() {
    AvidaVM::BuildInstSet(inst_set);  // Build the standard AvidaVM instruction set.
  }

  static constexpr size_t MAX_CALLBACKS = 32;  // Max number of callback instructions allowed.

  // Number of callback slots assigned so far (shared across all instances).
  [[nodiscard]] static size_t & GetNumCallbacks() {
    static size_t num_callbacks = 0;
    return num_callbacks;
  }

  // Callback functions stored by slot index.
  [[nodiscard]] static auto & GetCallbackStorage(size_t id) {
    static std::array<std::function<void(size_t)>, MAX_CALLBACKS> callbacks;
    emp_assert(id < GetNumCallbacks());
    return callbacks[id];
  }

  // A simple template function that forwards to the std::function in CallbackStorage.
  // This is to allow a plain function pointer that can be stored in InstSet.
  template <size_t ID>
  static void DoCallback(AvidaVM & vm) { GetCallbackStorage(ID)(vm.GetBiotaID()); }

  // Pre-built table of all MAX_CALLBACKS possible redirects.
  // Indexed by slot number so AddCallback can look up the right function in O(1).
  [[nodiscard]] static const auto & GetRedirectTable() {
    static const auto table = []<size_t... Is>(std::index_sequence<Is...>) {
      return std::array<AvidaVM::callback_t, MAX_CALLBACKS>{ DoCallback<Is>... };
    }(std::make_index_sequence<MAX_CALLBACKS>{});
    return table;
  }

  // Register a new callback instruction: store the function, wire up the trampoline, add to inst_set.
  void AddCallback(const emp::String & name, const std::function<void(size_t)> & callback) {
    emp_always_assert(GetNumCallbacks() < MAX_CALLBACKS, "Too many callbacks; failed to add '", name, "'");
    const size_t id = GetNumCallbacks()++;  // Claim slot and increment before GetCallbackStorage.
    GetCallbackStorage(id) = callback;
    AvidaVM::AddCallback(inst_set, name, GetRedirectTable()[id]);
  }

  // === Signal Listeners ===

  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().hardware.SetInstSet(inst_set);
  }

  template <concepts::Organism ORG_T>
  void OnOffspringInit(ORG_T & offspring, ORG_T & /*parent*/) {
    offspring.GetPhenotype().hardware.SetInstSet(inst_set);
  }

  // === Handlers ===

  std::expected<typename GlobalTypes::genome_t, emp::String> LoadGenome(const std::filesystem::path & filepath) {
    return inst_set.LoadGenome(filepath);
  }


};
