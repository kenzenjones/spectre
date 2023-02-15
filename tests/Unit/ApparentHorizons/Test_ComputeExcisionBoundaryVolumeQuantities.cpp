// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Framework/TestingFramework.hpp"

#include <array>
#include <cstddef>
#include <memory>

#include "ApparentHorizons/ComputeExcisionBoundaryVolumeQuantities.hpp"
#include "ApparentHorizons/ComputeExcisionBoundaryVolumeQuantities.tpp"
#include "Domain/Block.hpp"
#include "Domain/Creators/Brick.hpp"
#include "Domain/Domain.hpp"
#include "Domain/ElementMap.hpp"
#include "Domain/Structure/ElementId.hpp"
#include "Domain/Structure/InitialElementIds.hpp"
#include "NumericalAlgorithms/LinearOperators/PartialDerivatives.hpp"
#include "NumericalAlgorithms/LinearOperators/PartialDerivatives.tpp"
#include "NumericalAlgorithms/Spectral/LogicalCoordinates.hpp"
#include "NumericalAlgorithms/Spectral/Mesh.hpp"
#include "ParallelAlgorithms/Interpolation/Protocols/ComputeVarsToInterpolate.hpp"
#include "PointwiseFunctions/AnalyticSolutions/GeneralRelativity/KerrSchild.hpp"
#include "PointwiseFunctions/GeneralRelativity/GeneralizedHarmonic/Phi.hpp"
#include "PointwiseFunctions/GeneralRelativity/GeneralizedHarmonic/Pi.hpp"
#include "PointwiseFunctions/GeneralRelativity/SpacetimeMetric.hpp"
#include "PointwiseFunctions/GeneralRelativity/Tags.hpp"
#include "Time/Slab.hpp"
#include "Time/Time.hpp"
#include "Time/TimeStepId.hpp"
#include "Utilities/ErrorHandling/Assert.hpp"
#include "Utilities/Gsl.hpp"

