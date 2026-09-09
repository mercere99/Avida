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
  using ModuleBase<AVIDA_T>::avida;

  template <concepts::Organism ORG_T>
  void SetInputs(ORG_T & org, emp::Random & random) {
    org.GetPhenotype().inputs[0] = (random.GetUInt32() & random_mask) | fixed0;
    org.GetPhenotype().inputs[1] = (random.GetUInt32() & random_mask) | fixed1;
    org.Hardware().SetInput(org.GetPhenotype().inputs);
  }

  // The 16 possible Boolean functions are used to decode an output's fixed truth-table bits.
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
    TRUE,        //   1   1   1   1    0
    NUM_OPS
  };

  // Tasks that are equivalent under an exchange of the two inputs share one identity.
  enum class LogicTask {
    TASK_FALSE,
    TASK_AND,
    TASK_AND_NOT,
    TASK_ECHO,
    TASK_XOR,
    TASK_OR,
    TASK_NOR,
    TASK_EQU,
    TASK_NOT,
    TASK_OR_NOT,
    TASK_NAND,
    TASK_TRUE,
    NUM_TASKS
  };

  static constexpr size_t num_tasks = static_cast<size_t>(LogicTask::NUM_TASKS);

  emp::array<size_t, num_tasks> task_id;        // Unique Avida ID for each task performed
  emp::array<size_t, num_tasks> update_counts;  // Number of times task performed this update
  emp::array<uint32_t, 2> analysis_inputs{};    // Inputs of the organism currently being analyzed

  static constexpr const char * ToName(LogicTask task) {
    switch (task) {
    case LogicTask::TASK_FALSE:   return "FALSE";
    case LogicTask::TASK_AND:     return "AND";
    case LogicTask::TASK_AND_NOT: return "AND_NOT";
    case LogicTask::TASK_ECHO:    return "ECHO";
    case LogicTask::TASK_XOR:     return "XOR";
    case LogicTask::TASK_OR:      return "OR";
    case LogicTask::TASK_NOR:     return "NOR";
    case LogicTask::TASK_EQU:     return "EQU";
    case LogicTask::TASK_NOT:     return "NOT";
    case LogicTask::TASK_OR_NOT:  return "OR_NOT";
    case LogicTask::TASK_NAND:    return "NAND";
    case LogicTask::TASK_TRUE:    return "TRUE";
    default:                      return "Error";
    }
  }

  static constexpr LogicTask ToTask(LogicOp op) {
    switch (op) {
    case FALSE:                         return LogicTask::TASK_FALSE;
    case AND:                           return LogicTask::TASK_AND;
    case A_AND_NOT_B: case B_AND_NOT_A: return LogicTask::TASK_AND_NOT;
    case ECHO_A:      case ECHO_B:      return LogicTask::TASK_ECHO;
    case XOR:                           return LogicTask::TASK_XOR;
    case OR:                            return LogicTask::TASK_OR;
    case NOR:                           return LogicTask::TASK_NOR;
    case EQU:                           return LogicTask::TASK_EQU;
    case NOT_A:       case NOT_B:       return LogicTask::TASK_NOT;
    case A_OR_NOT_B:  case B_OR_NOT_A:  return LogicTask::TASK_OR_NOT;
    case NAND:                          return LogicTask::TASK_NAND;
    case TRUE:                          return LogicTask::TASK_TRUE;
    default:                            return LogicTask::NUM_TASKS;
    }
  }

  static constexpr uint32_t fixed_count = 4;
  static constexpr uint32_t fixed_offset = 16;
  static constexpr uint32_t fixed_mask  = emp::BitMask<uint32_t>(fixed_count, fixed_offset);
  static constexpr uint32_t random_mask = ~fixed_mask;
  static constexpr uint32_t fixed0 = static_cast<uint32_t>(0b0011) << fixed_offset;
  static constexpr uint32_t fixed1 = static_cast<uint32_t>(0b0101) << fixed_offset;

public:
  EnvironmentLogic(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "EnvironmentLogic", "Environment",
        "Reward performance of logic operations.") {}
  ~EnvironmentLogic() {}

  void Serialize(emp::SerialPod & /* pod */) {
    // Nothing extra to serialize; everything should be in the SettingsManager
  }

  void AfterLoad() {
    update_counts.fill(0);
    analysis_inputs.fill(0);
  }

