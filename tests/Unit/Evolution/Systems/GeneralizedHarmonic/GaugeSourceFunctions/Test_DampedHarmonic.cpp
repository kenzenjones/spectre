// Distributed under the MIT License.
// See LICENSE.txt for details.

#include "Framework/TestingFramework.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <pup.h>
#include <random>
#include <string>
#include <utility>

#include "DataStructures/DataVector.hpp"
#include "DataStructures/Tensor/EagerMath/DeterminantAndInverse.hpp"
#include "DataStructures/Tensor/Tensor.hpp"
#include "Evolution/Systems/GeneralizedHarmonic/GaugeSourceFunctions/DampedHarmonic.hpp"
#include "Evolution/Systems/GeneralizedHarmonic/GaugeSourceFunctions/DampedWaveHelpers.hpp"
#include "Evolution/Systems/GeneralizedHarmonic/GaugeSourceFunctions/Gauges.hpp"
#include "Evolution/Systems/GeneralizedHarmonic/GaugeSourceFunctions/RegisterDerived.hpp"
#include "Evolution/Systems/GeneralizedHarmonic/Tags.hpp"
#include "Framework/CheckWithRandomValues.hpp"
#include "Framework/SetupLocalPythonEnvironment.hpp"
#include "Framework/TestCreation.hpp"
#include "Framework/TestHelpers.hpp"
#include "Helpers/DataStructures/DataBox/TestHelpers.hpp"
#include "Helpers/DataStructures/MakeWithRandomValues.hpp"
#include "Options/ParseOptions.hpp"
#include "PointwiseFunctions/GeneralRelativity/Lapse.hpp"
#include "PointwiseFunctions/GeneralRelativity/Shift.hpp"
#include "PointwiseFunctions/GeneralRelativity/SpacetimeNormalOneForm.hpp"
#include "PointwiseFunctions/GeneralRelativity/SpatialMetric.hpp"
#include "Utilities/Gsl.hpp"
#include "Utilities/TMPL.hpp"

