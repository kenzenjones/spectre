// Distributed under the MIT License.
// See LICENSE.txt for details.

#pragma once

#include <cstddef>

#include "DataStructures/VariablesTag.hpp"
#include "Evolution/Systems/Burgers/BoundaryConditions/BoundaryCondition.hpp"
#include "Evolution/Systems/Burgers/BoundaryCorrections/BoundaryCorrection.hpp"
#include "Evolution/Systems/Burgers/Characteristics.hpp"
#include "Evolution/Systems/Burgers/Tags.hpp"
#include "Evolution/Systems/Burgers/TimeDerivativeTerms.hpp"
#include "Utilities/TMPL.hpp"

/// \ingroup EvolutionSystemsGroup
/// \brief Items related to evolving the %Burgers equation
/// \f$0 = \partial_t U + \partial_x\left(U^2/2\right)\f$.
///
/// \note For this definition (i.e., with the factor of one half in the flux)
/// of the Burgers system, the local characteristic speed is \f$U\f$.
namespace Burgers {
struct System {
  static constexpr bool is_in_flux_conservative_form = true;
  static constexpr bool has_primitive_and_conservative_vars = false;
  static constexpr size_t volume_dim = 1;

  using boundary_conditions_base = BoundaryConditions::BoundaryCondition;
  using boundary_correction_base = BoundaryCorrections::BoundaryCorrection;

  using variables_tag = ::Tags::Variables<tmpl::list<Tags::U>>;
  using flux_variables = tmpl::list<Tags::U>;
  using gradient_variables = tmpl::list<>;

  using compute_volume_time_derivative_terms = TimeDerivativeTerms;

  using compute_largest_characteristic_speed =
      Tags::ComputeLargestCharacteristicSpeed;
};
}  // namespace Burgers
