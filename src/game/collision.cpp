/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <engine/kernel.h>
#include <engine/map.h>
#include <cmath>

#include <game/collision.h>
#include <game/layers.h>
#include <game/mapitems.h>

CCollision::CCollision()
{
	m_pLayers = nullptr;
	m_Width = 0;
	m_Height = 0;
	m_pTiles = nullptr;
	m_pPhysicalQuads = nullptr;
}

void CCollision::Init(class CLayers *pLayers)
{
	m_pLayers = pLayers;
	m_Width = m_pLayers->GameLayer()->m_Width;
	m_Height = m_pLayers->GameLayer()->m_Height;
	m_pTiles = static_cast<CTile *>(m_pLayers->Map()->GetData(m_pLayers->GameLayer()->m_Data));
	m_pPhysicalQuads = static_cast<CQuad *>(m_pLayers->Map()->GetDataSwapped(m_pLayers->PhysicalLayer()->m_Data));

	for(int i = 0; i < m_Width * m_Height; i++)
	{
		int Index = m_pTiles[i].m_Index;

		if(Index > 128)
			continue;

		switch(Index)
		{
		case TILE_DEATH:
			m_pTiles[i].m_Index = COLFLAG_DEATH;
			break;
		case TILE_SOLID:
			m_pTiles[i].m_Index = COLFLAG_SOLID;
			break;
		case TILE_NOHOOK:
			m_pTiles[i].m_Index = COLFLAG_SOLID | COLFLAG_NOHOOK;
			break;
		case TILE_EXPORT:
			m_pTiles[i].m_Index = COLFLAG_EXPORT;
			break;
		default:
			m_pTiles[i].m_Index = 0;
		}
	}

	// check physical quads
	if(m_pPhysicalQuads)
	{
		for(int q = 0; q < m_pLayers->PhysicalLayer()->m_NumQuads; q++)
		{
			int Index = m_pPhysicalQuads[q].m_ColorEnvOffset;

			if(Index > 128)
				continue;

			switch(Index)
			{
			case TILE_DEATH:
				m_pPhysicalQuads[q].m_ColorEnvOffset = COLFLAG_DEATH;
				break;
			case TILE_EXPORT:
				m_pPhysicalQuads[q].m_ColorEnvOffset = COLFLAG_EXPORT;
				break;
			case TILE_SOLID: // bad collision prediction
			case TILE_NOHOOK: // bad collision prediction
			default:
				m_pPhysicalQuads[q].m_ColorEnvOffset = 0;
			}
		}
	}
}

// from infclass
inline bool SameSide(const vec2 &l0, const vec2 &l1, const vec2 &p0, const vec2 &p1)
{
	vec2 l0l1 = l1 - l0;
	vec2 l0p0 = p0 - l0;
	vec2 l0p1 = p1 - l0;

	return sign(l0l1.x * l0p0.y - l0l1.y * l0p0.x) == sign(l0l1.x * l0p1.y - l0l1.y * l0p1.x);
}

// t0, t1 and t2 are position of triangle vertices
inline vec3 BarycentricCoordinates(const vec2 &t0, const vec2 &t1, const vec2 &t2, const vec2 &p)
{
	vec2 e0 = t1 - t0;
	vec2 e1 = t2 - t0;
	vec2 e2 = p - t0;

	float d00 = dot(e0, e0);
	float d01 = dot(e0, e1);
	float d11 = dot(e1, e1);
	float d20 = dot(e2, e0);
	float d21 = dot(e2, e1);
	float denom = d00 * d11 - d01 * d01;

	vec3 bary;
	bary.x = (d11 * d20 - d01 * d21) / denom;
	bary.y = (d00 * d21 - d01 * d20) / denom;
	bary.z = 1.0f - bary.x - bary.y;

	return bary;
}

// t0, t1 and t2 are position of triangle vertices
inline bool InsideTriangle(const vec2 &t0, const vec2 &t1, const vec2 &t2, const vec2 &p)
{
	vec3 bary = BarycentricCoordinates(t0, t1, t2, p);
	return (bary.x >= 0.0f && bary.y >= 0.0f && bary.x + bary.y < 1.0f);
}

// t0, t1 and t2 are position of quad vertices
inline bool InsideQuad(const vec2 &q0, const vec2 &q1, const vec2 &q2, const vec2 &q3, const vec2 &p)
{
	if(SameSide(q1, q2, p, q0))
		return InsideTriangle(q0, q1, q2, p);
	else
		return InsideTriangle(q1, q2, q3, p);
}