namespace {
template <typename IsTimeDependent, typename TargetFrame, typename SrcTags,
          typename DestTags>
void test_compute_excision_boundary_volume_quantities() {
  const size_t number_of_grid_points = 8;

  // slab and temporal_id used only in the TimeDependent version.
  Slab slab(0.0, 1.0);
  TimeStepId temporal_id{true, 0, Time(slab, Rational(13, 15))};

  // Create a brick offset from the origin, so a KerrSchild solution
  // doesn't have a singularity or excision_boundary in the domain.
  std::unique_ptr<DomainCreator<3>> domain_creator;
  if constexpr (IsTimeDependent::value) {
    domain_creator = std::make_unique<domain::creators::Brick>(
        std::array<double, 3>{3.1, 3.2, 3.3},
        std::array<double, 3>{4.1, 4.2, 4.3}, std::array<size_t, 3>{0, 0, 0},
        std::array<size_t, 3>{number_of_grid_points, number_of_grid_points,
                              number_of_grid_points},
        std::array<bool, 3>{false, false, false},
        std::make_unique<
            domain::creators::time_dependence::UniformTranslation<3>>(
            0.0, std::array<double, 3>{0.01, 0.02, 0.03}));
  } else {
    domain_creator = std::make_unique<domain::creators::Brick>(
        std::array<double, 3>{3.1, 3.2, 3.3},
        std::array<double, 3>{4.1, 4.2, 4.3}, std::array<size_t, 3>{0, 0, 0},
        std::array<size_t, 3>{number_of_grid_points, number_of_grid_points,
                              number_of_grid_points});
  }
  const auto domain = domain_creator->create_domain();
  ASSERT(domain.blocks().size() == 1, "Expected a Domain with one block");

  const auto element_ids = initial_element_ids(
      domain.blocks()[0].id(),
      domain_creator->initial_refinement_levels()[domain.blocks()[0].id()]);
  ASSERT(element_ids.size() == 1, "Expected a Domain with only one element");

  // Set up target coordinates, and jacobians.
  // We always compute our source quantities in the inertial frame.
  // But we want our destination quantities in the target frame.
  const Mesh mesh{domain_creator->initial_extents()[element_ids[0].block_id()],
                  Spectral::Basis::Legendre,
                  Spectral::Quadrature::GaussLobatto};
  tnsr::I<DataVector, 3, TargetFrame> target_frame_coords{};
  Jacobian<DataVector, 3, Frame::ElementLogical, Frame::Inertial>
      jacobian_logical_to_inertial{};
  InverseJacobian<DataVector, 3, Frame::ElementLogical, Frame::Inertial>
      inv_jacobian_logical_to_inertial{};
  Jacobian<DataVector, 3, TargetFrame, Frame::Inertial>
      jacobian_target_to_inertial{};
  InverseJacobian<DataVector, 3, TargetFrame, Frame::Inertial>
      inv_jacobian_target_to_inertial{};
  Jacobian<DataVector, 3, Frame::ElementLogical, TargetFrame>
      jacobian_logical_to_target{};
  InverseJacobian<DataVector, 3, Frame::ElementLogical, TargetFrame>
      inv_jacobian_logical_to_target{};
  tnsr::I<DataVector, 3, Frame::Inertial> frame_velocity_grid_to_inertial{};
  if constexpr (IsTimeDependent::value) {
    ElementMap<3, Frame::Grid> map_logical_to_grid{
        element_ids[0],
        domain.blocks()[0].moving_mesh_logical_to_grid_map().get_clone()};
    const auto inv_jacobian_logical_to_grid =
        map_logical_to_grid.inv_jacobian(logical_coordinates(mesh));
    const auto inv_jacobian_grid_to_inertial =
        domain.blocks()[0].moving_mesh_grid_to_inertial_map().inv_jacobian(
            map_logical_to_grid(logical_coordinates(mesh)),
            temporal_id.step_time().value(),
            domain_creator->functions_of_time());
    inv_jacobian_logical_to_inertial =
        InverseJacobian<DataVector, 3, Frame::ElementLogical, Frame::Inertial>(
            mesh.number_of_grid_points(), 0.0);
    for (size_t i = 0; i < 3; ++i) {
      for (size_t j = 0; j < 3; ++j) {
        for (size_t k = 0; k < 3; ++k) {
          inv_jacobian_logical_to_inertial.get(i, j) +=
              inv_jacobian_logical_to_grid.get(k, i) *
              inv_jacobian_grid_to_inertial.get(j, k);
        }
      }
    }
    if constexpr (std::is_same_v<TargetFrame, Frame::Grid>) {
      jacobian_target_to_inertial =
          domain.blocks()[0].moving_mesh_grid_to_inertial_map().jacobian(
              map_logical_to_grid(logical_coordinates(mesh)),
              temporal_id.step_time().value(),
              domain_creator->functions_of_time());
      inv_jacobian_logical_to_target = inv_jacobian_logical_to_grid;
      target_frame_coords = map_logical_to_grid(logical_coordinates(mesh));
    } else {
      static_assert(std::is_same_v<TargetFrame, Frame::Inertial>,
                    "TargetFrame must be the Inertial frame");
      inv_jacobian_logical_to_target = inv_jacobian_logical_to_inertial;
      // jacobian_target_to_inertial is the identity in this case.
      jacobian_target_to_inertial =
          Jacobian<DataVector, 3, TargetFrame, Frame::Inertial>(
              mesh.number_of_grid_points(), 0.0);
      for (size_t i = 0; i < 3; ++i) {
        jacobian_target_to_inertial.get(i, i) = 1.0;
      }
      target_frame_coords =
          domain.blocks()[0].moving_mesh_grid_to_inertial_map()(
              map_logical_to_grid(logical_coordinates(mesh)),
              temporal_id.step_time().value(),
              domain_creator->functions_of_time());
    }
  } else {
    // time-independent.
    static_assert(std::is_same_v<TargetFrame, Frame::Inertial>,
                  "TargetFrame must be the Inertial frame");
    // Don't need to define jacobian_target_to_inertial
    ElementMap<3, Frame::Inertial> map_logical_to_inertial{
        element_ids[0], domain.blocks()[0].stationary_map().get_clone()};
    target_frame_coords = map_logical_to_inertial(logical_coordinates(mesh));
    inv_jacobian_logical_to_inertial =
        map_logical_to_inertial.inv_jacobian(logical_coordinates(mesh));
    inv_jacobian_logical_to_target = inv_jacobian_logical_to_inertial;
  }

  // Set up analytic solution.
  gr::Solutions::KerrSchild solution(1.0, {{0.1, 0.2, 0.3}},
                                     {{0.03, 0.01, 0.02}});
  const auto solution_vars_target_frame = solution.variables(
      target_frame_coords, 0.0,
      typename gr::Solutions::KerrSchild::tags<DataVector, TargetFrame>{});
  const auto& lapse =
      get<gr::Tags::Lapse<DataVector>>(solution_vars_target_frame);
  const auto& shift = get<gr::Tags::Shift<3, TargetFrame, DataVector>>(
      solution_vars_target_frame);
  const auto& spatial_metric =
      get<gr::Tags::SpatialMetric<3, TargetFrame, DataVector>>(
          solution_vars_target_frame);

  // Fill src vars with analytic solution.
  Variables<SrcTags> src_vars(mesh.number_of_grid_points());

  // Inertial metric variables. Needed only if TargetFrame is not Inertial.
  // Set to zero size if TargetFrame is Inertial.
  // Define them here, instead of inside the `if constexpr` below,
  // because we might want to use them in later tests.
  using inertial_metric_vars_tags =
      tmpl::list<gr::Tags::Lapse<DataVector>,
                 gr::Tags::Shift<3, Frame::Inertial, DataVector>,
                 gr::Tags::SpatialMetric<3, Frame::Inertial, DataVector>>;
  Variables<inertial_metric_vars_tags> inertial_metric_vars([&mesh]() {
    if constexpr (std::is_same_v<TargetFrame, Frame::Inertial>) {
      return 0;
      // silence warning 'lambda capture mesh not used'.
      (void)mesh;
    } else {
      return mesh.number_of_grid_points();
    }
  }());

  if constexpr (std::is_same_v<TargetFrame, Frame::Inertial>) {
    // Src vars are always in inertial frame. Here TargetFrame is inertial,
    // so no frame transformation is needed.

    get<::gr::Tags::SpacetimeMetric<3, TargetFrame>>(src_vars) =
        gr::spacetime_metric(lapse, shift, spatial_metric);
  } else {
    static_assert(std::is_same_v<TargetFrame, Frame::Grid>,
                  "TargetFrame must be the Grid frame");
    // Src vars are always in inertial frame. Here TargetFrame is not
    // inertial, so we need to transform to the inertial frame.

    // First figure out jacobians
    const auto coords_frame_velocity_jacobians =
        domain.blocks()[0]
            .moving_mesh_grid_to_inertial_map()
            .coords_frame_velocity_jacobians(
                target_frame_coords, temporal_id.step_time().value(),
                domain_creator->functions_of_time());
    const auto& inv_jacobian_grid_to_inertial =
        std::get<1>(coords_frame_velocity_jacobians);
    const auto& jacobian_grid_to_inertial =
        std::get<2>(coords_frame_velocity_jacobians);
    frame_velocity_grid_to_inertial =
        std::get<3>(coords_frame_velocity_jacobians);
    inv_jacobian_target_to_inertial =
        std::get<1>(coords_frame_velocity_jacobians);
    jacobian_target_to_inertial =
        std::get<2>(coords_frame_velocity_jacobians);

    // Now compute metric variables and transform them into the
    // inertial frame.  We transform lapse, shift, 3-metric.  Then we
    // will numerically differentiate transformed 3-metric because we
    // don't have Hessians and therefore we cannot transform the GH
    // Phi variable directly.

    // Just copy lapse, since it doesn't transform. Need it for derivs.
    get<gr::Tags::Lapse<DataVector>>(inertial_metric_vars) = lapse;

    // Transform shift
    auto& shift_inertial =
        get<::gr::Tags::Shift<3, Frame::Inertial>>(inertial_metric_vars);
    for (size_t k = 0; k < 3; ++k) {
      shift_inertial.get(k) = -frame_velocity_grid_to_inertial.get(k);
      for (size_t j = 0; j < 3; ++j) {
        shift_inertial.get(k) +=
            jacobian_grid_to_inertial.get(k, j) * shift.get(j);
      }
    }

    // Transform lower metric.
    auto& lower_metric_inertial =
        get<::gr::Tags::SpatialMetric<3, Frame::Inertial>>(
            inertial_metric_vars);
    transform::to_different_frame(make_not_null(&lower_metric_inertial),
                                  spatial_metric,
                                  inv_jacobian_grid_to_inertial);

    // Now fill src_vars
    get<::gr::Tags::SpacetimeMetric<3, Frame::Inertial>>(src_vars) =
        gr::spacetime_metric(lapse, shift_inertial, lower_metric_inertial);
  }

  // Compute dest_vars
  Variables<DestTags> dest_vars(mesh.number_of_grid_points());
  if constexpr (IsTimeDependent::value) {
    ah::ComputeExcisionBoundaryVolumeQuantities::apply(
        make_not_null(&dest_vars), src_vars, mesh, jacobian_target_to_inertial,
        inv_jacobian_target_to_inertial, jacobian_logical_to_target,
        inv_jacobian_logical_to_target, frame_velocity_grid_to_inertial);
  } else {
    // time-independent.
    ah::ComputeExcisionBoundaryVolumeQuantities::apply(
        make_not_null(&dest_vars), src_vars, mesh);
  }

  // Now make sure that dest vars are correct.
  if constexpr (tmpl::list_contains_v<
                    DestTags, gr::Tags::SpatialMetric<3, TargetFrame>>) {
    const auto& numerical_spatial_metric =
        get<gr::Tags::SpatialMetric<3, TargetFrame>>(dest_vars);
    CHECK_ITERABLE_APPROX(spatial_metric, numerical_spatial_metric);
  }

  if constexpr (tmpl::list_contains_v<
                    DestTags, gr::Tags::InverseSpatialMetric<3, TargetFrame>>) {
    const auto& inv_spatial_metric =
        get<gr::Tags::InverseSpatialMetric<3, TargetFrame>>(dest_vars);
    CHECK_ITERABLE_APPROX(determinant_and_inverse(spatial_metric).second,
                          inv_spatial_metric);
  }

  if constexpr (tmpl::list_contains_v<DestTags, gr::Tags::Lapse<DataVector>>) {
    const auto& numerical_lapse = get<gr::Tags::Lapse<DataVector>>(dest_vars);
    CHECK_ITERABLE_APPROX(lapse, numerical_lapse);
  }

  if constexpr (tmpl::list_contains_v<DestTags,
                                      gr::Tags::Shift<3, TargetFrame>>) {
    const auto& numerical_shift =
        get<gr::Tags::Shift<3, TargetFrame>>(dest_vars);
    CHECK_ITERABLE_APPROX(shift, numerical_shift);
  }

  // If TargetFrame is not the same as Inertial frame, we allow
  // (optional) computation of destination quantities in the inertial frame.
  // Test these here.
  if constexpr (not std::is_same_v<TargetFrame, Frame::Inertial>) {
    if constexpr (tmpl::list_contains_v<
                      DestTags, gr::Tags::SpatialMetric<3, Frame::Inertial>>) {
      const auto& expected_inertial_spatial_metric =
          get<::gr::Tags::SpatialMetric<3, Frame::Inertial>>(
              inertial_metric_vars);
      const auto& inertial_spatial_metric =
          get<gr::Tags::SpatialMetric<3, Frame::Inertial>>(dest_vars);
      CHECK_ITERABLE_APPROX(expected_inertial_spatial_metric,
                            inertial_spatial_metric);
    }

    if constexpr (tmpl::list_contains_v<
                      DestTags,
                      gr::Tags::InverseSpatialMetric<3, Frame::Inertial>>) {
      const auto expected_inertial_inverse_spatial_metric =
          determinant_and_inverse(
              get<::gr::Tags::SpatialMetric<3, Frame::Inertial>>(
                  inertial_metric_vars))
              .second;
      const auto& inertial_inverse_spatial_metric =
          get<gr::Tags::InverseSpatialMetric<3, Frame::Inertial>>(dest_vars);
      CHECK_ITERABLE_APPROX(expected_inertial_inverse_spatial_metric,
                            inertial_inverse_spatial_metric);
    }

    if constexpr (tmpl::list_contains_v<DestTags,
                                        gr::Tags::Shift<3, Frame::Inertial>>) {
      const auto& expected_inertial_shift =
          get<gr::Tags::Shift<3, Frame::Inertial>>(inertial_metric_vars);
      const auto& inertial_shift =
          get<gr::Tags::Shift<3, Frame::Inertial>>(dest_vars);
      CHECK_ITERABLE_APPROX(expected_inertial_shift, inertial_shift);
    }
  }
}

SPECTRE_TEST_CASE(
    "Unit.ApparentHorizons.ComputeExcisionBoundaryVolumeQuantities",
    "[ApparentHorizons][Unit]") {
  static_assert(
      tt::assert_conforms_to_v<ah::ComputeExcisionBoundaryVolumeQuantities,
                               intrp::protocols::ComputeVarsToInterpolate>);
    // time-independent.
    // All possible tags.
  test_compute_excision_boundary_volume_quantities<
      std::false_type, Frame::Inertial,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Pi<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>,
                 Tags::deriv<GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>,
                             tmpl::size_t<3>, Frame::Inertial>>,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 gr::Tags::SpatialMetric<3, Frame::Inertial>,
                 gr::Tags::Lapse<DataVector>,
                 gr::Tags::Shift<3, Frame::Inertial>>>();

  // Leave out a few tags.
  test_compute_excision_boundary_volume_quantities<
      std::false_type, Frame::Inertial,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Pi<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>>,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 gr::Tags::SpatialMetric<3, Frame::Inertial>,
                 gr::Tags::Lapse<DataVector>>>();

  test_compute_excision_boundary_volume_quantities<
      std::false_type, Frame::Inertial,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Pi<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>>,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 gr::Tags::SpatialMetric<3, Frame::Inertial>,
                 gr::Tags::Shift<3, Frame::Inertial>>>();

  // time-dependent.
  // All possible tags.
  test_compute_excision_boundary_volume_quantities<
      std::true_type, Frame::Grid,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Pi<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>,
                 Tags::deriv<GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>,
                             tmpl::size_t<3>, Frame::Inertial>>,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 gr::Tags::SpatialMetric<3, Frame::Inertial>,
                 gr::Tags::Lapse<DataVector>,
                 gr::Tags::Shift<3, Frame::Inertial>,
                 gr::Tags::Shift<3, Frame::Grid>>>();

  // Leave out a few tags.
  test_compute_excision_boundary_volume_quantities<
      std::true_type, Frame::Grid,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Pi<3, Frame::Inertial>,
                 GeneralizedHarmonic::Tags::Phi<3, Frame::Inertial>>,
      tmpl::list<gr::Tags::SpacetimeMetric<3, Frame::Inertial>,
                 gr::Tags::SpatialMetric<3, Frame::Inertial>>>();
}
}  // namespace