namespace {
// The return-by-value implementations of spatial_weight_function and
// spacetime_deriv_of_spatial_weight_function are intentionally only available
// in the test because while convenient the additional allocations are bad for
// performance. By not having them available in the production code we avoid
// possible accidental usage.
template <size_t SpatialDim, typename Frame, typename DataType>
Scalar<DataType> spatial_weight_function(
    const tnsr::I<DataType, SpatialDim, Frame>& coords, const double sigma_r) {
  Scalar<DataType> spatial_weight{};
  spatial_weight_function(make_not_null(&spatial_weight), coords, sigma_r);
  return spatial_weight;
}

template <size_t SpatialDim, typename Frame, typename DataType>
tnsr::a<DataType, SpatialDim, Frame> spacetime_deriv_of_spatial_weight_function(
    const tnsr::I<DataType, SpatialDim, Frame>& coords, const double sigma_r) {
  tnsr::a<DataType, SpatialDim, Frame> d4_weight{};
  spacetime_deriv_of_spatial_weight_function(
      make_not_null(&d4_weight), coords, sigma_r,
      spatial_weight_function(coords, sigma_r));
  return d4_weight;
}

template <size_t SpatialDim, typename Frame, typename DataType>
void test_rollon_function(const DataType& used_for_size) {
  INFO("Test rollon function");
  // roll_on_function
  pypp::check_with_random_values<1>(
      &::GeneralizedHarmonic::gauges::DampedHarmonicGauge_detail::
          roll_on_function,
      "Evolution.Systems.GeneralizedHarmonic.GaugeSourceFunctions."
      "DampedHarmonic",
      "roll_on_function", {{{std::numeric_limits<double>::denorm_min(), 1.}}},
      used_for_size);
  // time_deriv_of_roll_on_function
  pypp::check_with_random_values<1>(
      &::GeneralizedHarmonic::gauges::DampedHarmonicGauge_detail::
          time_deriv_of_roll_on_function,
      "Evolution.Systems.GeneralizedHarmonic.GaugeSourceFunctions."
      "DampedHarmonic",
      "time_deriv_roll_on_function",
      {{{std::numeric_limits<double>::denorm_min(), 1.}}}, used_for_size);
}

//  Tests of the damped harmonic gauge source function and its spacetime
//  derivative. We need a wrapper because pypp does not currently support
//  integer types.
template <size_t SpatialDim, typename Frame>
void wrap_damped_harmonic_rollon(
    const gsl::not_null<tnsr::a<DataVector, SpatialDim, Frame>*> gauge_h,
    const gsl::not_null<tnsr::ab<DataVector, SpatialDim, Frame>*> d4_gauge_h,
    const tnsr::a<DataVector, SpatialDim, Frame>& gauge_h_init,
    const tnsr::ab<DataVector, SpatialDim, Frame>& dgauge_h_init,
    tnsr::aa<DataVector, SpatialDim, Frame> spacetime_metric,
    const tnsr::aa<DataVector, SpatialDim, Frame>& pi,
    const tnsr::iaa<DataVector, SpatialDim, Frame>& phi, double time,
    const tnsr::I<DataVector, SpatialDim, Frame>& coords,
    const double amp_coef_L1, const double amp_coef_L2, const double amp_coef_S,
    const double rollon_start_time, const double rollon_width,
    const double sigma_r) {
  get<0, 0>(spacetime_metric) -= 1.0;
  for (size_t i = 0; i < SpatialDim; ++i) {
    spacetime_metric.get(i + 1, i + 1) += 1.0;
  }
  const auto spatial_metric = gr::spatial_metric(spacetime_metric);
  const auto [det_spatial_metric, inverse_spatial_metric] =
      determinant_and_inverse(spatial_metric);
  const Scalar<DataVector> sqrt_det_spatial_metric{
      DataVector{sqrt(get(det_spatial_metric))}};
  const auto shift = gr::shift(spacetime_metric, inverse_spatial_metric);
  const auto lapse = gr::lapse(shift, spacetime_metric);
  const auto spacetime_unit_normal_one_form =
      gr::spacetime_normal_one_form<SpatialDim, Frame>(lapse);
  GeneralizedHarmonic::gauges::damped_harmonic_rollon(
      gauge_h, d4_gauge_h, gauge_h_init, dgauge_h_init, lapse, shift,
      spacetime_unit_normal_one_form, sqrt_det_spatial_metric,
      inverse_spatial_metric, spacetime_metric, pi, phi, time, coords,
      amp_coef_L1, amp_coef_L2, amp_coef_S, 4, 4, 4, rollon_start_time,
      rollon_width, sigma_r);
}

template <size_t SpatialDim, typename Frame>
void wrap_damped_harmonic(
    const gsl::not_null<tnsr::a<DataVector, SpatialDim, Frame>*> gauge_h,
    const gsl::not_null<tnsr::ab<DataVector, SpatialDim, Frame>*> d4_gauge_h,
    tnsr::aa<DataVector, SpatialDim, Frame> spacetime_metric,
    const tnsr::aa<DataVector, SpatialDim, Frame>& pi,
    const tnsr::iaa<DataVector, SpatialDim, Frame>& phi,
    const tnsr::I<DataVector, SpatialDim, Frame>& coords,
    const double amp_coef_L1, const double amp_coef_L2, const double amp_coef_S,
    const double sigma_r) {
  get<0, 0>(spacetime_metric) -= 1.0;
  for (size_t i = 0; i < SpatialDim; ++i) {
    spacetime_metric.get(i + 1, i + 1) += 1.0;
  }

  const auto spatial_metric = gr::spatial_metric(spacetime_metric);
  const auto [det_spatial_metric, inverse_spatial_metric] =
      determinant_and_inverse(spatial_metric);
  const Scalar<DataVector> sqrt_det_spatial_metric{
      DataVector{sqrt(get(det_spatial_metric))}};
  const auto shift = gr::shift(spacetime_metric, inverse_spatial_metric);
  const auto lapse = gr::lapse(shift, spacetime_metric);
  const auto spacetime_unit_normal_one_form =
      gr::spacetime_normal_one_form<SpatialDim, Frame>(lapse);
  GeneralizedHarmonic::gauges::damped_harmonic(
      gauge_h, d4_gauge_h, lapse, shift, spacetime_unit_normal_one_form,
      sqrt_det_spatial_metric, inverse_spatial_metric, spacetime_metric, pi,
      phi, coords, amp_coef_L1, amp_coef_L2, amp_coef_S, 4, 4, 4, sigma_r);
}

// Compare with Python implementation
template <size_t SpatialDim, typename Frame>
void test_with_python(const DataVector& used_for_size) {
  INFO("Test with python");
  CAPTURE(SpatialDim);
  CAPTURE(Frame{});
  pypp::check_with_random_values<1>(
      &wrap_damped_harmonic_rollon<SpatialDim, Frame>,
      "Evolution.Systems.GeneralizedHarmonic.GaugeSourceFunctions."
      "DampedHarmonic",
      {"damped_harmonic_gauge_source_function_rollon",
       "spacetime_deriv_damped_harmonic_gauge_source_function_rollon"},
      {{{-0.01, 0.01}}}, used_for_size);

  pypp::check_with_random_values<1>(
      &wrap_damped_harmonic<SpatialDim, Frame>,
      "Evolution.Systems.GeneralizedHarmonic.GaugeSourceFunctions."
      "DampedHarmonic",
      {"damped_harmonic_gauge_source_function",
       "spacetime_deriv_damped_harmonic_gauge_source_function"},
      {{{-0.01, 0.01}}}, used_for_size);
}

template <size_t Dim>
struct Metavariables {
  struct factory_creation
      : tt::ConformsTo<Options::protocols::FactoryCreation> {
    using factory_classes = tmpl::map<
        tmpl::pair<GeneralizedHarmonic::gauges::GaugeCondition,
                   tmpl::list<GeneralizedHarmonic::gauges::DampedHarmonic>>>;
  };
};

template <size_t Dim>
void test_derived_class(const Mesh<Dim>& mesh) {
  CAPTURE(Dim);

  const auto gauge_condition = serialize_and_deserialize(
      TestHelpers::test_creation<
          std::unique_ptr<GeneralizedHarmonic::gauges::GaugeCondition>,
          Metavariables<Dim>>("DampedHarmonic:\n"
                              "  SpatialDecayWidth: 100.0\n"
                              "  Amplitudes: [0.5, 1.5, 2.5]\n"
                              "  Exponents: [2, 4, 6]\n")
          ->get_clone());

  const size_t num_points = mesh.number_of_grid_points();

  const double time = 1.2;
  MAKE_GENERATOR(gen);
  std::uniform_real_distribution<> deriv_dist(-1.e-5, 1.e-5);
  std::uniform_real_distribution<> metric_dist(0.1, 1.0);
  const auto pi =
      make_with_random_values<tnsr::aa<DataVector, Dim, Frame::Inertial>>(
          make_not_null(&gen), make_not_null(&deriv_dist), num_points);
  const auto phi =
      make_with_random_values<tnsr::iaa<DataVector, Dim, Frame::Inertial>>(
          make_not_null(&gen), make_not_null(&deriv_dist), num_points);

  auto spacetime_metric =
      make_with_random_values<tnsr::aa<DataVector, Dim, Frame::Inertial>>(
          make_not_null(&gen), make_not_null(&metric_dist), num_points);
  get<0, 0>(spacetime_metric) += -2.0;
  for (size_t i = 0; i < Dim; ++i) {
    spacetime_metric.get(i + 1, i + 1) += 4.0;
    spacetime_metric.get(i + 1, 0) *= 0.01;
  }
  const auto spatial_metric = gr::spatial_metric(spacetime_metric);
  const auto [det_spatial_metric, inverse_spatial_metric] =
      determinant_and_inverse(spatial_metric);
  const Scalar<DataVector> sqrt_det_spatial_metric(
      sqrt(get(det_spatial_metric)));
  const auto shift = gr::shift(spacetime_metric, inverse_spatial_metric);
  const auto lapse = gr::lapse(shift, spacetime_metric);
  const auto spacetime_normal_one_form =
      gr::spacetime_normal_one_form<Dim, Frame::Inertial>(lapse);
  const auto inverse_spacetime_metric =
      determinant_and_inverse(spacetime_metric).second;

  std::uniform_real_distribution<> coords_dist(1.0, 100.0);
  const auto inertial_coords =
      make_with_random_values<tnsr::I<DataVector, Dim, Frame::Inertial>>(
          make_not_null(&gen), make_not_null(&coords_dist), num_points);

  tnsr::a<DataVector, Dim, Frame::Inertial> gauge_h(num_points);
  tnsr::ab<DataVector, Dim, Frame::Inertial> d4_gauge_h(num_points);
  dynamic_cast<const GeneralizedHarmonic::gauges::DampedHarmonic&>(
      *gauge_condition)
      .gauge_and_spacetime_derivative(
          make_not_null(&gauge_h), make_not_null(&d4_gauge_h), lapse, shift,
          spacetime_normal_one_form, sqrt_det_spatial_metric,
          inverse_spatial_metric, spacetime_metric, pi, phi, time,
          inertial_coords);

  tnsr::a<DataVector, Dim, Frame::Inertial> expected_gauge_h(num_points);
  tnsr::ab<DataVector, Dim, Frame::Inertial> expected_d4_gauge_h(num_points);
  GeneralizedHarmonic::gauges::damped_harmonic(
      make_not_null(&expected_gauge_h), make_not_null(&expected_d4_gauge_h),
      lapse, shift, spacetime_normal_one_form, sqrt_det_spatial_metric,
      inverse_spatial_metric, spacetime_metric, pi, phi, inertial_coords, 0.5,
      1.5, 2.5, 2, 4, 6, 100.0);

  CHECK_ITERABLE_APPROX(gauge_h, expected_gauge_h);
  CHECK_ITERABLE_APPROX(d4_gauge_h, expected_d4_gauge_h);
}
}  // namespace

