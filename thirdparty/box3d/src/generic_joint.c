// SPDX-License-Identifier: MIT
// EGP extension of the Box3D Soft Step joint solver.
#include "body.h"
#include "joint.h"
#include "physics_world.h"
#include "recording.h"
#include "solver_set.h"

#include "box3d/box3d.h"

#include <string.h>

void b3GenericJoint_SetParam( b3JointId id, int axis, int param, float value )
{
	B3_ASSERT( axis >= 0 && axis < 3 && param >= 0 && param < 24 && b3IsValidFloat( value ) );
	b3World* world = b3GetWorld( id.world0 );
	B3_REC( world, GenericJointSetParam, id, axis, param, value );
	b3JointSim* base = b3GetJointSimCheckType( id, b3_genericJoint );
	if ( base->genericJoint.parameters[axis][param] == value )
		return;
	base->genericJoint.parameters[axis][param] = value;
	int index = axis + ( param >= 10 && param != 22 ? 3 : 0 );
	base->genericJoint.lowerImpulse[index] = base->genericJoint.upperImpulse[index] = base->genericJoint.motorImpulse[index] =
		base->genericJoint.springImpulse[index] = 0.0f;
	b3Joint_WakeBodies( id );
}
void b3GenericJoint_SetFlag( b3JointId id, int axis, int flag, bool value )
{
	B3_ASSERT( axis >= 0 && axis < 3 && flag >= 0 && flag < 6 );
	b3World* world = b3GetWorld( id.world0 );
	B3_REC( world, GenericJointSetFlag, id, axis, flag, value );
	b3JointSim* base = b3GetJointSimCheckType( id, b3_genericJoint );
	if ( base->genericJoint.flags[axis][flag] == value )
		return;
	base->genericJoint.flags[axis][flag] = value;
	// An axis disabled and re-enabled must not inherit impulses from its previous mode.
	memset( base->genericJoint.lowerImpulse, 0, sizeof( base->genericJoint.lowerImpulse ) );
	memset( base->genericJoint.upperImpulse, 0, sizeof( base->genericJoint.upperImpulse ) );
	memset( base->genericJoint.motorImpulse, 0, sizeof( base->genericJoint.motorImpulse ) );
	memset( base->genericJoint.springImpulse, 0, sizeof( base->genericJoint.springImpulse ) );
	// Waking can move the joint between solver sets and invalidate base.
	b3Joint_WakeBodies( id );
}
void b3GenericJoint_SetTargetRotation( b3JointId id, b3Quat rotation )
{
	B3_ASSERT( b3IsValidQuat( rotation ) );
	b3World* world = b3GetWorld( id.world0 );
	B3_REC( world, GenericJointSetTargetRotation, id, rotation );
	b3GetJointSimCheckType( id, b3_genericJoint )->genericJoint.targetRotation = rotation;
	b3Joint_WakeBodies( id );
}

