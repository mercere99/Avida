#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Reward organisms for completing tasks (in any environment) by modifying a trait.
 *
 *  Each configured reaction applies a bonus (additive or multiplicative) to a trait on the
 *  organism that performs the task -- typically its metabolic_mult, so doing tasks raises the
 *  organism's own metabolic rate.  When the organism divides, that earned bonus passes to its
 *  offspring as a starting floor (handled by TrackMetabolism's fission), and must be re-earned
 *  the next gestation.
 *
 *  This module also tracks, per organism, how many times it performed each task this gestation,
 *  and what its parent performed.  The data file reports, per task, how many current organisms
 *  had a parent perform it -- a proxy for how many are likely capable of that task.
 */

#include <cstddef>    // for size_t

#include "emp/base/notify.hpp"
#include "emp/base/Ptr.hpp"
#include "emp/base/vector.hpp"
#include "emp/meta/TypeID.hpp"
#include "emp/tools/String.hpp"

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class ReactionsManager : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  enum class Op { ADD, MULT };

  // A single configured reaction: reward a target trait when a given task completes.
  struct Reaction {
    emp::String task_name;    // Task that triggers this reaction.
    emp::String trait_name;   // Target trait to modify.
    Op op = Op::ADD;          // Additive or multiplicative bonus.
    double value = 0.0;       // Magnitude of the change.
    size_t max_triggers = 0;  // Per-lifetime cap on reaction count (0 == unlimited).

    // Internally calculated:
    size_t task_id = 0;                                // Resolved task ID
    // Target trait, resolved in BeforeStart.  A Trait *pointer* (template-id) is cached rather
    // than the accessor by value, so this struct names no organism_t-dependent type at class
    // scope -- which would fail while Avida's phenotype (and thus organism_t) is still forming.
    emp::Ptr<const Trait<double, AVIDA_T>> trait_ptr;
  };

  emp::vector<Reaction> reactions;                  // All reactions, in declaration order.
  emp::vector<emp::vector<size_t>> task_reactions;  // task ID -> indices into `reactions`.

  static Op ToOp(emp::String name) {
    name.SetLower();
    if (name == "add"  || name == "+") return Op::ADD;
    if (name == "mult" || name == "*") return Op::MULT;
    emp::notify::Error("Unknown reaction operation '", name, "'; expected 'add' or 'mult'.");
    return Op::ADD;
  }

  // Count active organisms whose parent performed `task_id` at least once (a capability proxy).
  size_t CountParentPerformers(size_t task_id) {
    size_t count = 0;
    avida.GetBiota().ForEachOrg([&](auto & org) {      
      const auto & p_tasks = org.GetPhenotype().parent_task_counts;
      if (task_id < p_tasks.size() && p_tasks[task_id]) ++count;
    });
    return count;
  }

public:
  ReactionsManager(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(avida, "ReactionsManager", "Reactions",
        "Reward tasks by modifying organism traits (classic-Avida merit model).") {}
  ~ReactionsManager() {}

  void Serialize(emp::SerialPod & /* pod */) {
    // `reactions`/`targets`/`task_reactions` are rebuilt from config keywords plus BeforeStart.
    // Per-organism task counts are registered traits (serialized with each organism), and the
    // output tally is recomputed before each write -- so there is nothing extra to save here.
  }

  // === Phenotypic Traits ===

  // Per-organism state, indexed by task ID.  task_counts is how many times this organism has
  // performed each task THIS gestation; it caps reaction rewards and is reset on divide, so a
  // bonus must be re-earned each gestation.  parent_task_counts is the parent's task_counts for
  // the gestation that produced this organism, copied at birth.
  struct Phenotype {
    emp::vector<size_t> task_counts;        // Times performed each task THIS gestation.
    emp::vector<size_t> parent_task_counts; // Parent's task_counts for the gestation that made us.
  };

  void RegisterTraits() {
    AVIDA_REGISTER_TRAIT(task_counts, "Times organism performed each task this gestation.");
    AVIDA_REGISTER_TRAIT(parent_task_counts, "Times organism's parent performed each task (its gestation).");
  }

  void RegisterSettings() {
    avida.AddKeyword("Reaction",
      [this](emp::vector<emp::String> args){ AddReaction(args); },
      "Define a reaction: Reaction <task> <trait> <add|mult> <value> [max_triggers]");
  }

  // Parse one `Reaction` config line into an (as-yet unresolved) reaction record.
  void AddReaction(const emp::vector<emp::String> & args) {
    if (args.size() < 4 || args.size() > 5) {
      emp::notify::Error("Reaction needs 4-5 args: <task> <trait> <add|mult> <value> [max_triggers];"
                         " received ", args.size(), ".");
      return;
    }
    Reaction react;
    react.task_name    = args[0];
    react.trait_name   = args[1];
    react.op           = ToOp(args[2]);
    react.value        = args[3].ConvertTo<double>();
    react.max_triggers = (args.size() >= 5) ? args[4].ConvertTo<size_t>() : 0;
    reactions.push_back(std::move(react));
  }

  // === Signal Listeners ===

  // Validate the configuration and resolve task IDs + trait accessors, now that every
  // module has registered its tasks and traits.
  void BeforeStart() {
    task_reactions.resize(avida.GetNumTasks());

    // Process each reaction...
    for (size_t react_id = 0; react_id < reactions.size(); ++react_id) {
      Reaction & react = reactions[react_id];

      // Make sure that we can hook in the task for this reaction.
      if (!avida.HasTask(react.task_name)) {
        emp::notify::Error("Reaction references unknown task '", react.task_name,
          "'; is an environment module that defines it included in the module list?");
      }
      react.task_id = avida.GetTaskID(react.task_name);

      // Make sure that we have a proper double trait to modify from this reaction.
      if (!avida.HasTrait(react.trait_name)) {
        emp::notify::Error("Reaction targets unknown trait '", react.trait_name, "'.");
      }
      if (avida.GetTrait(react.trait_name).GetTypeID() != emp::GetTypeID<double>()) {
        emp::notify::Error("Reaction trait '", react.trait_name, "' must be type double, but is ",
          avida.GetTrait(react.trait_name).GetTypeID(), ".");
      }

      react.trait_ptr = &avida.template GetTypedTrait<double>(react.trait_name);

      task_reactions[react.task_id].push_back(react_id);

      avida.AddOutput("reactions.csv", react.task_name,
        [this, task_id=react.task_id](){ return CountParentPerformers(task_id); });
    }
  }

  // Count this task and apply each associated reaction's bonus to the organism's own rate trait.
  // That earned bonus passes to the organism's offspring as a starting floor when it divides
  // (handled by TrackMetabolism's fission), and is re-earned the next gestation.
  template <concepts::Organism ORG_T>
  void OnTaskComplete(ORG_T & org, size_t task_id) {
    emp_assert(task_id < task_reactions.size(), task_id, task_reactions.size());

    auto & task_counts = org.GetPhenotype().task_counts;
    const size_t n = ++task_counts[task_id];  // Nth time performed this gestation.

    // Trigger all reactions associated with this task.
    for (size_t r_idx : task_reactions[task_id]) {
      const Reaction & react = reactions[r_idx];
      if (react.max_triggers && n > react.max_triggers) continue;  // Past this gestation's cap.
      switch (react.op) {
      case Op::ADD:  react.trait_ptr->GetAccessFun()(org) += react.value; break;
      case Op::MULT: react.trait_ptr->GetAccessFun()(org) *= react.value; break;
      }
    }
  }

  // The offspring inherits the parent's task counts for the gestation that produced it; the parent
  // then begins a fresh gestation and must re-earn its task bonuses (mirroring the rate reset in
  // TrackMetabolism's OnOffspringReady).
  template <concepts::Organism ORG_T>
  void OnOffspringReady(ORG_T & offspring, ORG_T & parent) {
    offspring.GetPhenotype().parent_task_counts = parent.GetPhenotype().task_counts;
    parent.GetPhenotype().task_counts.assign(avida.GetNumTasks(), 0);
  }

  // Injected organisms have no parent, so they start with an empty parental task history.
  template <concepts::Organism ORG_T>
  void OnInjectReady(ORG_T & org) {
    org.GetPhenotype().parent_task_counts.assign(avida.GetNumTasks(), 0);
  }

  // Start every organism's first gestation with zero task counts.  parent_task_counts was set at
  // birth (OnOffspringReady) or injection (OnInjectReady), so it is intentionally left intact here.
  template <concepts::Organism ORG_T>
  void BeforePlacement(ORG_T & org) {
    org.GetPhenotype().task_counts.assign(avida.GetNumTasks(), 0);
  }
};
