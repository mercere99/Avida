#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Handler for all of the active plug-ins.
 */


// Template arguments: Finalized Avida type and Finalized plug-in module types.
template <typename AVIDA_T, typename... PLUG_IN_Ts>
class PlugInManager {
private:
  static_assert(sizeof...(PLUG_IN_Ts) > 0, "At least one plug-in required to manage run.");

  using organism_t = typename AVIDA_T::organism_t;
  using genome_t   = typename AVIDA_T::genome_t;

  std::tuple<PLUG_IN_Ts...> plug_ins;

// === Signals ===

  // Pass all plug-ins through signal_fun, ignoring returns.
  void TriggerSignal(auto signal_fun) {
    std::apply([&signal_fun](auto &... p){ (signal_fun(p), ...); }, plug_ins);
  }

  // Pass all plug-ins through handler_fun, returning the first non-null returned.
  template <typename T>
  std::expected<T, emp::String> TriggerHandler(auto handler_fun) {
    std::expected<T, emp::String> result = std::unexpected<emp::String>("No handler");
    std::apply([&](auto &... p){
      ((result = handler_fun(p), result.has_value()) || ...);
    }, plug_ins);
    return result;
  }

  size_t CountResult(auto check_fun) const {
    size_t count = 0;
    std::apply([&](auto &... p){ ((count += check_fun(p)), ...); }, plug_ins);
    return count;
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

  // "Signal" generator macro.  Signals are sent out to ALL modules listening for them,
  // which they do by implementing a function with the same signature as the signal.
  #define AVIDA_SIGNAL_DEF(TRIGGER, ...)                                 \
    TriggerSignal([__VA_ARGS__](auto & plug_in) {                        \
      if constexpr (requires { plug_in.TRIGGER; }) { plug_in.TRIGGER; }  \
    });

  // "Handler" generator macro.  Handlers are similar to signals, but return std::expected.
  // As soon as the "expected" has a proper value, it is returned and no more handlers are called.
  #define AVIDA_HANDLER_DEF(RETURN_TYPE, TRIGGER, ...)                          \
    TriggerHandler<RETURN_TYPE>([__VA_ARGS__](auto & plug_in) {                 \
      if constexpr (requires { plug_in.TRIGGER; }) { return plug_in.TRIGGER; }  \
      else return std::unexpected<emp::String>("No Handler");                   \
    })

  // Generate a function to count how many plug-ins respond to a given signal.
  #define AVIDA_ADD_SIGNAL_COUNT(TRIGGER, ...)                                         \
    size_t Count_ ## TRIGGER() const {                                                 \
      return CountResult([](auto & plug_in) -> size_t {                                \
        using nc_t = std::remove_const_t<std::remove_reference_t<decltype(plug_in)>>;  \
        return requires(nc_t & p) { p.TRIGGER(__VA_ARGS__); };                         \
      });                                                                              \
    }

  // Create Count_AddCallback()
  AVIDA_ADD_SIGNAL_COUNT(AddCallback, "name", [](size_t){}) // Generates Count_AddCallback()

  // ======== SIGNALS ========

  void AddCallback(const emp::String & name, std::function<void(size_t)> fun) {
    emp_always_assert(Count_AddCallback() > 0, "AddCallback() triggered, but no plug-in modules listening");
    AVIDA_SIGNAL_DEF(AddCallback(name, fun), name, fun);
  }

  void OnUpdateStart(size_t update) { AVIDA_SIGNAL_DEF(OnUpdateStart(update), update); }
  void OnUpdate(size_t update)      { AVIDA_SIGNAL_DEF(OnUpdate(update), update); }
  void OnUpdateEnd(size_t update)   { AVIDA_SIGNAL_DEF(OnUpdateEnd(update), update); }

  void BeforeRepro(organism_t & parent) { AVIDA_SIGNAL_DEF(BeforeRepro(parent), &parent); }

  void OnOffspringInit(organism_t & offspring, organism_t & parent) {
    AVIDA_SIGNAL_DEF(OnOffspringInit(offspring, parent), &offspring, &parent);
  }

  void OnOffspringReady(organism_t & offspring, organism_t & parent) {
    AVIDA_SIGNAL_DEF(OnOffspringReady(offspring, parent), &offspring, &parent);
  }

  void OnInjectReady(organism_t & org) { AVIDA_SIGNAL_DEF(OnInjectReady(org), &org); }
  void BeforePlacement(organism_t & org) { AVIDA_SIGNAL_DEF(BeforePlacement(org), &org); }
  void OnPlacement(organism_t & org) { AVIDA_SIGNAL_DEF(OnPlacement(org), &org); }
  void BeforeDeath(organism_t & org) { AVIDA_SIGNAL_DEF(BeforeDeath(org), &org); }

  void BeforeExit() { AVIDA_SIGNAL_DEF(BeforeExit(), ); }
  void OnHelp() { AVIDA_SIGNAL_DEF(OnHelp(), ); }

  // Start-up sequence
  void RegisterTraits() { AVIDA_SIGNAL_DEF(RegisterTraits(), ); }
  void RegisterSettings() { AVIDA_SIGNAL_DEF(RegisterSettings(), ); }
  void RegisterCallbacks() { AVIDA_SIGNAL_DEF(RegisterCallbacks(), ); }
  void BeforeStart() { AVIDA_SIGNAL_DEF(BeforeStart(), ); }
  void OnStart() { AVIDA_SIGNAL_DEF(OnStart(), ); }

  // ======== HANDLERS ========

  std::expected<genome_t, emp::String> LoadGenome(const std::filesystem::path & filepath) {
    return AVIDA_HANDLER_DEF(genome_t, LoadGenome(filepath), &filepath);
  }

  // ======== Other Data ========

  size_t CountReservedOrgs() {
    return CountResult([](auto & plug_in) -> size_t {
      using nc_t = std::remove_const_t<std::remove_reference_t<decltype(plug_in)>>;
      if constexpr (requires(nc_t & p) { p.GetOrgReserveCount(); }) {
        return plug_in.GetOrgReserveCount();
      } else return 0;
    });
  }
};