static b3Vec3 genericEulerXYZ( b3Quat q )
{
	b3Vec3 x = b3RotateVector( q, b3Vec3_axisX );
	b3Vec3 y = b3RotateVector( q, b3Vec3_axisY );
	b3Vec3 z = b3RotateVector( q, b3Vec3_axisZ );
	float c = sqrtf( b3MaxFloat( 0.0f, 1.0f - z.x * z.x ) );
	if ( c > 1.0e-5f )
		return (b3Vec3){ b3Atan2( -z.y, z.z ), b3Atan2( z.x, c ), b3Atan2( -y.x, x.x ) };
	// Deterministic convention at the Euler singularity: keep Z at zero.
	return (b3Vec3){ b3Atan2( y.z, y.y ), z.x > 0.0f ? 0.5f * B3_PI : -0.5f * B3_PI, 0.0f };
}
static float component( b3Vec3 v, int axis )
{
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

float b3GetGenericJointSeparation( const b3JointSim* base, b3WorldTransform a, b3WorldTransform b, bool angular )
{
	b3Quat qa = b3MulQuat( a.q, base->localFrameA.q );
	b3Quat qb = b3MulQuat( b.q, base->localFrameB.q );
	b3Vec3 value = angular ? genericEulerXYZ( b3InvMulQuat( qb, qa ) )
						   : b3InvRotateVector( qa, b3SubPos( b3TransformWorldPoint( b, base->localFrameB.p ),
															  b3TransformWorldPoint( a, base->localFrameA.p ) ) );
	float error = 0;
	for ( int axis = 0; axis < 3; ++axis )
	{
		const float* p = base->genericJoint.parameters[axis];
		int offset = angular ? 10 : 0;
		if ( base->genericJoint.flags[axis][angular ? 1 : 0] && p[offset] <= p[offset + 1] )
		{
			float c = component( value, axis );
			float violation = b3MaxFloat( 0, b3MaxFloat( p[offset] - c, c - p[offset + 1] ) );
			error += violation * violation;
		}
	}
	return sqrtf( error );
}

typedef struct GenericGeometry
{
	b3Vec3 axis, a, b;
	float coordinate, k;
} GenericGeometry;

static GenericGeometry genericGeometry( b3JointSim* base, b3BodyState* sA, b3BodyState* sB, int index )
{
	b3GenericJoint* joint = &base->genericJoint;
	b3Quat qA = b3MulQuat( sA->deltaRotation, joint->frameA.q );
	b3Quat qB = b3MulQuat( sB->deltaRotation, joint->frameB.q );
	int axis = index % 3;
	GenericGeometry g = { 0 };
	if ( index < 3 )
	{
		b3Vec3 unit = axis == 0 ? b3Vec3_axisX : axis == 1 ? b3Vec3_axisY : b3Vec3_axisZ;
		g.axis = b3RotateVector( qA, unit );
		b3Vec3 rA = b3RotateVector( sA->deltaRotation, joint->frameA.p );
		b3Vec3 rB = b3RotateVector( sB->deltaRotation, joint->frameB.p );
		b3Vec3 d = b3Add( joint->deltaCenter, b3Add( b3Sub( sB->deltaPosition, sA->deltaPosition ), b3Sub( rB, rA ) ) );
		g.a = b3Cross( b3Add( rA, d ), g.axis );
		g.b = b3Cross( rB, g.axis );
		g.coordinate = b3Dot( d, g.axis );
		g.k = base->invMassA + base->invMassB;
	}
	else
	{
		// Match PhysicsServer3D's XYZ Euler limits on frame B inverse times frame A.
		b3Vec3 bx = b3RotateVector( qB, b3Vec3_axisX );
		b3Vec3 az = b3RotateVector( qA, b3Vec3_axisZ );
		b3Vec3 middle = b3Cross( az, bx );
		b3Vec3 jac = axis == 0 ? b3Cross( middle, az ) : axis == 1 ? middle : b3Cross( bx, middle );
		float cosine = b3Length( middle );
		if ( cosine < 1.0e-5f )
		{
			b3Vec3 unit = axis == 0 ? b3Vec3_axisX : axis == 1 ? b3Vec3_axisY : b3Vec3_axisZ;
			jac = b3RotateVector( qB, unit );
		}
		// The inverse Euler-rate matrix divides X/Z by cos(Y)^2 and Y by
		// cos(Y). Normalizing all three axes loses the X/Z rate near the pole.
		else
		{
			jac = b3MulSV( 1.0f / ( axis == 1 ? cosine : cosine * cosine ), jac );
		}
		g.a = g.b = b3Neg( jac );
		g.coordinate = component( genericEulerXYZ( b3InvMulQuat( qB, qA ) ), axis );
	}
	g.k += b3Dot( g.a, b3MulMV( base->invIA, g.a ) ) + b3Dot( g.b, b3MulMV( base->invIB, g.b ) );
	return g;
}
static float genericVelocity( GenericGeometry g, b3BodyState* a, b3BodyState* b )
{
	return b3Dot( g.axis, b3Sub( b->linearVelocity, a->linearVelocity ) ) + b3Dot( g.b, b->angularVelocity ) -
		   b3Dot( g.a, a->angularVelocity );
}
static void genericApply( b3JointSim* base, GenericGeometry g, b3BodyState* a, b3BodyState* b, float impulse )
{
	b3Vec3 p = b3MulSV( impulse, g.axis );
	if ( a->flags & b3_dynamicFlag )
	{
		a->linearVelocity = b3MulSub( a->linearVelocity, base->invMassA, p );
		a->angularVelocity = b3Sub( a->angularVelocity, b3MulSV( impulse, b3MulMV( base->invIA, g.a ) ) );
	}
	if ( b->flags & b3_dynamicFlag )
	{
		b->linearVelocity = b3MulAdd( b->linearVelocity, base->invMassB, p );
		b->angularVelocity = b3Add( b->angularVelocity, b3MulSV( impulse, b3MulMV( base->invIB, g.b ) ) );
	}
}
void b3PrepareGenericJoint( b3JointSim* base, b3StepContext* context )
{
	b3World* world = context->world;
	b3Body* a = b3Array_Get( world->bodies, base->bodyIdA );
	b3Body* b = b3Array_Get( world->bodies, base->bodyIdB );
	b3BodySim* sa = b3Array_Get( b3Array_Get( world->solverSets, a->setIndex )->bodySims, a->localIndex );
	b3BodySim* sb = b3Array_Get( b3Array_Get( world->solverSets, b->setIndex )->bodySims, b->localIndex );
	base->invMassA = sa->invMass;
	base->invMassB = sb->invMass;
	base->invIA = sa->invInertiaWorld;
	base->invIB = sb->invInertiaWorld;
	b3GenericJoint* joint = &base->genericJoint;
	joint->indexA = a->setIndex == b3_awakeSet ? a->localIndex : B3_NULL_INDEX;
	joint->indexB = b->setIndex == b3_awakeSet ? b->localIndex : B3_NULL_INDEX;
	joint->frameA.q = b3MulQuat( sa->transform.q, base->localFrameA.q );
	joint->frameB.q = b3MulQuat( sb->transform.q, base->localFrameB.q );
	joint->frameA.p = b3RotateVector( sa->transform.q, b3Sub( base->localFrameA.p, sa->localCenter ) );
	joint->frameB.p = b3RotateVector( sb->transform.q, b3Sub( base->localFrameB.p, sb->localCenter ) );
	joint->deltaCenter = b3SubPos( sb->center, sa->center );
	b3BodyState dummy = b3_identityBodyState;
	b3BodyState* stateA = joint->indexA == B3_NULL_INDEX ? &dummy : context->states + joint->indexA;
	b3BodyState* stateB = joint->indexB == B3_NULL_INDEX ? &dummy : context->states + joint->indexB;
	for ( int i = 0; i < 6; ++i )
	{
		joint->initialVelocity[i] = genericVelocity( genericGeometry( base, stateA, stateB, i ), stateA, stateB );
		if ( !context->enableWarmStarting )
			joint->lowerImpulse[i] = joint->upperImpulse[i] = joint->motorImpulse[i] = joint->springImpulse[i] = 0;
	}
}
void b3WarmStartGenericJoint( b3JointSim* base, b3StepContext* context )
{
	b3GenericJoint* joint = &base->genericJoint;
	b3BodyState dummy = b3_identityBodyState;
	b3BodyState* a = joint->indexA == B3_NULL_INDEX ? &dummy : context->states + joint->indexA;
	b3BodyState* b = joint->indexB == B3_NULL_INDEX ? &dummy : context->states + joint->indexB;
	for ( int i = 0; i < 6; ++i )
		genericApply( base, genericGeometry( base, a, b, i ), a, b,
					  joint->lowerImpulse[i] - joint->upperImpulse[i] + joint->motorImpulse[i] + joint->springImpulse[i] );
}
void b3SolveGenericJoint( b3JointSim* base, b3StepContext* context, bool useBias )
{
	b3GenericJoint* joint = &base->genericJoint;
	b3BodyState dummy = b3_identityBodyState;
	b3BodyState* a = joint->indexA == B3_NULL_INDEX ? &dummy : context->states + joint->indexA;
	b3BodyState* b = joint->indexB == B3_NULL_INDEX ? &dummy : context->states + joint->indexB;
	b3Vec3 target = genericEulerXYZ( joint->targetRotation );
	joint->reactionForce = joint->reactionTorque = b3Vec3_zero;
	for ( int i = 0; i < 6; ++i )
	{
		bool angular = i >= 3;
		int ax = i % 3;
		int offset = angular ? 10 : 0;
		float* p = joint->parameters[ax];
		bool* flags = joint->flags[ax];
		GenericGeometry g = genericGeometry( base, a, b, i );
		if ( g.k <= 0.0f )
			continue;
		float driveCap = b3MaxFloat( 0.0f, p[angular ? 23 : 22] ) * context->h;
		bool spring = flags[angular ? 2 : 3];
		if ( spring && p[angular ? 19 : 7] > 0.0f )
		{
			float stiffness = p[angular ? 19 : 7];
			float damping = b3MaxFloat( 0.0f, p[angular ? 20 : 8] );
			float equilibrium = p[angular ? 21 : 9] + ( angular ? component( target, ax ) : 0.0f );
			float cf = 1.0f / ( context->h * ( damping + context->h * stiffness ) );
			float bias = ( g.coordinate - equilibrium ) * stiffness / ( damping + context->h * stiffness );
			float old = joint->springImpulse[i];
			float impulse = -( genericVelocity( g, a, b ) + bias + cf * old ) / ( g.k + cf );
			joint->springImpulse[i] =
				b3ClampFloat( old + impulse, -driveCap - joint->motorImpulse[i], driveCap - joint->motorImpulse[i] );
			genericApply( base, g, a, b, joint->springImpulse[i] - old );
		}
		else
		{
			joint->springImpulse[i] = 0.0f;
		}
		if ( flags[angular ? 4 : 5] )
		{
			float cap = b3MaxFloat( 0.0f, p[angular ? 18 : 6] ) * context->h;
			float old = joint->motorImpulse[i];
			float impulse = -( genericVelocity( g, a, b ) - p[angular ? 17 : 5] ) / g.k;
			float lower = b3MaxFloat( -cap, -driveCap - joint->springImpulse[i] );
			float upper = b3MinFloat( cap, driveCap - joint->springImpulse[i] );
			joint->motorImpulse[i] = b3ClampFloat( old + impulse, lower, upper );
			genericApply( base, g, a, b, joint->motorImpulse[i] - old );
		}
		else
		{
			joint->motorImpulse[i] = 0.0f;
		}
		float lower = p[offset], upper = p[offset + 1];
		if ( flags[angular ? 1 : 0] && lower <= upper )
		{
			float softness = b3MaxFloat( 0.0f, p[offset + 2] );
			float damping = b3MaxFloat( 0.0f, p[angular ? 13 : 4] );
			float restitution = b3ClampFloat( p[angular ? 14 : 3], 0.0f, 1.0f );
			float erp = angular ? b3ClampFloat( p[16], 0.0f, 1.0f ) : 1.0f;
			float cap = angular && p[15] > 0.0f ? p[15] * context->h : FLT_MAX;
			for ( int side = 0; side < 2; ++side )
			{
				float sign = side == 0 ? 1.0f : -1.0f;
				float c = side == 0 ? g.coordinate - lower : upper - g.coordinate;
				float bias = c > 0.0f ? c * context->inv_h : useBias ? base->constraintSoftness.biasRate * erp * c : 0.0f;
				if ( c <= 1.0e-4f )
					bias += restitution * b3MinFloat( 0.0f, sign * joint->initialVelocity[i] );
				float massScale = c < 0 && useBias ? base->constraintSoftness.massScale : 1.0f;
				float impulseScale = c < 0 && useBias ? base->constraintSoftness.impulseScale : 0.0f;
				float* accumulated = side == 0 ? &joint->lowerImpulse[i] : &joint->upperImpulse[i];
				float old = *accumulated;
				float impulse =
					-massScale * softness * ( damping * sign * genericVelocity( g, a, b ) + bias ) / g.k - impulseScale * old;
				*accumulated = b3ClampFloat( old + impulse, 0.0f, cap );
				genericApply( base, g, a, b, sign * ( *accumulated - old ) );
			}
		}
		else
		{
			joint->lowerImpulse[i] = joint->upperImpulse[i] = 0.0f;
		}
		float total = joint->lowerImpulse[i] - joint->upperImpulse[i] + joint->motorImpulse[i] + joint->springImpulse[i];
		if ( angular )
			joint->reactionTorque = b3MulAdd( joint->reactionTorque, total, g.b );
		else
			joint->reactionForce = b3MulAdd( joint->reactionForce, total, g.axis );
	}
}
