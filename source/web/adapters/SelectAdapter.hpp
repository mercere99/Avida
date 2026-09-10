#pragma once

#include "emp/meta/TypePack.hpp"

namespace avida_web {

// A specialization registers itself by defining supported_t. Selection uses the actual Avida
// module pack; no second list of UI module types needs to be maintained by a build profile.
template <typename AVIDA_T, template <typename> typename ADAPTER>
struct SelectAdapter {
  template <typename MODULE_T>
  using supported_t = typename ADAPTER<MODULE_T>::supported_t;

  using matches_t = typename AVIDA_T::plug_in_pack_t::template filter<supported_t>;
  static_assert(matches_t::SIZE == 1,
    "A web capability requires exactly one supported module adapter in the Avida module pack.");
  using type = ADAPTER<typename matches_t::first_t>;
};

template <typename AVIDA_T, template <typename> typename ADAPTER>
using selected_adapter_t = typename SelectAdapter<AVIDA_T, ADAPTER>::type;

}
