#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

#include <functional>
#include <utility>

#include "core/ModuleBase.hpp"

template <typename AVIDA_T>
class WebInterfaceBridge : public ModuleBase<AVIDA_T> {
private:
  std::function<void()> on_start_callback;
  std::function<void()> before_exit_callback;

public:
  WebInterfaceBridge(AVIDA_T & avida)
    : ModuleBase<AVIDA_T>(
        avida,
        "WebInterfaceBridge",
        "Interface",
        "Connect Avida lifecycle signals to the web interface."
      ) { }

  void Serialize(emp::SerialPod & /* pod */) { }

  void SetOnStartCallback(std::function<void()> callback) {
    on_start_callback = std::move(callback);
  }

  void SetBeforeExitCallback(std::function<void()> callback) {
    before_exit_callback = std::move(callback);
  }

  void OnStart() {
    if (on_start_callback) on_start_callback();
  }

  void BeforeExit() {
    if (before_exit_callback) before_exit_callback();
  }
};

