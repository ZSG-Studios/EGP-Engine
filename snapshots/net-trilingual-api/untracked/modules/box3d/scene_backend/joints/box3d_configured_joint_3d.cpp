// SPDX-License-Identifier: MIT
#include "box3d_configured_joint_3d.hpp"

#include "../misc/type_conversions.hpp"
#include "../objects/box3d_body_impl_3d.hpp"
#include "../spaces/box3d_space_3d.hpp"

#include <cfloat>
Box3DConfiguredJoint3D::Box3DConfiguredJoint3D(PS3DE::JointType p_type, Box3DBodyImpl3D *p_a, Box3DBodyImpl3D *p_b, const Transform3D &p_frame_a, const Transform3D &p_frame_b) :
		Box3DJointImpl3D(p_a, p_b, p_frame_a, p_frame_b), type(p_type) {
	configuration = default_configuration(p_type);
}
Dictionary Box3DConfiguredJoint3D::default_configuration(PS3DE::JointType p_type) {
	Dictionary result;
	switch (p_type) {
		case PS3DE::JOINT_TYPE_DISTANCE: {
			const auto def = b3DefaultDistanceJointDef();
			result["length"] = def.length;
			result["enable_spring"] = def.enableSpring;
			result["lower_spring_force"] = def.lowerSpringForce;
			result["upper_spring_force"] = def.upperSpringForce;
			result["hertz"] = def.hertz;
			result["damping_ratio"] = def.dampingRatio;
			result["enable_limit"] = def.enableLimit;
			result["min_length"] = def.minLength;
			result["max_length"] = def.maxLength;
			result["enable_motor"] = def.enableMotor;
			result["max_motor_force"] = def.maxMotorForce;
			result["motor_speed"] = def.motorSpeed;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_FILTER: {
			const auto def = b3DefaultFilterJointDef();
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_MOTOR: {
			const auto def = b3DefaultMotorJointDef();
			result["linear_velocity"] = b3_to_godot(def.linearVelocity);
			result["max_velocity_force"] = def.maxVelocityForce;
			result["angular_velocity"] = b3_to_godot(def.angularVelocity);
			result["max_velocity_torque"] = def.maxVelocityTorque;
			result["linear_hertz"] = def.linearHertz;
			result["linear_damping_ratio"] = def.linearDampingRatio;
			result["max_spring_force"] = def.maxSpringForce;
			result["angular_hertz"] = def.angularHertz;
			result["angular_damping_ratio"] = def.angularDampingRatio;
			result["max_spring_torque"] = def.maxSpringTorque;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_PARALLEL: {
			const auto def = b3DefaultParallelJointDef();
			result["hertz"] = def.hertz;
			result["damping_ratio"] = def.dampingRatio;
			result["max_torque"] = def.maxTorque;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_PRISMATIC: {
			const auto def = b3DefaultPrismaticJointDef();
			result["enable_spring"] = def.enableSpring;
			result["hertz"] = def.hertz;
			result["damping_ratio"] = def.dampingRatio;
			result["target_translation"] = def.targetTranslation;
			result["enable_limit"] = def.enableLimit;
			result["lower_translation"] = def.lowerTranslation;
			result["upper_translation"] = def.upperTranslation;
			result["enable_motor"] = def.enableMotor;
			result["max_motor_force"] = def.maxMotorForce;
			result["motor_speed"] = def.motorSpeed;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_REVOLUTE: {
			const auto def = b3DefaultRevoluteJointDef();
			result["target_angle"] = def.targetAngle;
			result["enable_spring"] = def.enableSpring;
			result["hertz"] = def.hertz;
			result["damping_ratio"] = def.dampingRatio;
			result["enable_limit"] = def.enableLimit;
			result["lower_angle"] = def.lowerAngle;
			result["upper_angle"] = def.upperAngle;
			result["enable_motor"] = def.enableMotor;
			result["max_motor_torque"] = def.maxMotorTorque;
			result["motor_speed"] = def.motorSpeed;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_SPHERICAL: {
			const auto def = b3DefaultSphericalJointDef();
			result["enable_spring"] = def.enableSpring;
			result["hertz"] = def.hertz;
			result["damping_ratio"] = def.dampingRatio;
			result["target_rotation"] = b3_to_godot(def.targetRotation);
			result["enable_cone_limit"] = def.enableConeLimit;
			result["cone_angle"] = def.coneAngle;
			result["enable_twist_limit"] = def.enableTwistLimit;
			result["lower_twist_angle"] = def.lowerTwistAngle;
			result["upper_twist_angle"] = def.upperTwistAngle;
			result["enable_motor"] = def.enableMotor;
			result["max_motor_torque"] = def.maxMotorTorque;
			result["motor_velocity"] = b3_to_godot(def.motorVelocity);
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_WELD: {
			const auto def = b3DefaultWeldJointDef();
			result["linear_hertz"] = def.linearHertz;
			result["angular_hertz"] = def.angularHertz;
			result["linear_damping_ratio"] = def.linearDampingRatio;
			result["angular_damping_ratio"] = def.angularDampingRatio;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS3DE::JOINT_TYPE_WHEEL: {
			const auto def = b3DefaultWheelJointDef();
			result["enable_suspension_spring"] = def.enableSuspensionSpring;
			result["suspension_hertz"] = def.suspensionHertz;
			result["suspension_damping_ratio"] = def.suspensionDampingRatio;
			result["enable_suspension_limit"] = def.enableSuspensionLimit;
			result["lower_suspension_limit"] = def.lowerSuspensionLimit;
			result["upper_suspension_limit"] = def.upperSuspensionLimit;
			result["enable_spin_motor"] = def.enableSpinMotor;
			result["max_spin_torque"] = def.maxSpinTorque;
			result["spin_speed"] = def.spinSpeed;
			result["enable_steering"] = def.enableSteering;
			result["steering_hertz"] = def.steeringHertz;
			result["steering_damping_ratio"] = def.steeringDampingRatio;
			result["target_steering_angle"] = def.targetSteeringAngle;
			result["max_steering_torque"] = def.maxSteeringTorque;
			result["enable_steering_limit"] = def.enableSteeringLimit;
			result["lower_steering_limit"] = def.lowerSteeringLimit;
			result["upper_steering_limit"] = def.upperSteeringLimit;
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		default:
			return Dictionary();
	}
	return result;
}
bool Box3DConfiguredJoint3D::validate_configuration(PS3DE::JointType p_type, const Dictionary &p_configuration) {
	Dictionary candidate = default_configuration(p_type);
	ERR_FAIL_COND_V(candidate.is_empty(), false);
	for (const Variant &key : p_configuration.get_key_list()) {
		ERR_FAIL_COND_V(!candidate.has(key), false);
		Variant value = p_configuration[key];
		Variant original = candidate[key];
		if (original.get_type() == Variant::BOOL) {
			ERR_FAIL_COND_V(value.get_type() != Variant::BOOL, false);
		} else if (original.get_type() == Variant::FLOAT || original.get_type() == Variant::INT) {
			ERR_FAIL_COND_V(value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT, false);
			double number = value;
			ERR_FAIL_COND_V(!Math::is_finite(number) || Math::abs(number) > FLT_MAX, false);
			String name = key;
			if (name.contains("hertz") || name.contains("damping") || name.begins_with("max_") || name.ends_with("threshold") || name == "length" || name == "min_length" || name == "upper_spring_force") {
				ERR_FAIL_COND_V(number < 0, false);
			}
			if (name == "lower_spring_force") {
				ERR_FAIL_COND_V(number > 0, false);
			}
		} else {
			ERR_FAIL_COND_V(value.get_type() != original.get_type(), false);
			if (value.get_type() == Variant::VECTOR3) {
				Vector3 item = value;
				ERR_FAIL_COND_V(!item.is_finite(), false);
			}
			if (value.get_type() == Variant::QUATERNION) {
				Quaternion item = value;
				ERR_FAIL_COND_V(!item.is_finite(), false);
				ERR_FAIL_COND_V(!item.is_normalized(), false);
			}
		}
		candidate[key] = value;
	}
	switch (p_type) {
		case PS3DE::JOINT_TYPE_DISTANCE:
			ERR_FAIL_COND_V(double(candidate["lower_spring_force"]) > double(candidate["upper_spring_force"]), false);
			ERR_FAIL_COND_V(double(candidate["min_length"]) > double(candidate["max_length"]), false);
			break;
		case PS3DE::JOINT_TYPE_PRISMATIC:
			ERR_FAIL_COND_V(double(candidate["lower_translation"]) > double(candidate["upper_translation"]), false);
			break;
		case PS3DE::JOINT_TYPE_REVOLUTE:
			ERR_FAIL_COND_V(double(candidate["lower_angle"]) > double(candidate["upper_angle"]), false);
			ERR_FAIL_COND_V(double(candidate["lower_angle"]) < -0.99 * Math::PI || double(candidate["upper_angle"]) > 0.99 * Math::PI, false);
			break;
		case PS3DE::JOINT_TYPE_SPHERICAL:
			ERR_FAIL_COND_V(double(candidate["lower_twist_angle"]) > double(candidate["upper_twist_angle"]), false);
			ERR_FAIL_COND_V(double(candidate["lower_twist_angle"]) < -0.99 * Math::PI || double(candidate["upper_twist_angle"]) > 0.99 * Math::PI, false);
			ERR_FAIL_COND_V(double(candidate["cone_angle"]) < 0 || double(candidate["cone_angle"]) > Math::PI, false);
			break;
		case PS3DE::JOINT_TYPE_WHEEL:
			ERR_FAIL_COND_V(double(candidate["lower_suspension_limit"]) > double(candidate["upper_suspension_limit"]), false);
			ERR_FAIL_COND_V(double(candidate["lower_steering_limit"]) > double(candidate["upper_steering_limit"]), false);
			ERR_FAIL_COND_V(double(candidate["lower_steering_limit"]) < -0.99 * Math::PI || double(candidate["upper_steering_limit"]) > 0.99 * Math::PI, false);
			break;
		default:
			break;
	}
	return true;
}
bool Box3DConfiguredJoint3D::set_configuration(const Dictionary &p_configuration) {
	Dictionary candidate = configuration.duplicate();
	candidate.merge(p_configuration, true);
	if (!validate_configuration(type, candidate)) {
		return false;
	}
	configuration = candidate;
	set_collision_disabled(!bool(configuration["collide_connected"]));
	_apply_configuration(p_configuration);
	return true;
}
b3JointId Box3DConfiguredJoint3D::_create_joint_id(b3WorldId p_world, b3BodyId p_a, b3BodyId p_b, b3Transform p_frame_a, b3Transform p_frame_b) {
	switch (type) {
		case PS3DE::JOINT_TYPE_DISTANCE: {
			auto def = b3DefaultDistanceJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.length = (float)configuration["length"];
			def.enableSpring = (bool)configuration["enable_spring"];
			def.lowerSpringForce = (float)configuration["lower_spring_force"];
			def.upperSpringForce = (float)configuration["upper_spring_force"];
			def.hertz = (float)configuration["hertz"];
			def.dampingRatio = (float)configuration["damping_ratio"];
			def.enableLimit = (bool)configuration["enable_limit"];
			def.minLength = (float)configuration["min_length"];
			def.maxLength = (float)configuration["max_length"];
			def.enableMotor = (bool)configuration["enable_motor"];
			def.maxMotorForce = (float)configuration["max_motor_force"];
			def.motorSpeed = (float)configuration["motor_speed"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateDistanceJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_FILTER: {
			auto def = b3DefaultFilterJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateFilterJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_MOTOR: {
			auto def = b3DefaultMotorJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.linearVelocity = godot_to_b3(Vector3(configuration["linear_velocity"]));
			def.maxVelocityForce = (float)configuration["max_velocity_force"];
			def.angularVelocity = godot_to_b3(Vector3(configuration["angular_velocity"]));
			def.maxVelocityTorque = (float)configuration["max_velocity_torque"];
			def.linearHertz = (float)configuration["linear_hertz"];
			def.linearDampingRatio = (float)configuration["linear_damping_ratio"];
			def.maxSpringForce = (float)configuration["max_spring_force"];
			def.angularHertz = (float)configuration["angular_hertz"];
			def.angularDampingRatio = (float)configuration["angular_damping_ratio"];
			def.maxSpringTorque = (float)configuration["max_spring_torque"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateMotorJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_PARALLEL: {
			auto def = b3DefaultParallelJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.hertz = (float)configuration["hertz"];
			def.dampingRatio = (float)configuration["damping_ratio"];
			def.maxTorque = (float)configuration["max_torque"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateParallelJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_PRISMATIC: {
			auto def = b3DefaultPrismaticJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.enableSpring = (bool)configuration["enable_spring"];
			def.hertz = (float)configuration["hertz"];
			def.dampingRatio = (float)configuration["damping_ratio"];
			def.targetTranslation = (float)configuration["target_translation"];
			def.enableLimit = (bool)configuration["enable_limit"];
			def.lowerTranslation = (float)configuration["lower_translation"];
			def.upperTranslation = (float)configuration["upper_translation"];
			def.enableMotor = (bool)configuration["enable_motor"];
			def.maxMotorForce = (float)configuration["max_motor_force"];
			def.motorSpeed = (float)configuration["motor_speed"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreatePrismaticJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_REVOLUTE: {
			auto def = b3DefaultRevoluteJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.targetAngle = (float)configuration["target_angle"];
			def.enableSpring = (bool)configuration["enable_spring"];
			def.hertz = (float)configuration["hertz"];
			def.dampingRatio = (float)configuration["damping_ratio"];
			def.enableLimit = (bool)configuration["enable_limit"];
			def.lowerAngle = (float)configuration["lower_angle"];
			def.upperAngle = (float)configuration["upper_angle"];
			def.enableMotor = (bool)configuration["enable_motor"];
			def.maxMotorTorque = (float)configuration["max_motor_torque"];
			def.motorSpeed = (float)configuration["motor_speed"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateRevoluteJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_SPHERICAL: {
			auto def = b3DefaultSphericalJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.enableSpring = (bool)configuration["enable_spring"];
			def.hertz = (float)configuration["hertz"];
			def.dampingRatio = (float)configuration["damping_ratio"];
			def.targetRotation = godot_to_b3(Quaternion(configuration["target_rotation"]));
			def.enableConeLimit = (bool)configuration["enable_cone_limit"];
			def.coneAngle = (float)configuration["cone_angle"];
			def.enableTwistLimit = (bool)configuration["enable_twist_limit"];
			def.lowerTwistAngle = (float)configuration["lower_twist_angle"];
			def.upperTwistAngle = (float)configuration["upper_twist_angle"];
			def.enableMotor = (bool)configuration["enable_motor"];
			def.maxMotorTorque = (float)configuration["max_motor_torque"];
			def.motorVelocity = godot_to_b3(Vector3(configuration["motor_velocity"]));
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateSphericalJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_WELD: {
			auto def = b3DefaultWeldJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.linearHertz = (float)configuration["linear_hertz"];
			def.angularHertz = (float)configuration["angular_hertz"];
			def.linearDampingRatio = (float)configuration["linear_damping_ratio"];
			def.angularDampingRatio = (float)configuration["angular_damping_ratio"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateWeldJoint(p_world, &def);
		}
		case PS3DE::JOINT_TYPE_WHEEL: {
			auto def = b3DefaultWheelJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.enableSuspensionSpring = (bool)configuration["enable_suspension_spring"];
			def.suspensionHertz = (float)configuration["suspension_hertz"];
			def.suspensionDampingRatio = (float)configuration["suspension_damping_ratio"];
			def.enableSuspensionLimit = (bool)configuration["enable_suspension_limit"];
			def.lowerSuspensionLimit = (float)configuration["lower_suspension_limit"];
			def.upperSuspensionLimit = (float)configuration["upper_suspension_limit"];
			def.enableSpinMotor = (bool)configuration["enable_spin_motor"];
			def.maxSpinTorque = (float)configuration["max_spin_torque"];
			def.spinSpeed = (float)configuration["spin_speed"];
			def.enableSteering = (bool)configuration["enable_steering"];
			def.steeringHertz = (float)configuration["steering_hertz"];
			def.steeringDampingRatio = (float)configuration["steering_damping_ratio"];
			def.targetSteeringAngle = (float)configuration["target_steering_angle"];
			def.maxSteeringTorque = (float)configuration["max_steering_torque"];
			def.enableSteeringLimit = (bool)configuration["enable_steering_limit"];
			def.lowerSteeringLimit = (float)configuration["lower_steering_limit"];
			def.upperSteeringLimit = (float)configuration["upper_steering_limit"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			return b3CreateWheelJoint(p_world, &def);
		}
		default:
			return b3_nullJointId;
	}
}
void Box3DConfiguredJoint3D::_apply_configuration(const Dictionary &p_changed) {
	if (!has_joint_id()) {
		return;
	}
	const b3JointId id = get_joint_id();
	if (p_changed.has("force_threshold")) {
		b3Joint_SetForceThreshold(id, float(configuration["force_threshold"]));
	}
	if (p_changed.has("torque_threshold")) {
		b3Joint_SetTorqueThreshold(id, float(configuration["torque_threshold"]));
	}
	if (p_changed.has("constraint_hertz") || p_changed.has("constraint_damping_ratio")) {
		b3Joint_SetConstraintTuning(id, float(configuration["constraint_hertz"]), float(configuration["constraint_damping_ratio"]));
	}
	switch (type) {
		case PS3DE::JOINT_TYPE_DISTANCE:
			if (p_changed.has("lower_spring_force") || p_changed.has("upper_spring_force")) {
				b3DistanceJoint_SetSpringForceRange(id, float(configuration["lower_spring_force"]), float(configuration["upper_spring_force"]));
			}
			if (p_changed.has("min_length") || p_changed.has("max_length")) {
				b3DistanceJoint_SetLengthRange(id, float(configuration["min_length"]), float(configuration["max_length"]));
			}
			if (p_changed.has("length")) {
				b3DistanceJoint_SetLength(id, (float)configuration["length"]);
			}
			if (p_changed.has("enable_spring")) {
				b3DistanceJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b3DistanceJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b3DistanceJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("enable_limit")) {
				b3DistanceJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b3DistanceJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_force")) {
				b3DistanceJoint_SetMaxMotorForce(id, (float)configuration["max_motor_force"]);
			}
			if (p_changed.has("motor_speed")) {
				b3DistanceJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		case PS3DE::JOINT_TYPE_FILTER:
			break;
		case PS3DE::JOINT_TYPE_MOTOR:
			if (p_changed.has("linear_velocity")) {
				b3MotorJoint_SetLinearVelocity(id, godot_to_b3(Vector3(configuration["linear_velocity"])));
			}
			if (p_changed.has("max_velocity_force")) {
				b3MotorJoint_SetMaxVelocityForce(id, (float)configuration["max_velocity_force"]);
			}
			if (p_changed.has("angular_velocity")) {
				b3MotorJoint_SetAngularVelocity(id, godot_to_b3(Vector3(configuration["angular_velocity"])));
			}
			if (p_changed.has("max_velocity_torque")) {
				b3MotorJoint_SetMaxVelocityTorque(id, (float)configuration["max_velocity_torque"]);
			}
			if (p_changed.has("linear_hertz")) {
				b3MotorJoint_SetLinearHertz(id, (float)configuration["linear_hertz"]);
			}
			if (p_changed.has("linear_damping_ratio")) {
				b3MotorJoint_SetLinearDampingRatio(id, (float)configuration["linear_damping_ratio"]);
			}
			if (p_changed.has("max_spring_force")) {
				b3MotorJoint_SetMaxSpringForce(id, (float)configuration["max_spring_force"]);
			}
			if (p_changed.has("angular_hertz")) {
				b3MotorJoint_SetAngularHertz(id, (float)configuration["angular_hertz"]);
			}
			if (p_changed.has("angular_damping_ratio")) {
				b3MotorJoint_SetAngularDampingRatio(id, (float)configuration["angular_damping_ratio"]);
			}
			if (p_changed.has("max_spring_torque")) {
				b3MotorJoint_SetMaxSpringTorque(id, (float)configuration["max_spring_torque"]);
			}
			break;
		case PS3DE::JOINT_TYPE_PARALLEL:
			if (p_changed.has("hertz")) {
				b3ParallelJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b3ParallelJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("max_torque")) {
				b3ParallelJoint_SetMaxTorque(id, (float)configuration["max_torque"]);
			}
			break;
		case PS3DE::JOINT_TYPE_PRISMATIC:
			if (p_changed.has("lower_translation") || p_changed.has("upper_translation")) {
				b3PrismaticJoint_SetLimits(id, float(configuration["lower_translation"]), float(configuration["upper_translation"]));
			}
			if (p_changed.has("enable_spring")) {
				b3PrismaticJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b3PrismaticJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b3PrismaticJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("target_translation")) {
				b3PrismaticJoint_SetTargetTranslation(id, (float)configuration["target_translation"]);
			}
			if (p_changed.has("enable_limit")) {
				b3PrismaticJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b3PrismaticJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_force")) {
				b3PrismaticJoint_SetMaxMotorForce(id, (float)configuration["max_motor_force"]);
			}
			if (p_changed.has("motor_speed")) {
				b3PrismaticJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		case PS3DE::JOINT_TYPE_REVOLUTE:
			if (p_changed.has("lower_angle") || p_changed.has("upper_angle")) {
				b3RevoluteJoint_SetLimits(id, float(configuration["lower_angle"]), float(configuration["upper_angle"]));
			}
			if (p_changed.has("target_angle")) {
				b3RevoluteJoint_SetTargetAngle(id, (float)configuration["target_angle"]);
			}
			if (p_changed.has("enable_spring")) {
				b3RevoluteJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b3RevoluteJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b3RevoluteJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("enable_limit")) {
				b3RevoluteJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b3RevoluteJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_torque")) {
				b3RevoluteJoint_SetMaxMotorTorque(id, (float)configuration["max_motor_torque"]);
			}
			if (p_changed.has("motor_speed")) {
				b3RevoluteJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		case PS3DE::JOINT_TYPE_SPHERICAL:
			if (p_changed.has("lower_twist_angle") || p_changed.has("upper_twist_angle")) {
				b3SphericalJoint_SetTwistLimits(id, float(configuration["lower_twist_angle"]), float(configuration["upper_twist_angle"]));
			}
			if (p_changed.has("enable_spring")) {
				b3SphericalJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b3SphericalJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b3SphericalJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("target_rotation")) {
				b3SphericalJoint_SetTargetRotation(id, godot_to_b3(Quaternion(configuration["target_rotation"])));
			}
			if (p_changed.has("enable_cone_limit")) {
				b3SphericalJoint_EnableConeLimit(id, (bool)configuration["enable_cone_limit"]);
			}
			if (p_changed.has("cone_angle")) {
				b3SphericalJoint_SetConeLimit(id, (float)configuration["cone_angle"]);
			}
			if (p_changed.has("enable_twist_limit")) {
				b3SphericalJoint_EnableTwistLimit(id, (bool)configuration["enable_twist_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b3SphericalJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_torque")) {
				b3SphericalJoint_SetMaxMotorTorque(id, (float)configuration["max_motor_torque"]);
			}
			if (p_changed.has("motor_velocity")) {
				b3SphericalJoint_SetMotorVelocity(id, godot_to_b3(Vector3(configuration["motor_velocity"])));
			}
			break;
		case PS3DE::JOINT_TYPE_WELD:
			if (p_changed.has("linear_hertz")) {
				b3WeldJoint_SetLinearHertz(id, (float)configuration["linear_hertz"]);
			}
			if (p_changed.has("angular_hertz")) {
				b3WeldJoint_SetAngularHertz(id, (float)configuration["angular_hertz"]);
			}
			if (p_changed.has("linear_damping_ratio")) {
				b3WeldJoint_SetLinearDampingRatio(id, (float)configuration["linear_damping_ratio"]);
			}
			if (p_changed.has("angular_damping_ratio")) {
				b3WeldJoint_SetAngularDampingRatio(id, (float)configuration["angular_damping_ratio"]);
			}
			break;
		case PS3DE::JOINT_TYPE_WHEEL:
			if (p_changed.has("lower_suspension_limit") || p_changed.has("upper_suspension_limit")) {
				b3WheelJoint_SetSuspensionLimits(id, float(configuration["lower_suspension_limit"]), float(configuration["upper_suspension_limit"]));
			}
			if (p_changed.has("lower_steering_limit") || p_changed.has("upper_steering_limit")) {
				b3WheelJoint_SetSteeringLimits(id, float(configuration["lower_steering_limit"]), float(configuration["upper_steering_limit"]));
			}
			if (p_changed.has("enable_suspension_spring")) {
				b3WheelJoint_EnableSuspension(id, (bool)configuration["enable_suspension_spring"]);
			}
			if (p_changed.has("suspension_hertz")) {
				b3WheelJoint_SetSuspensionHertz(id, (float)configuration["suspension_hertz"]);
			}
			if (p_changed.has("suspension_damping_ratio")) {
				b3WheelJoint_SetSuspensionDampingRatio(id, (float)configuration["suspension_damping_ratio"]);
			}
			if (p_changed.has("enable_suspension_limit")) {
				b3WheelJoint_EnableSuspensionLimit(id, (bool)configuration["enable_suspension_limit"]);
			}
			if (p_changed.has("enable_spin_motor")) {
				b3WheelJoint_EnableSpinMotor(id, (bool)configuration["enable_spin_motor"]);
			}
			if (p_changed.has("max_spin_torque")) {
				b3WheelJoint_SetMaxSpinTorque(id, (float)configuration["max_spin_torque"]);
			}
			if (p_changed.has("spin_speed")) {
				b3WheelJoint_SetSpinMotorSpeed(id, (float)configuration["spin_speed"]);
			}
			if (p_changed.has("enable_steering")) {
				b3WheelJoint_EnableSteering(id, (bool)configuration["enable_steering"]);
			}
			if (p_changed.has("steering_hertz")) {
				b3WheelJoint_SetSteeringHertz(id, (float)configuration["steering_hertz"]);
			}
			if (p_changed.has("steering_damping_ratio")) {
				b3WheelJoint_SetSteeringDampingRatio(id, (float)configuration["steering_damping_ratio"]);
			}
			if (p_changed.has("target_steering_angle")) {
				b3WheelJoint_SetTargetSteeringAngle(id, (float)configuration["target_steering_angle"]);
			}
			if (p_changed.has("max_steering_torque")) {
				b3WheelJoint_SetMaxSteeringTorque(id, (float)configuration["max_steering_torque"]);
			}
			if (p_changed.has("enable_steering_limit")) {
				b3WheelJoint_EnableSteeringLimit(id, (bool)configuration["enable_steering_limit"]);
			}
			break;
		default:
			break;
	}
	b3Joint_WakeBodies(id);
}
