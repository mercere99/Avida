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

  template <typename FUN_T, typename MODULE_T>
  static void TryInvoke(FUN_T & fun, MODULE_T & module) {
    if constexpr (requires { fun(module); }) fun(module);
  }

  template <typename FUN_T, typename MODULE_T>
  static constexpr size_t TryCount(FUN_T & fun, MODULE_T & module) {
    if constexpr (requires { fun(module); }) return 1;
    else return 0;
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

  // Call Serialize on ALL plug-ins; function is required to avoid accidental failures to save.
  void Serialize(emp::SerialPod & pod) {
    std::apply([&pod](auto &... p){ (pod(p), ...); }, plug_ins);
  }

  // Trigger a specified signal on each module that can respond.
  #define AVIDA_SIGNAL(...) \
    TriggerSignal([&]<typename AVIDA_MODULE_T_>(AVIDA_MODULE_T_& module) \
        requires requires { module.__VA_ARGS__; } \
    { module.__VA_ARGS__; })
    
  template <typename FUN_T>
  void InvokeSignal(FUN_T && fun) {
    std::apply([&fun](auto &... module) {
      (TryInvoke(fun, module), ...);
    }, plug_ins);
  }

  template <typename FUN_T>
  size_t CountSignal(FUN_T && fun) {
    return std::apply([&fun](auto &... module) -> size_t {
      return (TryCount(fun, module) + ...);
    }, plug_ins);
  }

  // Build out all of the individual signals.

  // "Handler" generator macro.  Handlers are similar to signals, but return std::expected.
  // As soon as the "expected" has a proper value, it is returned and no more handlers are called.
  #define AVIDA_HANDLER_DEF(RETURN_TYPE, TRIGGER, ...)                          \
    TriggerHandler<RETURN_TYPE>([__VA_ARGS__](auto & plug_in) {                 \
      if constexpr (requires { plug_in.TRIGGER; }) { return plug_in.TRIGGER; }  \
      else return std::unexpected<emp::String>("No Handler");                   \
    })


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