#ifdef AVIDA_CHECKPOINT_DIAGNOSTICS
  [[nodiscard]] const auto & CheckpointTaskIDs() const { return task_id; }
  [[nodiscard]] const auto & CheckpointUpdateCounts() const { return update_counts; }
  [[nodiscard]] const auto & CheckpointAnalysisInputs() const { return analysis_inputs; }
#endif

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
    default: emp::notify::Error("Unknown LogicOp: ", op);
    }
    return 0;
  }

  // Identify which logic task (if any) the given output value completes for these inputs.
  // The fixed bits of the output select a single candidate op; a task is done only when the full
  // output equals that op applied to the inputs. Symmetric ops map to the same task identity.
  static constexpr LogicTask DetectTask(
    uint32_t output, const emp::array<uint32_t, 2> & inputs
  ) {
    LogicOp test_op = static_cast<LogicOp>((output & fixed_mask) >> fixed_offset);
    if (output == PerformOp(test_op, inputs[0], inputs[1])) return ToTask(test_op);
    return LogicTask::NUM_TASKS;
  }

  // === Phenotypic Traits ===

  struct Phenotype {
    emp::array<uint32_t, 2> inputs;            // Input values to perform logic on
    emp::array<size_t, num_tasks> logic_counts; // Counts of logic task performance
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(inputs, "Input values provided to this organism.");
    AVIDA_REGISTER_TRAIT(logic_counts, "Number of times each logic task was performed.");

    // Register all of the tasks.
    for (size_t i = 0; i < num_tasks; ++i) {
      task_id[i] = avida.RegisterTask(ToName(static_cast<LogicTask>(i)));
    }
  }

  // === Signal Listeners ===

  void OnUpdateStart([[maybe_unused]] size_t update) {
    update_counts.fill(0); // Reset all logic task counts for the new update.
  }

  // Before an organism is placed in the environment, make sure it has its inputs ready.
  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    SetInputs(org, avida.GetRandom());
  }

  // Give isolated test organisms normal environment inputs without consuming the live RNG stream.
  template <concepts::Organism ORG_T>
  void OnAnalysisOrganism(ORG_T & org, emp::Random & analysis_random) {
    SetInputs(org, analysis_random);
    // Cache inputs for OnAnalyzeOutput, which sees only isolated analysis hardware with no live
    // Biota slot and thus no phenotype to read.
    // ASSUMPTION: only one organism is traced/analyzed at a time, so a single cache is sufficient.
    analysis_inputs = org.GetPhenotype().inputs;
  }

  template <concepts::Organism ORG_T>
  void OnOutputValue(ORG_T & org, uint32_t output) {
    LogicTask task = DetectTask(output, org.GetPhenotype().inputs);
    if (task != LogicTask::NUM_TASKS) {
      const size_t task_idx = static_cast<size_t>(task);
      ++org.GetPhenotype().logic_counts[task_idx]; // Increment org phenotype count.
      ++update_counts[task_idx];                   // Increment global task count this update.
      avida.SignalTask(org, task_id[task_idx]);    // Allow other modules to know about task.
    }
  }

  // Analysis counterpart to OnOutputValue: surface completed tasks in the trace without rewarding
  // the organism or touching global stats.  Runs on isolated analysis hardware, so it annotates the
  // hardware (which Trace() flushes) rather than mutating any organism or module state.
  template <typename HARDWARE_T>
  void OnAnalyzeOutput(HARDWARE_T & hardware, uint32_t output) {
    LogicTask task = DetectTask(output, analysis_inputs);
    if (task != LogicTask::NUM_TASKS) {
      const size_t task_idx = static_cast<size_t>(task);
      hardware.AddNote("Task performed: ", ToName(task));
      hardware.AddAnalysisTask(task_id[task_idx]);
    }
  }

  void OnConfigWrite(std::ostream & os) {
    std::print(os,
      "Reaction ECHO     metabolic_mult mult 2.0 1\n"
      "Reaction NOT      metabolic_mult mult 2.0 1\n"
      "Reaction NAND     metabolic_mult mult 2.0 1\n"
      "Reaction AND      metabolic_mult mult 2.0 1\n"
      "Reaction OR_NOT   metabolic_mult mult 2.0 1\n"
      "Reaction OR       metabolic_mult mult 2.0 1\n"
      "Reaction AND_NOT  metabolic_mult mult 2.0 1\n"
      "Reaction NOR      metabolic_mult mult 2.0 1\n"
      "Reaction XOR      metabolic_mult mult 2.0 1\n"
      "Reaction EQU      metabolic_mult mult 2.0 1\n");
  }
};
