/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/player.h>
#include <generated/server_data.h>

#include <algorithm>

#include "character.h"
#include "laser.h"
#include "projectile.h"

// input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i + 1) & INPUT_STATE_MASK;
		if(i & 1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_PLAYERS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld) :
	CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER, vec2(0, 0), ms_PhysSize)
{
	m_Health = 0;
	m_Armor = 0;
	m_TriggeredEvents = 0;

	for(int i = 0; i < 2; i++)
	{
		m_aFlashlightIDs[i] = Server()->SnapNewID();
	}
}

CCharacter::~CCharacter()
{
	for(int i = 0; i < 2; i++)
	{
		Server()->SnapFreeID(m_aFlashlightIDs[i]);
	}
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = WEAPON_HAMMER;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameWorld()->m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_SurpriseFrozenTick = -1;
	m_EscapingFrozenTick = -1;
	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameWorld()->InsertEntity(this);
	m_Alive = true;

	m_IsFlashlightOpened = false;
	m_IsVisible = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	return true;
}

void CCharacter::Destroy()
{
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
	m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x + GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x - GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;
	return false;
}

void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	if((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;

		// reset velocity and current move
		if(m_Ninja.m_CurrentMoveTime > 0)
			m_Core.m_Vel = m_Ninja.m_ActivationDir * m_Ninja.m_OldVelAmount;
		m_Ninja.m_CurrentMoveTime = -1;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if(m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * m_Ninja.m_OldVelAmount;
	}
	else if(m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we hit anything along the way
		const float Radius = GetProximityRadius() * 2.0f;
		const vec2 Center = OldPos + (m_Pos - OldPos) * 0.5f;
		CCharacter *aEnts[MAX_PLAYERS];
		const int Num = GameWorld()->FindEntities(Center, Radius, (CEntity **) aEnts, MAX_PLAYERS, CGameWorld::ENTTYPE_CHARACTER);

		for(int i = 0; i < Num; ++i)
		{
			if(aEnts[i] == this)
				continue;

			// make sure we haven't hit this object before
			bool AlreadyHit = false;
			for(int j = 0; j < m_NumObjectsHit; j++)
			{
				if(m_apHitObjects[j] == aEnts[i])
				{
					AlreadyHit = true;
					break;
				}
			}
			if(AlreadyHit)
				continue;

			// check so we are sufficiently close
			if(distance(aEnts[i]->m_Pos, m_Pos) > Radius)
				continue;

			// Hit a player, give him damage and stuffs...
			GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
			if(m_NumObjectsHit < MAX_PLAYERS)
				m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

			// set his velocity to fast upward (for now)
			aEnts[i]->TakeDamage(vec2(0, -10.0f), m_Ninja.m_ActivationDir * -1, g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
		}
	}
}

void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon + 1) % NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon - 1) < 0 ? NUM_WEAPONS - 1 : WantedWeapon - 1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon - 1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if(m_ReloadTimer != 0 || m_IsCaught)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_LASER)
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire & 1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if(m_LastNoAmmoSound + Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	vec2 ProjStartPos = m_Pos + Direction * GetProximityRadius() * 0.75f;

	if(Config()->m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "shot player='%d:%s' team=%d weapon=%d", m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), m_pPlayer->GetTeam(), m_ActiveWeapon);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	switch(m_ActiveWeapon)
	{
	case WEAPON_HAMMER:
	{
		GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

		CCharacter *apEnts[MAX_PLAYERS];
		int Hits = 0;
		int Num = GameWorld()->FindEntities(ProjStartPos, GetProximityRadius() * 0.5f, (CEntity **) apEnts,
			MAX_PLAYERS, CGameWorld::ENTTYPE_CHARACTER);

		int Damage = g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage;
		if(m_pPlayer->GetTeam() == TEAM_RED)
			Damage *= 2;

		for(int i = 0; i < Num; ++i)
		{
			CCharacter *pTarget = apEnts[i];

			if((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, nullptr, nullptr))
				continue;
			// the free ghosts can't be hit
			if(pTarget->GetPlayer()->GetTeam() == TEAM_RED)
			{
				auto Find = std::find(m_vCaughtGhosts.begin(), m_vCaughtGhosts.end(), pTarget);
				if(Find == m_vCaughtGhosts.end())
					continue;
				pTarget->AddEscapeProgress(-2);
			}

			// set his velocity to fast upward (for now)
			if(length(pTarget->m_Pos - ProjStartPos) > 0.0f)
				GameServer()->CreateHammerHit(pTarget->m_Pos - normalize(pTarget->m_Pos - ProjStartPos) * GetProximityRadius() * 0.5f);
			else
				GameServer()->CreateHammerHit(ProjStartPos);

			vec2 Dir;
			if(length(pTarget->m_Pos - m_Pos) > 0.0f)
				Dir = normalize(pTarget->m_Pos - m_Pos);
			else
				Dir = vec2(0.f, -1.f);
			// the ghosts can't be damaged
			if(pTarget->GetPlayer()->GetTeam() == TEAM_RED)
				continue;

			pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, Dir * -1, Damage,
				m_pPlayer->GetCID(), m_ActiveWeapon);
			Hits++;
		}

		// if we Hit anything, we have to wait for the reload
		if(Hits)
			m_ReloadTimer = Server()->TickSpeed() / 3;
	}
	break;

	case WEAPON_GUN: // flash light
	{
		if(m_HasFlashlight)
		{
			m_IsFlashlightOpened = !m_IsFlashlightOpened;
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			return;
		}

		new CProjectile(GameWorld(), WEAPON_GUN,
			m_pPlayer->GetCID(),
			ProjStartPos,
			Direction,
			(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime),
			g_pData->m_Weapons.m_Gun.m_pBase->m_Damage, false, 0, -1, WEAPON_GUN);

		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
	}
	break;

	case WEAPON_SHOTGUN:
	{
		int ShotSpread = 2;

		for(int i = -ShotSpread; i <= ShotSpread; ++i)
		{
			float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
			float a = angle(Direction);
			a += Spreading[i + 2];
			float v = 1 - (absolute(i) / (float) ShotSpread);
			float Speed = mix((float) GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
			new CProjectile(GameWorld(), WEAPON_SHOTGUN,
				m_pPlayer->GetCID(),
				ProjStartPos,
				vec2(cosf(a), sinf(a)) * Speed,
				(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime),
				g_pData->m_Weapons.m_Shotgun.m_pBase->m_Damage, false, 0, -1, WEAPON_SHOTGUN);
		}

		GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
	}
	break;

	case WEAPON_GRENADE:
	{
		if(m_HasGhostCleaner)
		{
			if(m_GhostCleanerPower)
			{
				m_IsGhostCleanerUsing = true;
				if(Server()->Tick() % 4 == 0)
					GameServer()->CreateSound(m_Pos, SOUND_HOOK_LOOP);
			}
			return;
		}

		new CProjectile(GameWorld(), WEAPON_GRENADE,
			m_pPlayer->GetCID(),
			ProjStartPos,
			Direction,
			(int) (Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime),
			g_pData->m_Weapons.m_Grenade.m_pBase->m_Damage, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
	}
	break;

	case WEAPON_LASER:
	{
		new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
		GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);
	}
	break;

	case WEAPON_NINJA:
	{
		m_NumObjectsHit = 0;

		m_Ninja.m_ActivationDir = Direction;
		m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
		m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

		GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
	}
	break;
	}

	m_AttackTick = Server()->Tick();

	if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if(!m_ReloadTimer)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	// ninja
	HandleNinja();

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	/*
	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if(AmmoRegenTime && m_aWeapons[m_ActiveWeapon].m_Ammo >= 0)
	{
		// If equipped and not active, regen ammo?
		if(m_ReloadTimer <= 0)
		{
			if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = minimum(m_aWeapons[m_ActiveWeapon].m_Ammo + 1,
					g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Maxammo);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}

	return;
	*/
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = minimum(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if(m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	if(IsSurpriseFrozen())
	{
		m_NumInputs++;
		return;
	}

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	if(IsSurpriseFrozen())
	{
		return;
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire & 1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	// set emote
	if(m_EmoteStop < Server()->Tick())
	{
		SetEmote(EMOTE_NORMAL, -1);
	}

	if(m_IsCaught)
	{
		m_IsVisible = true;

		if(!IsEscapingFrozen() && m_Input.m_Jump && !(m_Core.m_Jumped & 1) && random_int(0, 6) < 1)
		{
			AddEscapeProgress(random_int(1, 3));

			if(GetEscapeProgress() == 20)
			{
				m_pHunter->OnCharacterDeadOrEscaped(this);
				GameServer()->CreateSound(m_Pos, SOUND_CTF_GRAB_EN);
				if(GameServer()->Collision()->TestBox(m_Pos, vec2(GetProximityRadius(), GetProximityRadius())))
					SetPos(m_pHunter->GetPos());
				BeCaught(nullptr, false);
			}
			else if(GetEscapeProgress() > 15)
			{
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
			}
		}
		m_pPlayer->m_LastKillTick = Server()->Tick();
	}
	else if(m_pPlayer->GetTeam() == TEAM_RED)
	{
		bool Visible = false;
		CCharacter *apEnts[MAX_PLAYERS];
		float LightLength = 512.0f;
		float Spreading = 0.355f;
		int Num = GameWorld()->FindEntities(m_Pos, LightLength, (CEntity **) apEnts,
			MAX_PLAYERS, CGameWorld::ENTTYPE_CHARACTER);

		for(int i = 0; i < Num; ++i)
		{
			CCharacter *pChr = apEnts[i];
			if(pChr == this || pChr->GetPlayer()->GetTeam() != TEAM_BLUE)
				continue;

			vec2 Direction = pChr->GetDirection();
			vec2 TargetDirection = normalize(m_Pos - pChr->GetPos());
			if(acosf(dot(TargetDirection, Direction)) > Spreading)
				continue;

			vec2 StartPos = pChr->GetPos() + Direction * GetProximityRadius() * 0.75f;
			if(!Visible && pChr->IsLighting())
			{
				if(!GameServer()->Collision()->IntersectLine(StartPos, m_Pos, nullptr, nullptr))
					Visible = true;
			}
			if(pChr->IsGhostCleanerUsing())
			{
				pChr->SetEmote(EMOTE_HAPPY, Server()->Tick() + 1);
				if(!m_pPlayer->m_LastEmoteTick || m_pPlayer->m_LastEmoteTick + Server()->TickSpeed() * 3 < Server()->Tick())
				{
					m_pPlayer->m_LastEmoteTick = Server()->Tick();
					GameServer()->SendEmoticon(m_pPlayer->GetCID(), EMOTICON_OOP);
				}

				BeDraging(pChr->m_Pos);
				if(distance(pChr->GetPos(), m_Pos) < GetProximityRadius() * 2)
				{
					GameServer()->CreateSound(m_Pos, SOUND_CTF_RETURN);
					pChr->CatchGhost(this);
					BeCaught(pChr, true);
					break;
				}
			}
		}
		if(Server()->Tick() - m_LastVisibleTick < Server()->TickSpeed() * 2)
		{
			Visible = true;
		}

		if(!m_IsVisible && Visible)
		{
			GameServer()->CreatePlayerSpawn(m_Pos);
			m_LastVisibleTick = Server()->Tick();
		}
		m_IsVisible = Visible;
		SetEmote(m_IsVisible ? EMOTE_SURPRISE : EMOTE_BLINK, Server()->Tick() + 1);
	}
	else if(m_pPlayer->GetTeam() == TEAM_BLUE)
	{
		if(m_HasFlashlight && m_ActiveWeapon == WEAPON_GUN)
		{
			m_aWeapons[WEAPON_GUN].m_Ammo = round_to_int(m_FlashlightPower / 450.f);
			if(m_IsFlashlightOpened && m_FlashlightPower)
			{
				m_FlashlightPower--;
				if(!m_FlashlightPower) // No power
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, CmaskOne(m_pPlayer->GetCID()));
				else if(m_aWeapons[WEAPON_GUN].m_Ammo != round_to_int(m_FlashlightPower / 450.f))
					GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, CmaskOne(m_pPlayer->GetCID()));
			}
		}

		if(m_HasGhostCleaner && m_ActiveWeapon == WEAPON_GRENADE)
		{
			m_aWeapons[WEAPON_GRENADE].m_Ammo = round_to_int(m_GhostCleanerPower / 300.f);
			if(m_GhostCleanerPower)
			{
				m_GhostCleanerPower -= m_IsGhostCleanerUsing ? 2 : 1;

				if(!m_GhostCleanerPower) // No power
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, CmaskOne(m_pPlayer->GetCID()));
				else if(m_aWeapons[WEAPON_GRENADE].m_Ammo != round_to_int(m_GhostCleanerPower / 300.f))
					GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, CmaskOne(m_pPlayer->GetCID()));
			}
		}

		// broadcast about export
		if(GameServer()->Collision()->TestBox(m_Pos, vec2(GetProximityRadius(), GetProximityRadius()), CCollision::COLFLAG_EXPORT))
		{
			if(m_pPlayer->m_LastEmoteTick == Server()->Tick() - 1)
			{
				char aWithMsg[32];
				if(m_vCaughtGhosts.empty())
				{
					str_copy(aWithMsg, "nothing", sizeof(aWithMsg));
				}
				else if(m_vCaughtGhosts.size() == 1)
				{
					str_copy(aWithMsg, "a ghost", sizeof(aWithMsg));
				}
				else if(m_vCaughtGhosts.size() > 1)
				{
					str_format(aWithMsg, sizeof(aWithMsg), "%lu ghost", m_vCaughtGhosts.size());
				}
				else
				{
					str_copy(aWithMsg, "error", sizeof(aWithMsg));
				}

				char aMsg[64];
				str_format(aMsg, sizeof(aMsg), "'%s' has left with %s", Server()->ClientName(m_pPlayer->GetCID()), aWithMsg);
				GameServer()->SendChat(-1, CHAT_ALL, -1, aMsg);
				for(auto &pGhost : m_vCaughtGhosts)
				{
					pGhost->Die(m_pPlayer->GetCID(), WEAPON_GRENADE);
				}
				m_vCaughtGhosts.clear();
				GameServer()->m_pController->DoTeamChange(m_pPlayer, TEAM_RED, false);
				return;
			}
			else if(!m_pPlayer->m_LastGameInformationTick || Server()->Tick() - m_pPlayer->m_LastGameInformationTick >= Server()->TickSpeed())
			{
				GameServer()->SendBroadcast("Send emoticon", m_pPlayer->GetCID());
				m_pPlayer->m_LastGameInformationTick = Server()->Tick();
			}
		}

		for(auto &pGhost : m_vCaughtGhosts)
		{
			if(!pGhost)
				continue;
			pGhost->SetVel(m_Core.m_Vel);
			pGhost->SetPos(m_Pos + vec2(sign(m_LatestInput.m_TargetX) * (m_ActiveWeapon == WEAPON_HAMMER ? 32.f : -32.f), 0.f));
		}

		m_IsVisible = true;
	}
	m_IsGhostCleanerUsing = false;

	m_Core.m_Input = m_Input;
	m_Core.Tick(true);

	// handle leaving gamelayer
	if(GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
	HandleWeapons();
}

