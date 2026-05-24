#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Provide an IO instruction and track logic tasks performed.
 */

#include <cstddef>   // for size_t
#include <iostream>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class EnvironmentLogic : public ModuleBase<AVIDA_T> {
private:
  AVIDA_T & avida;

  enum LogicOp {
    // Inputs:       00  01  10  11    Min NANDs
    FALSE = 0,   //   0   0   0   0    0
    AND,         //   0   0   0   1    2
    A_AND_NOT_B, //   0   0   1   0    3
    ECHO_A,      //   0   0   1   1    0
    B_AND_NOT_A, //   0   1   0   0    3
    ECHO_B,      //   0   1   0   1    0
    XOR,         //   0   1   1   0    4
    OR,          //   0   1   1   1    3
    NOR,         //   1   0   0   0    4
    EQU,         //   1   0   0   1    5
    NOT_B,       //   1   0   1   0    1
    A_OR_NOT_B,  //   1   0   1   1    2
    NOT_A,       //   1   1   0   0    1
    B_OR_NOT_A,  //   1   1   0   1    2
    NAND,        //   1   1   1   0    1
    TRUE,         //   1   1   1   1    0
    NUM_OPS
  };

  static constexpr const char * ToName(LogicOp op) {
    switch (op) {
    case FALSE:       return "FALSE";
    case AND:         return "AND";
    case A_AND_NOT_B: return "A_AND_NOT_B";
    case ECHO_A:      return "ECHO_A";
    case B_AND_NOT_A: return "B_AND_NOT_A";
    case ECHO_B:      return "ECHO_B";
    case XOR:         return "XOR";
    case OR:          return "OR";
    case NOR:         return "NOR";
    case EQU:         return "EQU";
    case NOT_B:       return "NOT_B";
    case A_OR_NOT_B:  return "A_OR_NOT_B";
    case NOT_A:       return "NOT_A";
    case B_OR_NOT_A:  return "B_OR_NOT_A";
    case NAND:        return "NAND";
    case TRUE:        return "TRUE";
    default:          return "Error";
    }
  }

  static constexpr LogicOp ToOp(const emp::String & name) {
    if (name == "FALSE")       return LogicOp::FALSE;
    if (name == "TRUE")        return LogicOp::TRUE;
    if (name == "ECHO_A")      return LogicOp::ECHO_A;
    if (name == "ECHO_B")      return LogicOp::ECHO_B;
    if (name == "NOT_A")       return LogicOp::NOT_A;
    if (name == "NOT_B")       return LogicOp::NOT_B;
    if (name == "NAND")        return LogicOp::NAND;
    if (name == "AND")         return LogicOp::AND;
    if (name == "A_OR_NOT_B")  return LogicOp::A_OR_NOT_B;
    if (name == "B_OR_NOT_A")  return LogicOp::B_OR_NOT_A;
    if (name == "OR")          return LogicOp::OR;
    if (name == "A_AND_NOT_B") return LogicOp::A_AND_NOT_B;
    if (name == "B_AND_NOT_A") return LogicOp::B_AND_NOT_A;
    if (name == "NOR")         return LogicOp::NOR;
    if (name == "XOR")         return LogicOp::XOR;
    if (name == "EQU")         return LogicOp::EQU;
    return LogicOp::NUM_OPS;
  }

  static constexpr uint32_t fixed_count = 4;
  static constexpr uint32_t fixed_offset = 16;
  static constexpr uint32_t fixed_mask  = emp::BitMask(fixed_count, fixed_offset);
  static constexpr uint32_t random_mask = ~fixed_mask;
  static constexpr uint32_t fixed0 = static_cast<uint32_t>(0b0011) << fixed_offset;
  static constexpr uint32_t fixed1 = static_cast<uint32_t>(0b0101) << fixed_offset;

public:
  EnvironmentLogic(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>("EnvironmentLogic", "Environment", "Reward performance of logic operations.")
    , avida(avida) {}
  ~EnvironmentLogic() {}

  constexpr static uint32_t PerformOp(LogicOp op, uint32_t valA, uint32_t valB) {
    switch (op) {
    case LogicOp::FALSE:       return 0;
    case LogicOp::AND:         return valA & valB;
    case LogicOp::A_AND_NOT_B: return valA & ~valB;
    case LogicOp::ECHO_A:      return valA;
    case LogicOp::B_AND_NOT_A: return valB & ~valA;
    case LogicOp::ECHO_B:      return valB;
    case LogicOp::XOR:         return valA ^ valB;
    case LogicOp::OR:          return valA | valB;
    case LogicOp::NOR:         return ~(valA | valB);
    case LogicOp::EQU:         return ~(valA ^ valB);
    case LogicOp::NOT_B:       return ~valB;
    case LogicOp::A_OR_NOT_B:  return valA | ~valB;
    case LogicOp::NOT_A:       return ~valA;
    case LogicOp::B_OR_NOT_A:  return valB | ~valA;
    case LogicOp::NAND:        return ~(valA & valB);
    case LogicOp::TRUE:        return static_cast<uint32_t>(-1);
    }
    emp::notify::Error("Unknown LogicOp: ", op);
    return 0;
  }

  // === Phenotypic Traits ===

  struct Phenotype {
    emp::array<uint32_t, 2> inputs;            // Input values to perform logic on
    emp::array<size_t, NUM_OPS> logic_counts;  // Counts of logic operation performance
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(inputs, "Input values provided to this organism.");
    AVIDA_REGISTER_TRAIT(logic_counts, "Number of times each logic task was performed.");
  }

  // === Signal Listeners ===

  // Before an organism is placed in the environment, make sure it has its inputs ready.
  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    org.Phenotype().inputs[0] = (avida.GetRandom().GetUInt32() & random_mask) | fixed0;
    org.Phenotype().inputs[1] = (avida.GetRandom().GetUInt32() & random_mask) | fixed1;
    org.AddInputs(org.Phenotype().inputs);
  }

  // When an organism is about to reproduce, check its outputs.
  template <concepts::Organism ORG_T>
  void BeforeRepro(ORG_T & parent) {
    const emp::array<uint32_t, 2> & inputs = parent.Phenotype().inputs;
    std::span<uint32_t> outputs = parent.GetOutputs<uint32_t>();
    for (uint32_t output : outputs) {
      // Use the fixed bits to determine the candidate logic op.
      LogicOp test_op = static_cast<LogicOp>((output & fixed_mask) >> fixed_offset);
      if (output == PerformOp(test_op, inputs[0], inputs[1])) {
        ++parent.Phenotype().logic_counts[test_op];
      }
    }
  }

};
