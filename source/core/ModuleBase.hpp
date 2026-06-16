#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 * 
 *  Base class for all module plug-ins.
 */

#include "emp/base/Ptr.hpp"
#include "emp/datastructs/RobinHoodMap.hpp"
#include "emp/tools/String.hpp"

template <typename AVIDA_T>
class ModuleBase {
protected:
  AVIDA_T & avida;       // Reference to the main Avida controller for this module.

  emp::String name;      // Unique name for this module; typically the class name.
  emp::String type;      // Type category of this module.
  emp::String desc;      // Full description of this module.

public:
  ModuleBase(AVIDA_T & avida, const emp::String & name,
             const emp::String & type, const emp::String & desc)
    : avida(avida), name(name), type(type), desc(desc) { }

  [[nodiscard]] auto & Avida(this auto & self) { return self.avida; }

  [[nodiscard]] const emp::String & GetName() const { return name; }
  [[nodiscard]] const emp::String & GetType() const { return type; }
  [[nodiscard]] const emp::String & GetDesc() const { return desc; }

  template <typename... Ts>
  void TriggerSignal(Ts &&... args) { avida.TriggerSignal(std::forward<Ts>(args)...); }

  template <typename RETURN_T, typename... Ts>
  auto TriggerHandler(Ts &&... args) {
    return avida.template TriggerHandler<RETURN_T>(std::forward<Ts>(args)...);
  }

  template <typename RETURN_T, typename... Ts>
  auto TriggerCollector(Ts &&... args) {
    return avida.template TriggerCollector<RETURN_T>(std::forward<Ts>(args)...);
  }
};

// Modules can have their boilerplate handled with this macro.
#define AVIDA_DEFINE_MODULE(NAME, TYPE, DESC, ...)     \
template <typename AVIDA_T>                            \
class NAME : public ModuleBase<AVIDA_T> {              \
private:                                               \
  using ModuleBase<AVIDA_T>::avida;                    \
                                                       \
public:                                                \
  NAME(AVIDA_T & avida)                                \
    : ModuleBase<AVIDA_T>(avida, #NAME, TYPE, DESC) {} \
  ~NAME() {}                                           \
  __VA_ARGS__                                          \
}