void CCharacter::TickDefered()
{
	static const vec2 ColBox(CCharacterCore::PHYS_SIZE, CCharacterCore::PHYS_SIZE);
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	if(m_IsCaught)
	{
		// TODO: Fake tuning
		m_Core.m_HookPos = m_Pos;
		m_Core.m_Jumped &= ~2;
		m_Core.m_Jumped &= ~1;
		SetEmote(EMOTE_PAIN, Server()->Tick() + 1);
	}

	// apply drag velocity when the player is not firing ninja
	// and set it back to 0 for the next tick
	if(m_ActiveWeapon != WEAPON_NINJA || m_Ninja.m_CurrentMoveTime < 0)
		m_Core.AddDragVelocity();
	m_Core.ResetDragVelocity();

	// lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);

	m_Core.Move();

	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, ColBox);
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		} StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	m_TriggeredEvents |= m_Core.m_TriggeredEvents;

	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}
	else if(m_Core.m_Death)
	{
		// handle death-tiles
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reckoning for a top of 3 seconds
		if(m_ReckoningTick + Server()->TickSpeed() * 3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health + Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor + Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_Alive = false;
	m_pPlayer->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, (Killer < 0) ? 0 : GameServer()->m_apPlayers[Killer], Weapon);

	char aBuf[256];
	if(Killer < 0)
	{
		str_format(aBuf, sizeof(aBuf), "kill killer='%d:%d:' victim='%d:%d:%s' weapon=%d special=%d",
			Killer, -1 - Killer,
			m_pPlayer->GetCID(), m_pPlayer->GetTeam(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "kill killer='%d:%d:%s' victim='%d:%d:%s' weapon=%d special=%d",
			Killer, GameServer()->m_apPlayers[Killer]->GetTeam(), Server()->ClientName(Killer),
			m_pPlayer->GetCID(), m_pPlayer->GetTeam(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
	}
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_ModeSpecial = ModeSpecial;
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(!Server()->ClientIngame(i))
			continue;

		if(Killer < 0 && Server()->GetClientVersion(i) < MIN_KILLMESSAGE_CLIENTVERSION)
		{
			Msg.m_Killer = 0;
			Msg.m_Weapon = WEAPON_WORLD;
		}
		else
		{
			Msg.m_Killer = Killer;
			Msg.m_Weapon = Weapon;
		}
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	for(auto &pGhost : m_vCaughtGhosts)
	{
		if(!pGhost)
			continue;
		pGhost->BeCaught(nullptr, false);
	}

	GameWorld()->RemoveEntity(this);
	GameWorld()->m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, vec2 Source, int Dmg, int From, int Weapon)
{
	m_Core.m_Vel += Force;

	if(From >= 0)
	{
		if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From))
			return false;
		// oh no! too surprise!
		if(m_pPlayer->GetTeam() == TEAM_BLUE && GameServer()->m_apPlayers[From]->GetTeam() == TEAM_RED)
		{
			m_SurpriseFrozenTick = Server()->Tick();
		}
	}
	else
	{
		int Team = TEAM_RED;
		if(From == PLAYER_TEAM_BLUE)
			Team = TEAM_BLUE;
		if(GameServer()->m_pController->IsFriendlyTeamFire(m_pPlayer->GetTeam(), Team))
			return false;
	}

	// m_pPlayer only inflicts half damage on self
	if(From == m_pPlayer->GetCID())
		Dmg = maximum(1, Dmg / 2);

	int OldHealth = m_Health, OldArmor = m_Armor;
	if(Dmg)
	{
		if(m_Armor)
		{
			if(Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if(Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	// create healthmod indicator
	GameServer()->CreateDamage(m_Pos, m_pPlayer->GetCID(), Source, OldHealth - m_Health, OldArmor - m_Armor, From == m_pPlayer->GetCID());

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64_t Mask = CmaskOne(From);
		for(int i = 0; i < MAX_PLAYERS; i++)
		{
			if(GameServer()->m_apPlayers[i] && (GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || GameServer()->m_apPlayers[i]->m_DeadSpecMode) &&
				GameServer()->m_apPlayers[i]->GetSpectatorID() == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0)
	{
		// set attacker's face to happy (taunt!)
		if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if(pChr)
			{
				pChr->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
			}
		}

		if(m_pPlayer->GetTeam() == TEAM_BLUE)
		{
			GameServer()->m_pController->OnCharacterDeath(this, (From < 0) ? nullptr : GameServer()->m_apPlayers[From], Weapon);
			GameServer()->m_pController->DoTeamChange(m_pPlayer, TEAM_RED, false);
		}
		else
			Die(From, Weapon);

		return false;
	}

	if(Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	SetEmote(EMOTE_PAIN, Server()->Tick() + 500 * Server()->TickSpeed() / 1000);

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	if(SnappingClient != -1 && !m_IsVisible)
	{
		if(GameServer()->m_apPlayers[SnappingClient]->GetTeam() != m_pPlayer->GetTeam())
			return;
	}

	SnapCharacter(SnappingClient);

	if(IsLighting())
	{
		float LightLength = 512.0f;
		float Spreading[] = {-0.355f, 0.355f};
		vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
		vec2 StartPos = m_Pos + Direction * GetProximityRadius() * 0.75f;

		for(int i = 0; i < 2; i++)
		{
			vec2 LightDir = direction(angle(Direction) + Spreading[i]);
			vec2 EndPos = StartPos + LightDir * LightLength;
			GameServer()->Collision()->IntersectLine(StartPos, EndPos, nullptr, &EndPos);

			if(NetworkClippedLine(SnappingClient, StartPos, EndPos))
				continue;

			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aFlashlightIDs[i], sizeof(CNetObj_Laser)));
			if(!pObj)
				return;

			pObj->m_X = round_to_int(StartPos.x);
			pObj->m_Y = round_to_int(StartPos.y);
			pObj->m_FromX = round_to_int(EndPos.x);
			pObj->m_FromY = round_to_int(EndPos.y);
			pObj->m_StartTick = Server()->Tick() - (int) ((distance(StartPos, EndPos) / LightLength) * 4);
		}
	}

	if(m_HasGhostCleaner)
	{
		CNetObj_Pickup *pObj = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
		if(!pObj)
			return;

		pObj->m_X = round_to_int(m_Pos.x + sign(m_LatestInput.m_TargetX) * (m_ActiveWeapon == WEAPON_HAMMER ? 32.f : -32.f));
		pObj->m_Y = round_to_int(m_Pos.y);
		pObj->m_Type = PICKUP_ARMOR;
	}
}

void CCharacter::PostSnap()
{
	m_TriggeredEvents = 0;
}

void CCharacter::SnapCharacter(int SnappingClient)
{
	if(NetworkClippedLine(SnappingClient, m_Pos, m_Core.m_HookPos))
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, m_pPlayer->GetCID(), sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(!m_ReckoningTick || GameWorld()->m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;
	pCharacter->m_TriggeredEvents = m_TriggeredEvents;

	pCharacter->m_Weapon = IsEscapingFrozen() ? WEAPON_NINJA : m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!Config()->m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->GetSpectatorID()))
	{
		pCharacter->m_Health = m_IsCaught ? clamp(m_EscapeProgress, 0, 10) : m_Health;
		pCharacter->m_Armor = m_IsCaught ? clamp(m_EscapeProgress - 10, 0, 10) : m_Armor;
		if(m_ActiveWeapon == WEAPON_NINJA)
			pCharacter->m_AmmoCount = m_Ninja.m_ActivationTick + g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000;
		else if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(5 * Server()->TickSpeed() - ((Server()->Tick() - m_LastAction) % (5 * Server()->TickSpeed())) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}
}

vec2 CCharacter::GetDirection() const
{
	return normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
}

bool CCharacter::IsSurpriseFrozen()
{
	return m_SurpriseFrozenTick > -1 && Server()->Tick() - m_SurpriseFrozenTick < Server()->TickSpeed() / 10;
}

bool CCharacter::IsEscapingFrozen()
{
	return m_EscapingFrozenTick > -1 && Server()->Tick() - m_EscapingFrozenTick < 5 * Server()->TickSpeed();
}

bool CCharacter::IsLighting()
{
	return (m_ActiveWeapon == WEAPON_GUN && m_HasFlashlight && m_IsFlashlightOpened && m_FlashlightPower) ||
	       (m_ActiveWeapon == WEAPON_GRENADE && m_HasGhostCleaner && m_GhostCleanerPower);
}

void CCharacter::AddEscapeProgress(int Progress)
{
	if(Progress < 0 && absolute(Progress) >= m_EscapeProgress && m_EscapeProgress > 0)
	{
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
		m_EscapingFrozenTick = Server()->Tick();
	}

	m_EscapeProgress = clamp(m_EscapeProgress + Progress, 0, 20);
}

void CCharacter::CatchGhost(CCharacter *pGhost)
{
	for(auto &pCaughtGhost : m_vCaughtGhosts)
	{
		if(pCaughtGhost == pGhost)
			return;
	}

	m_vCaughtGhosts.push_back(pGhost);
}

void CCharacter::BeDraging(vec2 From)
{
	m_Core.m_Vel -= normalize(m_Pos - From) * 2.0f;
	m_LastVisibleTick = Server()->Tick();
}

void CCharacter::BeCaught(CCharacter *pHunter, bool Catch)
{
	m_pHunter = pHunter;
	m_IsCaught = Catch;
	m_EscapeProgress = 0;

	if(Catch)
		m_EscapingFrozenTick = Server()->Tick();
}

void CCharacter::SetFlashlight(bool Give)
{
	m_HasFlashlight = Give;
	m_FlashlightPower = Give ? 4500 : 0;
}

void CCharacter::SetGhostCleaner(bool Give)
{
	m_HasGhostCleaner = Give;
	m_GhostCleanerPower = Give ? 3000 : 0;
}

void CCharacter::SetPos(vec2 Pos)
{
	m_Core.m_Pos = Pos;
	m_Pos = Pos;
}

void CCharacter::SetVel(vec2 Vel)
{
	m_Core.m_Vel = Vel;
}

void CCharacter::ClearCaughtList()
{
	m_vCaughtGhosts.clear();
}

void CCharacter::OnCharacterDeadOrEscaped(CCharacter *pChr)
{
	if(m_pHunter == pChr)
		BeCaught(nullptr, false);

	auto Find = std::find(m_vCaughtGhosts.begin(), m_vCaughtGhosts.end(), pChr);
	if(Find == m_vCaughtGhosts.end())
		return;
	m_vCaughtGhosts.erase(Find);
}

void CCharacter::OnKilledByGhost(CPlayer *pGhost)
{
	if(!pGhost)
		return;

	pGhost->m_Score += m_vCaughtGhosts.size() * 3; // rescue a ghost score +3
}