#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Creates and routes signals to all of the active module plug-ins.
 */

#include "emp/base/vector.hpp"
#include "emp/tools/String.hpp"

// Template arguments: Finalized Avida type and Finalized plug-in module types.
template <typename AVIDA_T, typename... PLUG_IN_Ts>
class PlugInManager {
private:
  static_assert(sizeof...(PLUG_IN_Ts) > 0, "At least one plug-in required to manage run.");
  std::tuple<PLUG_IN_Ts...> plug_ins;

  // Run a function on a module if allowed.
  template <typename FUN_T, typename MODULE_T>
  static void TryInvoke(FUN_T & fun, MODULE_T & module) {
    if constexpr (requires { fun(module); }) fun(module);
  }

  // If a function can be run on a module, trigger an action on that module.
  template <typename FUN_T, typename MODULE_T, typename ACTION_T>
  static bool IfInvocable(FUN_T & fun, MODULE_T & module, ACTION_T action) {
    if constexpr (requires { fun(module); }) action(module);
    return requires { fun(module); };
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
    return TriggerCollector<emp::String>([](auto & module){ return module.GetName(); });
  }

  // Get the types of all of the plug-ins
  emp::vector<emp::String> GetTypes() {
    return TriggerCollector<emp::String>([](auto & module){ return module.GetType(); });
  }

  // Call Serialize on ALL plug-ins; function is required to avoid accidental failures to save.
  void Serialize(emp::SerialPod & pod) {
    std::apply([&pod](auto &... p){ (pod(p), ...); }, plug_ins);
  }

  // Trigger a specified signal on each module that can respond.
  #define AVIDA_SIGNAL(...)                                                    \
    this->TriggerSignal([&]<typename AVIDA_MODULE_T>(AVIDA_MODULE_T & module)  \
        requires requires { module.__VA_ARGS__; }                              \
    { module.__VA_ARGS__; })

  #define AVIDA_HANDLE(RETURN_TYPE, ...)                                                     \
    this->TriggerHandler<RETURN_TYPE>([&]<typename AVIDA_MODULE_T>(AVIDA_MODULE_T & module)  \
        requires requires { module.__VA_ARGS__; }                                            \
    { return module.__VA_ARGS__; })

  #define AVIDA_COLLECT(RETURN_TYPE, ...)                                                      \
    this->TriggerCollector<RETURN_TYPE>([&]<typename AVIDA_MODULE_T>(AVIDA_MODULE_T & module)  \
        requires requires { module.__VA_ARGS__; }                                              \
    { return module.__VA_ARGS__; })

  // Run the provided function on every module that it is allowed to run on.
  template <typename FUN_T>
  void TriggerSignal(FUN_T && fun) {
    std::apply([&fun](auto &... module) {
      (TryInvoke(fun, module), ...);
    }, plug_ins);
  }
    
  /// Run the provided function until a module returns the expected value.
  /// @return std::expected of the first value found or unexpected.
  template <typename RETURN_T, typename FUN_T>
  auto TriggerHandler(FUN_T && fun) {
    std::expected<RETURN_T, emp::String> result = std::unexpected("No handler found");
    std::apply([&](auto &... module) {
      (IfInvocable(fun, module, [&](auto & module){ result = fun(module); }) || ...);
    }, plug_ins);
    return result;
  }
    
  /// Run the provided function on every module that it is allowed to run on.
  /// @return A vector of all of the collected return values.
  template <typename VALUE_T, typename FUN_T>
  emp::vector<VALUE_T> TriggerCollector(FUN_T && fun) {
    emp::vector<VALUE_T> result{};
    std::apply([&](auto &... module) {
      (IfInvocable(fun, module, [&](auto & module){ result.push_back(fun(module)); }), ...);
    }, plug_ins);

    return result;
  }

  template <typename FUN_T>
  size_t CountSignal(FUN_T && fun) {
    return std::apply([&fun](auto &... module) {
      return (requires { fun(module); } + ...);
    }, plug_ins);
  }
};
