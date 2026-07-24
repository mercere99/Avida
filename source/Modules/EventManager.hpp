#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 *
 *  Schedule SettingsManager commands at population start, selected updates, or run end.
 */

#include <charconv>
#include <cstddef>
#include <limits>
#include <system_error>
#include <utility>

#include "../core/Avida.hpp"

template <typename AVIDA_T>
class EventManager : public ModuleBase<AVIDA_T> {
private:
  using ModuleBase<AVIDA_T>::avida;

  static constexpr size_t NO_STOP = std::numeric_limits<size_t>::max();

  struct UpdateEvent {
    size_t start = 0;
    size_t step = 1;
    size_t stop = 0;
    emp::String command;

    [[nodiscard]] bool IsDue(size_t update) const {
      return update >= start
        && update <= stop
        && (update - start) % step == 0;
    }

    void Serialize(emp::SerialPod & pod) {
      pod(start, step, stop, command);
    }
  };

  emp::vector<emp::String> start_commands;
  emp::vector<UpdateEvent> update_events;
  emp::vector<emp::String> end_commands;
  bool started = false;
  bool ended = false;

  [[nodiscard]] static size_t ParseUpdate(const emp::String & token,
                                          const emp::String & field_name) {
    size_t value = 0;
    const char * begin = token.data();
    const char * end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (begin == end || error != std::errc{} || ptr != end) {
      emp::notify::Error(
        "Invalid ", field_name, " '", token, "' in update event; expected an unsigned integer."
      );
    }
    return value;
  }

  [[nodiscard]] emp::String BuildCommand(const emp::vector<emp::String> & args,
                                         size_t command_pos) const {
    if (command_pos >= args.size()) {
      emp::notify::Error("Event is missing the SettingsManager command to execute.");
    }
    if (!avida.GetSettings().HasIdentifier(args[command_pos])) {
      emp::notify::Error(
        "Event command begins with unknown SettingsManager identifier '",
        args[command_pos], "'."
      );
    }

    emp::String command = args[command_pos];
    for (++command_pos; command_pos < args.size(); ++command_pos) {
      command += ' ';
      command += args[command_pos];
    }
    return command;
  }

  void AddUpdateEvent(const emp::vector<emp::String> & args) {
    if (args.size() < 3) {
      emp::notify::Error(
        "on update requires a schedule and command: "
        "on update <start>[:<step>[:<stop>]] <command>."
      );
    }

    size_t pos = 1;
    UpdateEvent event;
    event.start = ParseUpdate(args[pos++], "start update");
    if (event.start == 0) {
      emp::notify::Error("Update events must start at update 1 or later; use 'on start' before then.");
    }

    if (pos < args.size() && args[pos] == ":") {
      ++pos;
      if (pos >= args.size()) emp::notify::Error("Update event is missing its step value.");
      event.step = ParseUpdate(args[pos++], "update step");
      if (event.step == 0) emp::notify::Error("Update event step must be greater than zero.");
      event.stop = NO_STOP;

      if (pos < args.size() && args[pos] == ":") {
        ++pos;
        if (pos >= args.size()) emp::notify::Error("Update event is missing its stop update.");
        event.stop = ParseUpdate(args[pos++], "stop update");
        if (event.stop < event.start) {
          emp::notify::Error("Update event stop must not precede its start.");
        }
      }
    } else {
      event.stop = event.start;
    }

    event.command = BuildCommand(args, pos);
    update_events.push_back(std::move(event));
  }

  void AddEvent(const emp::vector<emp::String> & args) {
    if (args.empty()) {
      emp::notify::Error("on requires a signal: start, update, or end.");
    }

    if (args[0] == "start") {
      start_commands.push_back(BuildCommand(args, 1));
    } else if (args[0] == "update") {
      AddUpdateEvent(args);
    } else if (args[0] == "end") {
      end_commands.push_back(BuildCommand(args, 1));
    } else {
      emp::notify::Error(
        "Unknown event signal '", args[0], "'; expected start, update, or end."
      );
    }
  }

  void ExecuteCommands(const emp::vector<emp::String> & commands) {
    for (const emp::String & command : commands) avida.ExecuteCommand(command);
  }

public:
  EventManager(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(
        avida,
        "EventManager",
        "Events",
        "Schedule SettingsManager commands during an Avida run."
      )
  { }

  void Serialize(emp::SerialPod & pod) {
    pod(start_commands, update_events, end_commands, started, ended);
  }

  void RegisterSettings() {
    avida.AddKeyword(
      "on",
      [this](emp::vector<emp::String> args) { AddEvent(args); },
      "Schedule a command: on <start|end> <command>, or "
      "on update <start>[:<step>[:<stop>]] <command>"
    );
  }

  [[nodiscard]] size_t GetNumStartEvents() const { return start_commands.size(); }
  [[nodiscard]] size_t GetNumUpdateEvents() const { return update_events.size(); }
  [[nodiscard]] size_t GetNumEndEvents() const { return end_commands.size(); }

  /// Start events run only after the complete initial population is available.
  void OnPopulationReady() {
    if (started) return;
    started = true;
    ExecuteCommands(start_commands);
  }

  /// Update events run before any organisms execute during the selected update.
  void OnUpdateStart(size_t update) {
    emp::vector<emp::String> due_commands;
    for (const UpdateEvent & event : update_events) {
      if (event.IsDue(update)) due_commands.push_back(event.command);
    }
    ExecuteCommands(due_commands);
  }

  /// End events run while the final population is still intact.
  void BeforeExit() {
    if (!started || ended) return;
    ended = true;
    ExecuteCommands(end_commands);
  }
};