SPECTRE_TEST_CASE(
    "Unit.Evolution.Systems.GeneralizedHarmonic.Gauge.DampedHarmonic",
    "[Unit][Evolution]") {
  pypp::SetupLocalPythonEnvironment local_python_env{""};
  const DataVector used_for_size(5);

  test_rollon_function<1, Frame::Inertial>(used_for_size);
  test_rollon_function<2, Frame::Inertial>(used_for_size);
  test_rollon_function<3, Frame::Inertial>(used_for_size);

  test_rollon_function<1, Frame::Inertial>(1.);
  test_rollon_function<2, Frame::Inertial>(1.);
  test_rollon_function<3, Frame::Inertial>(1.);

  // Compare with Python implementation
  test_with_python<1, Frame::Inertial>(used_for_size);
  test_with_python<2, Frame::Inertial>(used_for_size);
  test_with_python<3, Frame::Inertial>(used_for_size);

  // Check the derived class for input file creation works.
  GeneralizedHarmonic::gauges::register_derived_with_charm();
  for (const auto& basis_and_quadrature :
       {std::pair{Spectral::Basis::Legendre,
                  Spectral::Quadrature::GaussLobatto},
        {Spectral::Basis::FiniteDifference,
         Spectral::Quadrature::CellCentered}}) {
    test_derived_class<1>(
        {5, basis_and_quadrature.first, basis_and_quadrature.second});
    test_derived_class<2>(
        {5, basis_and_quadrature.first, basis_and_quadrature.second});
    test_derived_class<3>(
        {5, basis_and_quadrature.first, basis_and_quadrature.second});
  }
}
