#pragma once

// The module pack is the source of truth for selecting interface adapters.
#include "core/Avida.hpp"
#include "core/Checkpoint.hpp"
#include "core/PopulationViewOptions.hpp"

#include "Modules/DriverBuffered.hpp"
#include "Modules/EnvironmentLogic.hpp"
#include "Modules/EventManager.hpp"
#include "Modules/MutationsDivideSub.hpp"
#include "web/adapters/OrganismAdapter.hpp"
#include "web/adapters/PopulationAdapter.hpp"
#include "Modules/ReactionsManager.hpp"
#include "Modules/TrackGeneration.hpp"
#include "Modules/TrackGenotypes.hpp"
#include "Modules/TrackMetabolism.hpp"

#include "web/WebInterfaceBridge.hpp"

#ifndef AVIDA_WEB_POPULATION
#define AVIDA_WEB_POPULATION PopGrid
#endif

template <template <typename> typename POPULATION>
using AvidianWebAvida = Avida<
  OrgTypeAvidian,
  POPULATION,
  DriverBuffered,
  MutationsDivideSub,
  TrackGeneration,
  TrackGenotypes,
  EventManager,
  EnvironmentLogic,
  ReactionsManager,
  TrackMetabolism,
  WebInterfaceBridge
>;
using avida_t = AvidianWebAvida<AVIDA_WEB_POPULATION>;

using population_adapter_t =
  avida_web::selected_adapter_t<avida_t, avida_web::PopulationAdapter>;
using organism_adapter_t =
  avida_web::selected_adapter_t<avida_t, avida_web::OrganismAdapter>;
using reaction_config_t = typename ReactionsManager<avida_t>::Config;
using event_config_t = typename EventManager<avida_t>::Config;

