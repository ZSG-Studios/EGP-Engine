// SPDX-License-Identifier: MIT
#include <box3d/box3d.h>
#include <box3d/collision.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(c) \
	do { \
		if (!(c)) { \
			std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #c); \
			std::exit(1); \
		} \
	} while (false)
static void step(b3WorldId world, int count) {
	for (int i = 0; i < count; ++i) {
		b3World_Step(world, 1.0f / 60, 4);
	}
}
struct Fixture {
	b3WorldId world;
	b3BodyId anchor, body;
	b3JointId joint{};
	Fixture() {
		auto w = b3DefaultWorldDef();
		w.gravity = { 0, 0, 0 };
		world = b3CreateWorld(&w);
		auto a = b3DefaultBodyDef();
		anchor = b3CreateBody(world, &a);
		a.type = b3_dynamicBody;
		a.enableSleep = false;
		body = b3CreateBody(world, &a);
		auto s = b3DefaultShapeDef();
		s.density = 1;
		b3Sphere sphere{ { 0, 0, 0 }, 0.5f };
		b3CreateSphereShape(body, &s, &sphere);
	}
	void generic() {
		auto d = b3DefaultWeldJointDef();
		d.base.bodyIdA = anchor;
		d.base.bodyIdB = body;
		joint = b3CreateGenericJoint(world, &d);
	}
	~Fixture() { b3DestroyWorld(world); }
};
int main() {
	{
		Fixture f;
		f.generic();
		b3Body_ApplyLinearImpulseToCenter(f.body, { 5, -7, 2 }, true);
		b3Body_ApplyAngularImpulse(f.body, { 2, 3, 4 }, true);
		step(f.world, 180);
		auto t = b3Body_GetTransform(f.body);
		CHECK(b3Length(t.p) < 0.03f);
		CHECK(b3GetQuatAngle(t.q) < 0.04f);
		b3GenericJoint_SetFlag(f.joint, 0, 0, false);
		b3Body_ApplyLinearImpulseToCenter(f.body, { 2, 0, 0 }, true);
		step(f.world, 60);
		CHECK(b3Body_GetPosition(f.body).x > 1);
	}
	{
		Fixture f;
		f.generic();
		b3GenericJoint_SetParam(f.joint, 1, 0, -1);
		b3GenericJoint_SetParam(f.joint, 1, 1, 0.5f);
		b3Body_ApplyLinearImpulseToCenter(f.body, { 0, -5, 0 }, true);
		step(f.world, 120);
		CHECK(b3Body_GetPosition(f.body).y >= -1.04f);
		CHECK(b3Body_GetPosition(f.body).y <= 0.54f);
		b3GenericJoint_SetParam(f.joint, 0, 10, -0.25f);
		b3GenericJoint_SetParam(f.joint, 0, 11, 0.25f);
		b3Body_ApplyAngularImpulse(f.body, { 3, 0, 0 }, true);
		step(f.world, 120);
		CHECK(b3GetQuatAngle(b3Body_GetRotation(f.body)) < 0.3f);
	}
	{
		Fixture f;
		f.generic();
		b3GenericJoint_SetFlag(f.joint, 0, 0, false);
		b3GenericJoint_SetFlag(f.joint, 0, 5, true);
		b3GenericJoint_SetParam(f.joint, 0, 5, 2);
		b3GenericJoint_SetParam(f.joint, 0, 6, 10);
		step(f.world, 120);
		CHECK(std::fabs(b3Body_GetLinearVelocity(f.body).x - 2) < 0.02f);
		b3GenericJoint_SetParam(f.joint, 0, 22, 0);
		b3Body_SetLinearVelocity(f.body, { 0, 0, 0 });
		step(f.world, 30);
		CHECK(std::fabs(b3Body_GetLinearVelocity(f.body).x) < 0.01f);
	}
	{
		Fixture f;
		f.generic();
		b3GenericJoint_SetFlag(f.joint, 0, 0, false);
		b3GenericJoint_SetFlag(f.joint, 0, 3, true);
		b3GenericJoint_SetParam(f.joint, 0, 7, 30);
		b3GenericJoint_SetParam(f.joint, 0, 8, 8);
		b3GenericJoint_SetParam(f.joint, 0, 9, 2);
		step(f.world, 300);
		CHECK(std::fabs(b3Body_GetPosition(f.body).x - 2) < 0.02f);
		// Exercise mutation op replay, seed restoration, and native constraint warm starts.
		auto recording = b3CreateRecording(0);
		b3World_StartRecording(f.world, recording);
		b3GenericJoint_SetParam(f.joint, 0, 9, 1);
		b3GenericJoint_SetFlag(f.joint, 1, 0, false);
		b3GenericJoint_SetTargetRotation(f.joint, b3Quat_identity);
		step(f.world, 60);
		b3World_StopRecording(f.world);
		CHECK(b3ValidateReplay(b3Recording_GetData(recording), b3Recording_GetSize(recording), 1));
		b3DestroyRecording(recording);
	}
	{
		Fixture f;
		auto recording = b3CreateRecording(0);
		b3World_StartRecording(f.world, recording);
		f.generic();
		b3GenericJoint_SetFlag(f.joint, 0, 1, false);
		b3GenericJoint_SetFlag(f.joint, 0, 4, true);
		b3GenericJoint_SetParam(f.joint, 0, 17, 1);
		b3GenericJoint_SetParam(f.joint, 0, 18, 10);
		step(f.world, 60);
		CHECK(std::fabs(b3Body_GetAngularVelocity(f.body).x + 1) < 0.02f);
		b3GenericJoint_SetFlag(f.joint, 0, 4, false);
		b3GenericJoint_SetFlag(f.joint, 0, 2, true);
		b3GenericJoint_SetParam(f.joint, 0, 19, 10);
		b3GenericJoint_SetParam(f.joint, 0, 20, 2);
		b3GenericJoint_SetTargetRotation(f.joint, b3Quat{ { std::sin(0.2f), 0, 0 }, std::cos(0.2f) });
		step(f.world, 240);
		CHECK(std::fabs(b3GetQuatAngle(b3Body_GetRotation(f.body)) - 0.4f) < 0.03f);
		b3World_StopRecording(f.world);
		CHECK(b3ValidateReplay(b3Recording_GetData(recording), b3Recording_GetSize(recording), 1));
		b3DestroyRecording(recording);
	}
	{
		Fixture f;
		auto d = b3DefaultWeldJointDef();
		d.base.bodyIdA = f.anchor;
		d.base.bodyIdB = f.body;
		d.base.localFrameA.q = d.base.localFrameB.q = b3Quat{ { 0, 0, std::sin(0.35f) }, std::cos(0.35f) };
		f.joint = b3CreateGenericJoint(f.world, &d);
		b3GenericJoint_SetFlag(f.joint, 0, 0, false);
		b3GenericJoint_SetFlag(f.joint, 0, 5, true);
		b3GenericJoint_SetParam(f.joint, 0, 5, 2);
		b3GenericJoint_SetParam(f.joint, 0, 6, 10);
		step(f.world, 60);
		auto velocity = b3Body_GetLinearVelocity(f.body);
		CHECK(std::fabs(velocity.x - 2 * std::cos(0.7f)) < 0.03f);
		CHECK(std::fabs(velocity.y - 2 * std::sin(0.7f)) < 0.03f);
		CHECK(b3Joint_GetLinearSeparation(f.joint) < 0.03f);
	}
	{
		Fixture f;
		f.generic();
		b3Body_SetTransform(f.body, { 3, 4, 0 }, b3Quat_identity);
		CHECK(std::fabs(b3Joint_GetLinearSeparation(f.joint) - 5) < 0.001f);
		b3Body_SetTransform(f.body, { 0, 0, 0 }, b3Quat{ { 0, std::sin(0.35f), 0 }, std::cos(0.35f) });
		CHECK(std::fabs(b3Joint_GetAngularSeparation(f.joint) - 0.7f) < 0.001f);
		b3Body_EnableSleep(f.body, true);
		b3Body_SetAwake(f.body, false);
		b3GenericJoint_SetFlag(f.joint, 0, 0, false);
		CHECK(b3Body_IsAwake(f.body));
		step(f.world, 2);
	}
	{
		Fixture f;
		auto d = b3DefaultSphericalJointDef();
		d.base.bodyIdA = f.anchor;
		d.base.bodyIdB = f.body;
		d.enableConeLimit = true;
		d.coneAngle = 2.0943951f;
		f.joint = b3CreateSphericalJoint(f.world, &d);
		b3Body_SetTransform(f.body, { 0, 0, 0 }, b3Quat{ { 0, std::sin(0.8726646f), 0 }, std::cos(0.8726646f) });
		step(f.world, 60);
		CHECK(b3SphericalJoint_GetConeAngle(f.joint) > 1.70f);
		CHECK(b3SphericalJoint_GetConeAngle(f.joint) < 1.80f);
		b3Body_ApplyAngularImpulse(f.body, { 0, 3, 0 }, true);
		step(f.world, 120);
		CHECK(b3SphericalJoint_GetConeAngle(f.joint) < 2.14f);
		b3SphericalJoint_EnableTwistLimit(f.joint, true);
		b3SphericalJoint_SetTwistLimits(f.joint, -0.2f, 0.2f);
		b3Body_SetTransform(f.body, { 0, 0, 0 }, b3Quat{ { 0, 0, std::sin(0.6f) }, std::cos(0.6f) });
		b3Body_SetAngularVelocity(f.body, { 0, 0, 0 });
		step(f.world, 120);
		CHECK(std::fabs(b3SphericalJoint_GetTwistAngle(f.joint)) < 0.24f);
	}
	puts("PASS native 6DOF locks, limits, motors, capped drives, springs, replay and wide cone/twist constraints");
}
