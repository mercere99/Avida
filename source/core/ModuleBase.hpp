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
  emp::String name;      // Unique name for this module; typically the class name.
  emp::String type;      // Type category of this module.
  emp::String desc;      // Full description of this module.

public:
  ModuleBase(const emp::String & name, const emp::String & type, const emp::String & desc)
    : name(name), type(type), desc(desc) { }

  [[nodiscard]] emp::String GetName() const { return name; }
  [[nodiscard]] emp::String GetType() const { return type; }
  [[nodiscard]] emp::String GetDesc() const { return desc; }
};

#define AVIDA_DEFINE_MODULE(NAME, TYPE, DESC, ...) \
template <typename AVIDA_T>                        \
class NAME : public ModuleBase<AVIDA_T> {          \
private:                                           \
  AVIDA_T & avida;                                 \
                                                   \
public:                                            \
  NAME(AVIDA_T & avida)                            \
    : ModuleBase<AVIDA_T>(#NAME, TYPE, DESC)       \
    , avida(avida) {}                              \
  ~NAME() {}                                       \
  __VA_ARGS__                                      \
}
