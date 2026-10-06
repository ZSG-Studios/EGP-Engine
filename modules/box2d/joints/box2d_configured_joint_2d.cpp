// SPDX-License-Identifier: MIT
#include "box2d_configured_joint_2d.h"

#include "../spaces/box2d_space_2d.h"

#include <cfloat>
Box2DConfiguredJoint2D::Box2DConfiguredJoint2D(PS2DE::JointType p_type, Box2DBody2D *p_a, Box2DBody2D *p_b, const Transform2D &p_frame_a, const Transform2D &p_frame_b) :
		Box2DJoint2D(p_type, p_a, p_b), frame_a(p_frame_a), frame_b(p_frame_b) {
	configuration = default_configuration(p_type);
}
Dictionary Box2DConfiguredJoint2D::default_configuration(PS2DE::JointType p_type) {
	Dictionary result;
	switch (p_type) {
		case PS2DE::JOINT_TYPE_DISTANCE: {
			const auto def = b2DefaultDistanceJointDef();
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
		case PS2DE::JOINT_TYPE_FILTER: {
			const auto def = b2DefaultFilterJointDef();
			result["constraint_hertz"] = def.base.constraintHertz;
			result["constraint_damping_ratio"] = def.base.constraintDampingRatio;
			result["force_threshold"] = def.base.forceThreshold;
			result["torque_threshold"] = def.base.torqueThreshold;
			result["collide_connected"] = def.base.collideConnected;
			break;
		}
		case PS2DE::JOINT_TYPE_MOTOR: {
			const auto def = b2DefaultMotorJointDef();
			result["linear_velocity"] = to_godot(def.linearVelocity);
			result["max_velocity_force"] = def.maxVelocityForce;
			result["angular_velocity"] = def.angularVelocity;
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
		case PS2DE::JOINT_TYPE_PRISMATIC: {
			const auto def = b2DefaultPrismaticJointDef();
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
		case PS2DE::JOINT_TYPE_REVOLUTE: {
			const auto def = b2DefaultRevoluteJointDef();
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
		case PS2DE::JOINT_TYPE_WELD: {
			const auto def = b2DefaultWeldJointDef();
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
		case PS2DE::JOINT_TYPE_WHEEL: {
			const auto def = b2DefaultWheelJointDef();
			result["enable_spring"] = def.enableSpring;
			result["hertz"] = def.hertz;
			result["damping_ratio"] = def.dampingRatio;
			result["enable_limit"] = def.enableLimit;
			result["lower_translation"] = def.lowerTranslation;
			result["upper_translation"] = def.upperTranslation;
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
		default:
			return Dictionary();
	}
	return result;
}
bool Box2DConfiguredJoint2D::validate_configuration(PS2DE::JointType p_type, const Dictionary &p_configuration) {
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
			if (value.get_type() == Variant::VECTOR2) {
				Vector2 item = value;
				ERR_FAIL_COND_V(!item.is_finite(), false);
			}
		}
		candidate[key] = value;
	}
	switch (p_type) {
		case PS2DE::JOINT_TYPE_DISTANCE:
			ERR_FAIL_COND_V(double(candidate["lower_spring_force"]) > double(candidate["upper_spring_force"]), false);
			ERR_FAIL_COND_V(double(candidate["min_length"]) > double(candidate["max_length"]), false);
			break;
		case PS2DE::JOINT_TYPE_PRISMATIC:
			ERR_FAIL_COND_V(double(candidate["lower_translation"]) > double(candidate["upper_translation"]), false);
			break;
		case PS2DE::JOINT_TYPE_REVOLUTE:
			ERR_FAIL_COND_V(double(candidate["lower_angle"]) > double(candidate["upper_angle"]), false);
			ERR_FAIL_COND_V(double(candidate["lower_angle"]) < -0.99 * Math::PI || double(candidate["upper_angle"]) > 0.99 * Math::PI, false);
			break;
		case PS2DE::JOINT_TYPE_WHEEL:
			ERR_FAIL_COND_V(double(candidate["lower_translation"]) > double(candidate["upper_translation"]), false);
			break;
		default:
			break;
	}
	return true;
}
bool Box2DConfiguredJoint2D::set_configuration(const Dictionary &p_configuration) {
	Dictionary candidate = configuration.duplicate();
	candidate.merge(p_configuration, true);
	if (!validate_configuration(type, candidate)) {
		return false;
	}
	configuration = candidate;
	disable_collisions_between_bodies(!bool(configuration["collide_connected"]));
	_apply_configuration(p_configuration);
	return true;
}
void Box2DConfiguredJoint2D::rebuild() {
	destroy_joint();
	if (!body_a || !body_a->in_space() || (body_b && body_b->get_space() != body_a->get_space())) {
		return;
	}
	if (!body_a->is_dynamic() && (!body_b || !body_b->is_dynamic())) {
		return;
	}
	space = body_a->get_space();
	const b2WorldId p_world = space->get_world_id();
	const b2BodyId p_a = body_a->get_body_id();
	const b2BodyId p_b = body_b ? body_b->get_body_id() : space->get_world_anchor_body();
	const b2Transform p_frame_a = to_box2d(frame_a);
	const b2Transform p_frame_b = to_box2d(frame_b);
	switch (type) {
		case PS2DE::JOINT_TYPE_DISTANCE: {
			auto def = b2DefaultDistanceJointDef();
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
			joint_id = b2CreateDistanceJoint(p_world, &def);
			break;
		}
		case PS2DE::JOINT_TYPE_FILTER: {
			auto def = b2DefaultFilterJointDef();
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
			joint_id = b2CreateFilterJoint(p_world, &def);
			break;
		}
		case PS2DE::JOINT_TYPE_MOTOR: {
			auto def = b2DefaultMotorJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.linearVelocity = to_box2d(Vector2(configuration["linear_velocity"]));
			def.maxVelocityForce = (float)configuration["max_velocity_force"];
			def.angularVelocity = (float)configuration["angular_velocity"];
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
			joint_id = b2CreateMotorJoint(p_world, &def);
			break;
		}
		case PS2DE::JOINT_TYPE_PRISMATIC: {
			auto def = b2DefaultPrismaticJointDef();
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
			joint_id = b2CreatePrismaticJoint(p_world, &def);
			break;
		}
		case PS2DE::JOINT_TYPE_REVOLUTE: {
			auto def = b2DefaultRevoluteJointDef();
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
			joint_id = b2CreateRevoluteJoint(p_world, &def);
			break;
		}
		case PS2DE::JOINT_TYPE_WELD: {
			auto def = b2DefaultWeldJointDef();
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
			joint_id = b2CreateWeldJoint(p_world, &def);
			break;
		}
		case PS2DE::JOINT_TYPE_WHEEL: {
			auto def = b2DefaultWheelJointDef();
			def.base.bodyIdA = p_a;
			def.base.bodyIdB = p_b;
			def.base.localFrameA = p_frame_a;
			def.base.localFrameB = p_frame_b;
			def.base.userData = this;
			def.enableSpring = (bool)configuration["enable_spring"];
			def.hertz = (float)configuration["hertz"];
			def.dampingRatio = (float)configuration["damping_ratio"];
			def.enableLimit = (bool)configuration["enable_limit"];
			def.lowerTranslation = (float)configuration["lower_translation"];
			def.upperTranslation = (float)configuration["upper_translation"];
			def.enableMotor = (bool)configuration["enable_motor"];
			def.maxMotorTorque = (float)configuration["max_motor_torque"];
			def.motorSpeed = (float)configuration["motor_speed"];
			def.base.constraintHertz = (float)configuration["constraint_hertz"];
			def.base.constraintDampingRatio = (float)configuration["constraint_damping_ratio"];
			def.base.forceThreshold = (float)configuration["force_threshold"];
			def.base.torqueThreshold = (float)configuration["torque_threshold"];
			def.base.collideConnected = (bool)configuration["collide_connected"];
			joint_id = b2CreateWheelJoint(p_world, &def);
			break;
		}
		default:
			break;
	}
}
void Box2DConfiguredJoint2D::_apply_configuration(const Dictionary &p_changed) {
	if (!b2Joint_IsValid(joint_id)) {
		return;
	}
	const b2JointId id = joint_id;
	if (p_changed.has("force_threshold")) {
		b2Joint_SetForceThreshold(id, float(configuration["force_threshold"]));
	}
	if (p_changed.has("torque_threshold")) {
		b2Joint_SetTorqueThreshold(id, float(configuration["torque_threshold"]));
	}
	if (p_changed.has("constraint_hertz") || p_changed.has("constraint_damping_ratio")) {
		b2Joint_SetConstraintTuning(id, float(configuration["constraint_hertz"]), float(configuration["constraint_damping_ratio"]));
	}
	switch (type) {
		case PS2DE::JOINT_TYPE_DISTANCE:
			if (p_changed.has("lower_spring_force") || p_changed.has("upper_spring_force")) {
				b2DistanceJoint_SetSpringForceRange(id, float(configuration["lower_spring_force"]), float(configuration["upper_spring_force"]));
			}
			if (p_changed.has("min_length") || p_changed.has("max_length")) {
				b2DistanceJoint_SetLengthRange(id, float(configuration["min_length"]), float(configuration["max_length"]));
			}
			if (p_changed.has("length")) {
				b2DistanceJoint_SetLength(id, (float)configuration["length"]);
			}
			if (p_changed.has("enable_spring")) {
				b2DistanceJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b2DistanceJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b2DistanceJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("enable_limit")) {
				b2DistanceJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b2DistanceJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_force")) {
				b2DistanceJoint_SetMaxMotorForce(id, (float)configuration["max_motor_force"]);
			}
			if (p_changed.has("motor_speed")) {
				b2DistanceJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		case PS2DE::JOINT_TYPE_FILTER:
			break;
		case PS2DE::JOINT_TYPE_MOTOR:
			if (p_changed.has("linear_velocity")) {
				b2MotorJoint_SetLinearVelocity(id, to_box2d(Vector2(configuration["linear_velocity"])));
			}
			if (p_changed.has("max_velocity_force")) {
				b2MotorJoint_SetMaxVelocityForce(id, (float)configuration["max_velocity_force"]);
			}
			if (p_changed.has("angular_velocity")) {
				b2MotorJoint_SetAngularVelocity(id, (float)configuration["angular_velocity"]);
			}
			if (p_changed.has("max_velocity_torque")) {
				b2MotorJoint_SetMaxVelocityTorque(id, (float)configuration["max_velocity_torque"]);
			}
			if (p_changed.has("linear_hertz")) {
				b2MotorJoint_SetLinearHertz(id, (float)configuration["linear_hertz"]);
			}
			if (p_changed.has("linear_damping_ratio")) {
				b2MotorJoint_SetLinearDampingRatio(id, (float)configuration["linear_damping_ratio"]);
			}
			if (p_changed.has("max_spring_force")) {
				b2MotorJoint_SetMaxSpringForce(id, (float)configuration["max_spring_force"]);
			}
			if (p_changed.has("angular_hertz")) {
				b2MotorJoint_SetAngularHertz(id, (float)configuration["angular_hertz"]);
			}
			if (p_changed.has("angular_damping_ratio")) {
				b2MotorJoint_SetAngularDampingRatio(id, (float)configuration["angular_damping_ratio"]);
			}
			if (p_changed.has("max_spring_torque")) {
				b2MotorJoint_SetMaxSpringTorque(id, (float)configuration["max_spring_torque"]);
			}
			break;
		case PS2DE::JOINT_TYPE_PRISMATIC:
			if (p_changed.has("lower_translation") || p_changed.has("upper_translation")) {
				b2PrismaticJoint_SetLimits(id, float(configuration["lower_translation"]), float(configuration["upper_translation"]));
			}
			if (p_changed.has("enable_spring")) {
				b2PrismaticJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b2PrismaticJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b2PrismaticJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("target_translation")) {
				b2PrismaticJoint_SetTargetTranslation(id, (float)configuration["target_translation"]);
			}
			if (p_changed.has("enable_limit")) {
				b2PrismaticJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b2PrismaticJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_force")) {
				b2PrismaticJoint_SetMaxMotorForce(id, (float)configuration["max_motor_force"]);
			}
			if (p_changed.has("motor_speed")) {
				b2PrismaticJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		case PS2DE::JOINT_TYPE_REVOLUTE:
			if (p_changed.has("lower_angle") || p_changed.has("upper_angle")) {
				b2RevoluteJoint_SetLimits(id, float(configuration["lower_angle"]), float(configuration["upper_angle"]));
			}
			if (p_changed.has("target_angle")) {
				b2RevoluteJoint_SetTargetAngle(id, (float)configuration["target_angle"]);
			}
			if (p_changed.has("enable_spring")) {
				b2RevoluteJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b2RevoluteJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b2RevoluteJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("enable_limit")) {
				b2RevoluteJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b2RevoluteJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_torque")) {
				b2RevoluteJoint_SetMaxMotorTorque(id, (float)configuration["max_motor_torque"]);
			}
			if (p_changed.has("motor_speed")) {
				b2RevoluteJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		case PS2DE::JOINT_TYPE_WELD:
			if (p_changed.has("linear_hertz")) {
				b2WeldJoint_SetLinearHertz(id, (float)configuration["linear_hertz"]);
			}
			if (p_changed.has("angular_hertz")) {
				b2WeldJoint_SetAngularHertz(id, (float)configuration["angular_hertz"]);
			}
			if (p_changed.has("linear_damping_ratio")) {
				b2WeldJoint_SetLinearDampingRatio(id, (float)configuration["linear_damping_ratio"]);
			}
			if (p_changed.has("angular_damping_ratio")) {
				b2WeldJoint_SetAngularDampingRatio(id, (float)configuration["angular_damping_ratio"]);
			}
			break;
		case PS2DE::JOINT_TYPE_WHEEL:
			if (p_changed.has("lower_translation") || p_changed.has("upper_translation")) {
				b2WheelJoint_SetLimits(id, float(configuration["lower_translation"]), float(configuration["upper_translation"]));
			}
			if (p_changed.has("enable_spring")) {
				b2WheelJoint_EnableSpring(id, (bool)configuration["enable_spring"]);
			}
			if (p_changed.has("hertz")) {
				b2WheelJoint_SetSpringHertz(id, (float)configuration["hertz"]);
			}
			if (p_changed.has("damping_ratio")) {
				b2WheelJoint_SetSpringDampingRatio(id, (float)configuration["damping_ratio"]);
			}
			if (p_changed.has("enable_limit")) {
				b2WheelJoint_EnableLimit(id, (bool)configuration["enable_limit"]);
			}
			if (p_changed.has("enable_motor")) {
				b2WheelJoint_EnableMotor(id, (bool)configuration["enable_motor"]);
			}
			if (p_changed.has("max_motor_torque")) {
				b2WheelJoint_SetMaxMotorTorque(id, (float)configuration["max_motor_torque"]);
			}
			if (p_changed.has("motor_speed")) {
				b2WheelJoint_SetMotorSpeed(id, (float)configuration["motor_speed"]);
			}
			break;
		default:
			break;
	}
	b2Joint_WakeBodies(id);
}
