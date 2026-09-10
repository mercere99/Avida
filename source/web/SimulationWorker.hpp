#pragma once

/*
 *  This file is part of the Avida Digital Evolution Research Platform, v5.0
 *  Copyright (C) 2026 Michigan State University & Dr. Charles Ofria
 *  Released under the MIT Public Licence.  See LICENSE.md for details.
 */

// Worker implementation; included after the AvidaWebApp declaration.
// Only the browser thread dispatches/consumes work. While busy, it must not access Avida.

bool AvidaWebApp::SimulationWorkerBusy() const {
  return simulation_worker_state.load(std::memory_order_acquire)
    != SimulationWorkerState::IDLE;
}

void AvidaWebApp::SimulationWorkerLoop() {
  while (!simulation_worker_shutdown.load(std::memory_order_acquire)) {
    std::unique_lock lock{simulation_wait_mutex};
    simulation_wait_condition.wait(lock, [this]() {
      return simulation_worker_shutdown.load(std::memory_order_acquire)
        || simulation_worker_state.load(std::memory_order_acquire)
          == SimulationWorkerState::REQUESTED;
    });
    if (simulation_worker_shutdown.load(std::memory_order_acquire)) return;
    simulation_worker_state.store(SimulationWorkerState::RUNNING, std::memory_order_release);
    lock.unlock();

    bool can_continue = true;
    const size_t update_budget = simulation_update_budget.load(std::memory_order_acquire);
    for (size_t completed = 0; completed < update_budget; ++completed) {
      if (simulation_pause_requested.load(std::memory_order_acquire)) break;
      can_continue = Avida().AdvanceUpdateDeferredShutdown();
      if (Avida().ConsumePauseRequest()) {
        simulation_pause_requested.store(true, std::memory_order_release);
        break;
      }
      if (!can_continue) break;
    }
    simulation_can_continue.store(can_continue, std::memory_order_release);
    if (simulation_worker_shutdown.load(std::memory_order_acquire)) return;
    simulation_worker_state.store(SimulationWorkerState::READY, std::memory_order_release);
  }
}

void AvidaWebApp::StartSimulationWorker() {
  emp_assert(!simulation_worker.joinable());
  simulation_worker_shutdown.store(false, std::memory_order_release);
  simulation_pause_requested.store(false, std::memory_order_release);
  simulation_update_budget.store(1, std::memory_order_release);
  simulation_worker_state.store(SimulationWorkerState::IDLE, std::memory_order_release);
  simulation_worker = std::thread([this](){ SimulationWorkerLoop(); });
}

void AvidaWebApp::StopSimulationWorker() {
  if (!simulation_worker.joinable()) return;
  {
    // Change the wait predicate under its mutex to prevent a lost wakeup.
    std::lock_guard lock{simulation_wait_mutex};
    simulation_worker_shutdown.store(true, std::memory_order_release);
  }
  simulation_wait_condition.notify_one();
  simulation_worker.join();
  simulation_worker_state.store(SimulationWorkerState::IDLE, std::memory_order_release);
}

bool AvidaWebApp::DispatchSimulationUpdate(size_t update_budget) {
  emp_assert(update_budget > 0);
  // Pair publication with the worker wait, including the predicate-to-sleep transition.
  std::lock_guard lock{simulation_wait_mutex};
  if (SimulationWorkerBusy()) return false;
  simulation_update_budget.store(update_budget, std::memory_order_release);
  SimulationWorkerState expected = SimulationWorkerState::IDLE;
  if (!simulation_worker_state.compare_exchange_strong(
        expected,
        SimulationWorkerState::REQUESTED,
        std::memory_order_acq_rel
      )) return false;
  simulation_wait_condition.notify_one();
  return true;
}

std::optional<bool> AvidaWebApp::ConsumeSimulationUpdate() {
  if (simulation_worker_state.load(std::memory_order_acquire)
      != SimulationWorkerState::READY) return std::nullopt;
  const bool can_continue = simulation_can_continue.load(std::memory_order_acquire);
  simulation_worker_state.store(SimulationWorkerState::IDLE, std::memory_order_release);
  return can_continue;
}
