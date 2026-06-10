#pragma once

#include "emp/base/concepts.hpp"

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Creates and routes signals to all of the active module plug-ins.
 */


// Template arguments: Finalized Avida type and Finalized plug-in module types.
template <typename AVIDA_T, typename... PLUG_IN_Ts>
class PlugInManager {
private:
  static_assert(sizeof...(PLUG_IN_Ts) > 0, "At least one plug-in required to manage run.");
  std::tuple<PLUG_IN_Ts...> plug_ins;

  // === Signals ===

  // Pass all plug-ins through signal_fun, ignoring returns.
  void TriggerSignal(auto signal_fun) {
    std::apply([&signal_fun](auto &... p){ (signal_fun(p), ...); }, plug_ins);
  }

  size_t CountResult(auto check_fun) const {
    size_t count = 0;
    std::apply([&](auto &... p){ ((count += check_fun(p)), ...); }, plug_ins);
    return count;
  }

  template <typename RETURN_T=void, typename FUN_T, typename MODULE_T>
  static RETURN_T TryInvoke(FUN_T & fun, MODULE_T & module) {
    if constexpr (std::is_void_v<RETURN_T>) {
      if constexpr (requires { fun(module); }) fun(module);
    } else if constexpr (requires { fun(module); }) {
      static_assert(requires { { fun(module) } -> std::convertible_to<RETURN_T>; },
        "Signal handler is callable on this module but its return type is not "
        "convertible to the expected RETURN_T.");
      return static_cast<RETURN_T>(fun(module));
    } else {
      return RETURN_T{};
    }
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
  #define AVIDA_SIGNAL(...)                                                    \
    TriggerSignal<void>([&]<typename AVIDA_MODULE_T>(AVIDA_MODULE_T & module)  \
        requires requires { module.__VA_ARGS__; }                              \
    { module.__VA_ARGS__; })

  #define AVIDA_HANDLE(RETURN_TYPE, ...)                                              \
    TriggerSignal<RETURN_TYPE>([&]<typename AVIDA_MODULE_T>(AVIDA_MODULE_T & module)  \
        requires requires { module.__VA_ARGS__; }                                     \
    { return module.__VA_ARGS__; })

  // Run the provided function on every module that it is allowed to run on.
  template <typename RETURN_T=void, typename FUN_T>
  RETURN_T InvokeSignal(FUN_T && fun) {
    // VOID return -> Just run all of the functions.
    if constexpr (std::is_void_v<RETURN_T>) {
      std::apply([&fun](auto &... module) {
        (TryInvoke(fun, module), ...);
      }, plug_ins);

    }
    
    // DIRECT MATCH for return type -> return value from FIRST responder.
    else if constexpr ((requires(PLUG_IN_Ts & module) {
      { std::declval<FUN_T>()(module) } -> std::convertible_to<RETURN_T>;
    } || ...)) {
      RETURN_T result{};
      std::apply([&](auto &... module) {
        ((TryCount(fun, module) && (result = TryInvoke<RETURN_T>(fun, module), true)) || ...);
      }, plug_ins);
      return result;
    }
    
    // VECTOR MATCH -> collect one result per module that implements the signal and return all.
    else if constexpr (emp::IsVectorContainer<RETURN_T> && (requires(PLUG_IN_Ts & module) {
      { std::declval<FUN_T>()(module) } -> std::convertible_to<typename RETURN_T::value_type>;
    } || ...)) {
      RETURN_T result{};
      using elem_t = typename RETURN_T::value_type;
      std::apply([&](auto &... module) {
        ([&] {
          if constexpr (requires { { fun(module) } -> std::convertible_to<elem_t>; })
            result.push_back(fun(module));
        }(), ...);
      }, plug_ins);
      return result;
    }
    
    // EXPECTED MATCH -> return first expected result or "unexpected" if none provided.
    else if constexpr (requires { typename RETURN_T::value_type; typename RETURN_T::error_type; }
        && (requires(PLUG_IN_Ts & module) {
          { std::declval<FUN_T>()(module) } -> std::convertible_to<typename RETURN_T::value_type>;
        } || ...)) {
      RETURN_T result = std::unexpected(typename RETURN_T::error_type{});
      std::apply([&](auto &... module) {
        (([&]() -> bool {
          if constexpr (requires { { fun(module) } ->
              std::convertible_to<typename RETURN_T::value_type>; }) {
            result = RETURN_T(fun(module));
            return true;
          }
          return false;
        }()) || ...);
      }, plug_ins);
      return result;
    }
    
    // NO match; compile-time error!
    else {
      static_assert(false,
        "InvokeSignal: RETURN_T is not compatible with any module's return type.");
    }
  }

  template <typename FUN_T>
  size_t CountSignal(FUN_T && fun) {
    return std::apply([&fun](auto &... module) -> size_t {
      return (TryCount(fun, module) + ...);
    }, plug_ins);
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