int CCollision::GetTile(int x, int y, bool PhysicLayer) const
{
	int Nx = clamp(x / 32, 0, m_Width - 1);
	int Ny = clamp(y / 32, 0, m_Height - 1);

	int Index = m_pTiles[Ny * m_Width + Nx].m_Index > 128 ? 0 : m_pTiles[Ny * m_Width + Nx].m_Index;
	if(PhysicLayer &&  m_pPhysicalQuads)
	{
		// check physical quads
		for(int q = 0; q < m_pLayers->PhysicalLayer()->m_NumQuads; q++)
		{
			vec2 p0 = vec2(fx2f(m_pPhysicalQuads[q].m_aPoints[0].x), fx2f(m_pPhysicalQuads[q].m_aPoints[0].y));
			vec2 p1 = vec2(fx2f(m_pPhysicalQuads[q].m_aPoints[1].x), fx2f(m_pPhysicalQuads[q].m_aPoints[1].y));
			vec2 p2 = vec2(fx2f(m_pPhysicalQuads[q].m_aPoints[2].x), fx2f(m_pPhysicalQuads[q].m_aPoints[2].y));
			vec2 p3 = vec2(fx2f(m_pPhysicalQuads[q].m_aPoints[3].x), fx2f(m_pPhysicalQuads[q].m_aPoints[3].y));

			if(InsideQuad(p0, p1, p2, p3, vec2(x, y)))
			{
				Index |= m_pPhysicalQuads[q].m_ColorEnvOffset;
			}
		}
	}

	return Index;
}

bool CCollision::IsTile(int x, int y, int Flag, bool PhysicLayer) const
{
	return GetTile(x, y, PhysicLayer) & Flag;
}

// TODO: rewrite this smarter!
int CCollision::IntersectLine(vec2 Pos0, vec2 Pos1, vec2 *pOutCollision, vec2 *pOutBeforeCollision, bool PhysicLayer) const
{
	const int End = distance(Pos0, Pos1) + 1;
	const float InverseEnd = 1.0f / End;
	vec2 Last = Pos0;

	for(int i = 0; i <= End; i++)
	{
		vec2 Pos = mix(Pos0, Pos1, i * InverseEnd);
		if(CheckPoint(Pos.x, Pos.y, PhysicLayer))
		{
			if(pOutCollision)
				*pOutCollision = Pos;
			if(pOutBeforeCollision)
				*pOutBeforeCollision = Last;
			return GetCollisionAt(Pos.x, Pos.y, PhysicLayer);
		}
		Last = Pos;
	}
	if(pOutCollision)
		*pOutCollision = Pos1;
	if(pOutBeforeCollision)
		*pOutBeforeCollision = Pos1;
	return 0;
}

// TODO: OPT: rewrite this smarter!
void CCollision::MovePoint(vec2 *pInoutPos, vec2 *pInoutVel, float Elasticity, int *pBounces, bool PhysicLayer) const
{
	if(pBounces)
		*pBounces = 0;

	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;
	if(CheckPoint(Pos + Vel, PhysicLayer))
	{
		int Affected = 0;
		if(CheckPoint(Pos.x + Vel.x, Pos.y, PhysicLayer))
		{
			pInoutVel->x *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(CheckPoint(Pos.x, Pos.y + Vel.y, PhysicLayer))
		{
			pInoutVel->y *= -Elasticity;
			if(pBounces)
				(*pBounces)++;
			Affected++;
		}

		if(Affected == 0)
		{
			pInoutVel->x *= -Elasticity;
			pInoutVel->y *= -Elasticity;
		}
	}
	else
	{
		*pInoutPos = Pos + Vel;
	}
}

bool CCollision::TestBox(vec2 Pos, vec2 Size, int Flag, bool PhysicLayer) const
{
	Size *= 0.5f;
	if(CheckPoint(Pos.x - Size.x, Pos.y - Size.y, Flag, PhysicLayer))
		return true;
	if(CheckPoint(Pos.x + Size.x, Pos.y - Size.y, Flag, PhysicLayer))
		return true;
	if(CheckPoint(Pos.x - Size.x, Pos.y + Size.y, Flag, PhysicLayer))
		return true;
	if(CheckPoint(Pos.x + Size.x, Pos.y + Size.y, Flag, PhysicLayer))
		return true;
	return false;
}

void CCollision::MoveBox(vec2 *pInoutPos, vec2 *pInoutVel, vec2 Size, float Elasticity, bool *pDeath, bool PhysicLayer) const
{
	// do the move
	vec2 Pos = *pInoutPos;
	vec2 Vel = *pInoutVel;

	const float Distance = length(Vel);
	const int Max = (int) Distance;

	if(pDeath)
		*pDeath = false;

	if(Distance > 0.00001f)
	{
		const float Fraction = 1.0f / (Max + 1);
		for(int i = 0; i <= Max; i++)
		{
			vec2 NewPos = Pos + Vel * Fraction; // TODO: this row is not nice

			// You hit a deathtile, congrats to that :)
			// Deathtiles are a bit smaller
			if(pDeath && TestBox(vec2(NewPos.x, NewPos.y), Size * (2.0f / 3.0f), COLFLAG_DEATH))
			{
				*pDeath = true;
			}

			if(TestBox(vec2(NewPos.x, NewPos.y), Size, PhysicLayer))
			{
				int Hits = 0;

				if(TestBox(vec2(Pos.x, NewPos.y), Size, PhysicLayer))
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					Hits++;
				}

				if(TestBox(vec2(NewPos.x, Pos.y), Size, PhysicLayer))
				{
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
					Hits++;
				}

				// neither of the tests got a collision.
				// this is a real _corner case_!
				if(Hits == 0)
				{
					NewPos.y = Pos.y;
					Vel.y *= -Elasticity;
					NewPos.x = Pos.x;
					Vel.x *= -Elasticity;
				}
			}

			Pos = NewPos;
		}
	}

	*pInoutPos = Pos;
	*pInoutVel = Vel;
}
