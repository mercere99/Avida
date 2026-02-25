#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Handler for all of the active plug-ins.
 */


// Template arguments: Finalized Avida type and Finalized plug-in module types.
template <typename AVIDA_T, typename... PLUG_IN_Ts>
class PlugInManager {
private:
  using organism_t = typename AVIDA_T::organism_t;

  std::tuple<PLUG_IN_Ts...> plug_ins;

  // === Signals ===
  void TriggerSignal(auto signal_fun) {
    std::apply([&signal_fun](auto &... p){ (signal_fun(p), ...); }, plug_ins);
  }

public:
  PlugInManager(AVIDA_T & avida) : plug_ins(PLUG_IN_Ts{avida}...) { }

  // Accessor to individual plug-ins by realized type.
  template <typename PLUG_IN_T>
  [[nodiscard]] auto & Get() { return std::get<PLUG_IN_T>(plug_ins); }

  // Accessor to individual plug-ins by template type.
  template <template <typename> typename PLUG_IN_T>
  [[nodiscard]] auto & Get() { return std::get<PLUG_IN_T<AVIDA_T>>(plug_ins); }

  // Accessor to individual plug-ins by template type.
  template <size_t INDEX>
  [[nodiscard]] auto & Get() { return std::get<INDEX>(plug_ins); }

  // Get the names of all of the plug-ins
  emp::vector<emp::String> GetNames() {
    emp::vector<emp::String> out;
    std::apply([&out](auto &... p){ out = { p.GetName()... }; }, plug_ins);
    return out;
  }

  // Get the types of all of the plug-ins
  emp::vector<emp::String> GetTypes() {
    emp::vector<emp::String> out;
    std::apply([&out](auto &... p){ out = { p.GetType()... }; }, plug_ins);
    return out;
  }

  // Build out all of the individual signals.

  // Start with a signal generator macro.
  #define AVIDA_SIGNAL_DEF(TRIGGER, ...)                                 \
    TriggerSignal([__VA_ARGS__](auto & plug_in) {                        \
      if constexpr (requires { plug_in.TRIGGER; }) { plug_in.TRIGGER; }  \
    });

  void OnUpdateStart(size_t update) { AVIDA_SIGNAL_DEF(OnUpdateStart(update), update); }
  void OnUpdateEnd(size_t update) { AVIDA_SIGNAL_DEF(OnUpdateEnd(update), update); }

  void BeforeRepro(organism_t & parent) { AVIDA_SIGNAL_DEF(BeforeRepro(parent), &parent); }

  void OnOffspringReady(organism_t & offspring, organism_t & parent) {
    AVIDA_SIGNAL_DEF(OnOffspringReady(offspring, parent), &offspring, &parent);
  }

  void OnInjectReady(organism_t & org) { AVIDA_SIGNAL_DEF(OnInjectReady(org), &org); }
  void BeforePlacement(organism_t & org) { AVIDA_SIGNAL_DEF(BeforePlacement(org), &org); }
  void OnPlacement(organism_t & org) { AVIDA_SIGNAL_DEF(OnPlacement(org), &org); }
  void BeforeDeath(organism_t & org) { AVIDA_SIGNAL_DEF(BeforeDeath(org), &org); }

  // @CAO TODO: Call these
  void BeforeMutate(organism_t & org) { AVIDA_SIGNAL_DEF(BeforeMutate(org), &org); }
  void OnMutate(organism_t & org) { AVIDA_SIGNAL_DEF(OnMutate(org), &org); }

  void BeforeExit() { AVIDA_SIGNAL_DEF(BeforeExit(), ); }
  void OnHelp() { AVIDA_SIGNAL_DEF(OnHelp(), ); }
  
};
