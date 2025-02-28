// Copyright (c) FIRST and other WPILib contributors.
// Open Source Software; you can modify and/or share it under the terms of
// the WPILib BSD license file in the root directory of this project.

#include "frc/geometry/Pose3d.h"

#include <cmath>

#include <wpi/json.h>

using namespace frc;

namespace {

/**
 * Applies the hat operator to a rotation vector.
 *
 * It takes a rotation vector and returns the corresponding matrix
 * representation of the Lie algebra element (a 3x3 rotation matrix).
 *
 * @param rotation The rotation vector.
 * @return The rotation vector as a 3x3 rotation matrix.
 */
Matrixd<3, 3> RotationVectorToMatrix(const Vectord<3>& rotation) {
  // Given a rotation vector <a, b, c>,
  //         [ 0 -c  b]
  // Omega = [ c  0 -a]
  //         [-b  a  0]
  return Matrixd<3, 3>{{0.0, -rotation(2), rotation(1)},
                       {rotation(2), 0.0, -rotation(0)},
                       {-rotation(1), rotation(0), 0.0}};
}
}  // namespace

Pose3d::Pose3d(Translation3d translation, Rotation3d rotation)
    : m_translation(std::move(translation)), m_rotation(std::move(rotation)) {}

Pose3d::Pose3d(units::meter_t x, units::meter_t y, units::meter_t z,
               Rotation3d rotation)
    : m_translation(x, y, z), m_rotation(std::move(rotation)) {}

Pose3d::Pose3d(const Pose2d& pose)
    : m_translation(pose.X(), pose.Y(), 0_m),
      m_rotation(0_rad, 0_rad, pose.Rotation().Radians()) {}

Pose3d Pose3d::operator+(const Transform3d& other) const {
  return TransformBy(other);
}

Transform3d Pose3d::operator-(const Pose3d& other) const {
  const auto pose = this->RelativeTo(other);
  return Transform3d{pose.Translation(), pose.Rotation()};
}

Pose3d Pose3d::operator*(double scalar) const {
  return Pose3d{m_translation * scalar, m_rotation * scalar};
}

Pose3d Pose3d::operator/(double scalar) const {
  return *this * (1.0 / scalar);
}

Pose3d Pose3d::TransformBy(const Transform3d& other) const {
  return {m_translation + (other.Translation().RotateBy(m_rotation)),
          other.Rotation() + m_rotation};
}

Pose3d Pose3d::RelativeTo(const Pose3d& other) const {
  const Transform3d transform{other, *this};
  return {transform.Translation(), transform.Rotation()};
}

Pose3d Pose3d::Exp(const Twist3d& twist) const {
  // Implementation from Section 3.2 of https://ethaneade.org/lie.pdf
  auto u = Vectord<3>{twist.dx.value(), twist.dy.value(), twist.dz.value()};
  auto rvec = Vectord<3>{twist.rx.value(), twist.ry.value(), twist.rz.value()};
  auto omega = RotationVectorToMatrix(rvec);
  auto omegaSq = omega * omega;
  auto theta = rvec.norm();
  auto thetaSq = theta * theta;

  double A;
  double B;
  double C;
  if (theta < 1E-9) {
    // Taylor Expansions around θ = 0
    // A = 1/1! - θ²/3! + θ⁴/5!
    // B = 1/2! - θ²/4! + θ⁴/6!
    // C = 1/3! - θ²/5! + θ⁴/7!
    A = 1 - thetaSq / 6 + thetaSq * thetaSq / 120;
    B = 1 / 2.0 - thetaSq / 24 + thetaSq * thetaSq / 720;
    C = 1 / 6.0 - thetaSq / 120 + thetaSq * thetaSq / 5040;
  } else {
    // A = std::sin(θ)/θ
    // B = (1 - std::cos(θ)) / θ²
    // C = (1 - A) / θ²
    A = std::sin(theta) / theta;
    B = (1 - std::cos(theta)) / thetaSq;
    C = (1 - A) / thetaSq;
  }

  auto R = Matrixd<3, 3>::Identity() + A * omega + B * omegaSq;
  auto V = Matrixd<3, 3>::Identity() + B * omega + C * omegaSq;

  auto translation_component = V * u;
  const Transform3d transform{
      Translation3d{units::meter_t{translation_component(0)},
                    units::meter_t{translation_component(1)},
                    units::meter_t{translation_component(2)}},
      Rotation3d{R}};

  return *this + transform;
}

Twist3d Pose3d::Log(const Pose3d& end) const {
  // Implementation from Section 3.2 of https://ethaneade.org/lie.pdf
  const auto transform = end.RelativeTo(*this);

  auto u = Vectord<3>{transform.X().value(), transform.Y().value(),
                      transform.Z().value()};
  auto rvec = transform.Rotation().GetQuaternion().ToRotationVector();

  auto omega = RotationVectorToMatrix(rvec);
  auto omegaSq = omega * omega;
  auto theta = rvec.norm();
  auto thetaSq = theta * theta;

  double C;
  if (theta < 1E-9) {
    // Taylor Expansions around θ = 0
    // A = 1/1! - θ²/3! + θ⁴/5!
    // B = 1/2! - θ²/4! + θ⁴/6!
    // C = 1/6 * (1/2 + θ²/5! + θ⁴/7!)
    C = 1 / 6.0 - thetaSq / 120 + thetaSq * thetaSq / 5040;
  } else {
    // A = std::sin(θ)/θ
    // B = (1 - std::cos(θ)) / θ²
    // C = (1 - A/(2*B)) / θ²
    double A = std::sin(theta) / theta;
    double B = (1 - std::cos(theta)) / thetaSq;
    C = (1 - A / (2 * B)) / thetaSq;
  }

  auto V_inv = Matrixd<3, 3>::Identity() - 0.5 * omega + C * omegaSq;

  auto translation_component = V_inv * u;

  return Twist3d{units::meter_t{translation_component(0)},
                 units::meter_t{translation_component(1)},
                 units::meter_t{translation_component(2)},
                 units::radian_t{rvec(0)},
                 units::radian_t{rvec(1)},
                 units::radian_t{rvec(2)}};
}

Pose2d Pose3d::ToPose2d() const {
  return Pose2d{m_translation.X(), m_translation.Y(), m_rotation.Z()};
}

void frc::to_json(wpi::json& json, const Pose3d& pose) {
  json = wpi::json{{"translation", pose.Translation()},
                   {"rotation", pose.Rotation()}};
}

void frc::from_json(const wpi::json& json, Pose3d& pose) {
  pose = Pose3d{json.at("translation").get<Translation3d>(),
                json.at("rotation").get<Rotation3d>()};
}
