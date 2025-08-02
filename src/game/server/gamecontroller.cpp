/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/mapitems.h>

#include "entities/character.h"
#include "entities/pickup.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"

#include <vector>

CGameController::CGameController(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();

	// game
	m_GameStartTick = Server()->Tick();
	m_GameEndTick = -1;

	// info
	m_GameFlags = GAMEFLAG_TEAMS;
	m_RealPlayerNum = 0;
	m_aTeamPlayersCount[TEAM_RED] = 0;
	m_aTeamPlayersCount[TEAM_BLUE] = 0;
	m_pGameType = "GhostHunt idm";

	// spawn
	m_aNumSpawnPoints[0] = 0;
	m_aNumSpawnPoints[1] = 0;
	m_aNumSpawnPoints[2] = 0;
}

// activity
void CGameController::DoActivityCheck()
{
	if(Config()->m_SvInactiveKickTime == 0)
		return;

	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(GameServer()->m_apPlayers[i] && !GameServer()->m_apPlayers[i]->IsDummy() && (GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS || Config()->m_SvInactiveKick > 0) &&
			!Server()->IsAuthed(i) && (GameServer()->m_apPlayers[i]->m_InactivityTickCounter > Config()->m_SvInactiveKickTime * Server()->TickSpeed() * 60))
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS)
			{
				if(Config()->m_SvInactiveKickSpec)
					Server()->Kick(i, "Kicked for inactivity");
			}
			else
			{
				switch(Config()->m_SvInactiveKick)
				{
				case 1:
				{
					// move player to spectator
					DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 2:
				{
					// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
					int Spectators = 0;
					for(int j = 0; j < MAX_PLAYERS; ++j)
						if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
							++Spectators;
					if(Spectators >= Config()->m_SvMaxClients - Config()->m_SvPlayerSlots)
						Server()->Kick(i, "Kicked for inactivity");
					else
						DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 3:
				{
					// kick the player
					Server()->Kick(i, "Kicked for inactivity");
				}
				}
			}
		}
	}
}

// event
int CGameController::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
	if(pKiller == pVictim->GetPlayer())
		pVictim->GetPlayer()->m_Score--; // suicide or world
	else
	{
		if(pKiller->GetTeam() == TEAM_BLUE)
		{
			pKiller->m_Score += 4; // caught a ghost score +4
		}
		else if(pKiller->GetTeam() == TEAM_RED)
		{
			pKiller->m_Score += 3; // kill a human score +3
			pVictim->OnKilledByGhost(pKiller); // and extra score
		}
	}
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() * 3.0f;

	// update spectator modes for dead players in survival
	if(m_GameFlags & GAMEFLAG_SURVIVAL)
	{
		for(int i = 0; i < MAX_PLAYERS; ++i)
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_DeadSpecMode)
				GameServer()->m_apPlayers[i]->UpdateDeadSpecMode();
	}

	return 0;
}

void CGameController::OnCharacterSpawn(CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);

	if(pChr->GetPlayer()->GetTeam() == TEAM_BLUE) // human
	{
		// give flashlight
		pChr->GiveWeapon(WEAPON_GUN, -1);
		pChr->SetFlashlight(true);

		// give ghostcleaner
		pChr->GiveWeapon(WEAPON_GRENADE, -1);
		pChr->SetGhostCleaner(true);
	}
}

bool CGameController::OnEntity(int Index, vec2 Pos)
{
	// don't add pickups in survival
	if(m_GameFlags & GAMEFLAG_SURVIVAL)
	{
		if(Index < ENTITY_SPAWN || Index > ENTITY_SPAWN_BLUE)
			return false;
	}

	int Type = -1;

	switch(Index)
	{
	case ENTITY_SPAWN:
		m_aaSpawnPoints[0][m_aNumSpawnPoints[0]++] = Pos;
		break;
	case ENTITY_SPAWN_RED:
		m_aaSpawnPoints[1][m_aNumSpawnPoints[1]++] = Pos;
		break;
	case ENTITY_SPAWN_BLUE:
		m_aaSpawnPoints[2][m_aNumSpawnPoints[2]++] = Pos;
		break;
	case ENTITY_ARMOR_1:
		Type = PICKUP_ARMOR;
		break;
	case ENTITY_HEALTH_1:
		Type = PICKUP_HEALTH;
		break;
		/*
		case ENTITY_WEAPON_SHOTGUN:
			Type = PICKUP_SHOTGUN;
			break;
		case ENTITY_WEAPON_GRENADE:
			Type = PICKUP_GRENADE;
			break;
		case ENTITY_WEAPON_LASER:
			Type = PICKUP_LASER;
			break;
		case ENTITY_POWERUP_NINJA:
			Type = PICKUP_NINJA;
			break;
		*/
	}

	if(Type != -1)
	{
		new CPickup(&GameServer()->m_World, Type, Pos);
		return true;
	}

	return false;
}

void CGameController::OnPlayerConnect(CPlayer *pPlayer)
{
	int ClientID = pPlayer->GetCID();
	pPlayer->Respawn();
	if(pPlayer->GetTeam() != TEAM_SPECTATORS)
		m_aTeamPlayersCount[pPlayer->GetTeam()]++;

	m_RealPlayerNum++;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// update game info
	SendGameInfo(ClientID);
}

void CGameController::OnPlayerDisconnect(CPlayer *pPlayer)
{
	if(pPlayer->GetTeam() == TEAM_RED && pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsCaught())
	{
		char aAddr[NETADDR_MAXSTRSIZE];
		Server()->GetClientAddr(pPlayer->GetCID(), aAddr, sizeof(aAddr));

		char aCommand[128];
		str_format(aCommand, sizeof(aCommand), "ban \"%s\" 5 \"Leaver Ghost\"", aAddr);
		GameServer()->Console()->ExecuteLine(aCommand);
	}

	pPlayer->OnDisconnect();

	m_RealPlayerNum--;
	if(pPlayer->GetTeam() != TEAM_SPECTATORS)
		m_aTeamPlayersCount[pPlayer->GetTeam()]--;

	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void CGameController::OnPlayerInfoChange(CPlayer *pPlayer)
{
}

void CGameController::OnPlayerReadyChange(CPlayer *pPlayer)
{
	if(pPlayer->GetTeam() != TEAM_SPECTATORS && !pPlayer->m_DeadSpecMode)
	{
		// change players ready state
		pPlayer->m_IsReadyToPlay ^= 1;
	}
}

void CGameController::OnReset()
{
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->m_RespawnDisabled = false;
			GameServer()->m_apPlayers[i]->Respawn();
			GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
			GameServer()->m_apPlayers[i]->m_Score = 0;
			GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
			GameServer()->m_apPlayers[i]->m_IsReadyToPlay = true;
		}
	}
}

// game
void CGameController::ResetGame()
{
	// reset the game
	GameServer()->m_World.m_ResetRequested = true;
	GameServer()->m_World.m_Paused = false;
	m_GameStartTick = Server()->Tick();
}

// general
void CGameController::Snap(int SnappingClient)
{
	CNetObj_GameData *pGameData = static_cast<CNetObj_GameData *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData)));
	if(!pGameData)
		return;

	pGameData->m_GameStartTick = m_GameStartTick;
	pGameData->m_GameStateFlags = m_GamePreparing ? GAMESTATEFLAG_WARMUP : 0;
	pGameData->m_GameStateEndTick = maximum(0, m_GameEndTick); // no timer/infinite = 0, on end = GameEndTick, otherwise = GameStateEndTick

	CNetObj_GameDataTeam *pGameDataTeam = static_cast<CNetObj_GameDataTeam *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATATEAM, 0, sizeof(CNetObj_GameDataTeam)));
	if(!pGameDataTeam)
		return;

	pGameDataTeam->m_TeamscoreBlue = m_aTeamPlayersCount[TEAM_BLUE];
	pGameDataTeam->m_TeamscoreRed = m_aTeamPlayersCount[TEAM_RED];

	// demo recording
	if(SnappingClient == -1)
	{
		CNetObj_De_GameInfo *pGameInfo = static_cast<CNetObj_De_GameInfo *>(Server()->SnapNewItem(NETOBJTYPE_DE_GAMEINFO, 0, sizeof(CNetObj_De_GameInfo)));
		if(!pGameInfo)
			return;

		pGameInfo->m_GameFlags = m_GameFlags;
		pGameInfo->m_ScoreLimit = 0;
		pGameInfo->m_TimeLimit = 0;
		pGameInfo->m_MatchNum = 0;
		pGameInfo->m_MatchCurrent = 0;
	}
}

void CGameController::Tick()
{
	// check for inactive players
	DoActivityCheck();

	int TotalPlayersNum = m_aTeamPlayersCount[TEAM_RED] + m_aTeamPlayersCount[TEAM_BLUE];
	m_GamePreparing = TotalPlayersNum < 3;

	if(m_GameEndTick > -1)
	{
		if(m_GameEndTick + Server()->TickSpeed() * 10 > Server()->Tick())
		{
			GameServer()->m_World.m_Paused = true;
			return;
		}
		m_GameEndTick = -1;
		m_GameStarted = false;
	}

	if(m_GamePreparing)
	{
		if(Server()->Tick() % Server()->TickSpeed() == 0)
			GameServer()->SendBroadcast("Waiting for more players...", -1);
		if(m_GameStarted)
		{
			// clear humans
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_BLUE)
					DoTeamChange(GameServer()->m_apPlayers[i], TEAM_RED, false);
			}
		}
		m_GameStarted = false;
		return;
	}

	// start game!
	if(!m_GameStarted)
	{
		ResetGame();

		GameServer()->SendChat(-1, CHAT_ALL, -1, "⚠|Ghost clean task: Start!");
		// choose humans
		int Humans = TotalPlayersNum / 3;
		static std::vector<int> vPlayers;
		vPlayers.clear();
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i])
			{
				GameServer()->m_apPlayers[i]->KillCharacter();
				if(GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
					vPlayers.push_back(i);
			}
		}

		dbg_assert(vPlayers.size() >= (size_t) Humans, "bad error: should find enough players but not found");

		for(int i = 0; i < Humans; i++)
		{
			int RandomIndex = random_int(0, vPlayers.size());
			DoTeamChange(GameServer()->m_apPlayers[vPlayers[RandomIndex]], TEAM_BLUE, false);

			// remove
			vPlayers.erase(vPlayers.begin() + RandomIndex);
		}

		m_GameStarted = true;
	}
	else if(m_aTeamPlayersCount[TEAM_BLUE] == 0) // do win check
	{
		GameServer()->SendChat(-1, CHAT_ALL, -1, "All of the humans had escaped or been killed!");
		GameServer()->SendChat(-1, CHAT_ALL, -1, "⚠|Ghost clean task: Finish!");

		GameServer()->m_World.m_Paused = true;
		m_GameEndTick = Server()->Tick();
	}
}

void CGameController::SendGameInfo(int ClientID)
{
	CNetMsg_Sv_GameInfo GameInfoMsg;
	GameInfoMsg.m_GameFlags = m_GameFlags;
	GameInfoMsg.m_ScoreLimit = 0;
	GameInfoMsg.m_TimeLimit = 0;
	GameInfoMsg.m_MatchNum = 0;
	GameInfoMsg.m_MatchCurrent = 0;

	CNetMsg_Sv_GameInfo GameInfoMsgNoRace = GameInfoMsg;
	GameInfoMsgNoRace.m_GameFlags &= ~GAMEFLAG_RACE;

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_PLAYERS; ++i)
		{
			if(!GameServer()->m_apPlayers[i] || !Server()->ClientIngame(i))
				continue;

			CNetMsg_Sv_GameInfo *pInfoMsg = (Server()->GetClientVersion(i) < CGameContext::MIN_RACE_CLIENTVERSION) ? &GameInfoMsgNoRace : &GameInfoMsg;
			Server()->SendPackMsg(pInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
		}
	}
	else
	{
		CNetMsg_Sv_GameInfo *pInfoMsg = (Server()->GetClientVersion(ClientID) < CGameContext::MIN_RACE_CLIENTVERSION) ? &GameInfoMsgNoRace : &GameInfoMsg;
		Server()->SendPackMsg(pInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
	}
}

// spawn
bool CGameController::CanSpawn(int Team, vec2 *pOutPos) const
{
	// spectators can't spawn
	if(Team == TEAM_SPECTATORS || GameServer()->m_World.m_Paused || GameServer()->m_World.m_ResetRequested)
		return false;

	CSpawnEval Eval;
	Eval.m_RandomSpawn = false;

	if(IsTeamplay())
	{
		EvaluateSpawnType(&Eval, 0);
		EvaluateSpawnType(&Eval, Team + 1);
	}
	else
	{
		EvaluateSpawnType(&Eval, 0);
		EvaluateSpawnType(&Eval, 1);
		EvaluateSpawnType(&Eval, 2);
	}

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

float CGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos) const
{
	float Score = 0.0f;
	CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
	for(; pC; pC = (CCharacter *) pC->TypeNext())
	{
		// team mates are not as dangerous as enemies
		float Scoremod = 1.0f;
		if(pEval->m_FriendlyTeam != -1 && pC->GetPlayer()->GetTeam() == pEval->m_FriendlyTeam)
			Scoremod = 0.5f;

		float d = distance(Pos, pC->GetPos());
		Score += Scoremod * (d == 0 ? 1000000000.0f : 1.0f / d);
	}

	return Score;
}

void CGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type) const
{
	// get spawn point
	for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
	{
		// check if the position is occupado
		CCharacter *aEnts[MAX_PLAYERS];
		int Num = GameServer()->m_World.FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity **) aEnts, MAX_PLAYERS, CGameWorld::ENTTYPE_CHARACTER);
		vec2 Positions[5] = {vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f)}; // start, left, up, right, down
		int Result = -1;
		for(int Index = 0; Index < 5 && Result == -1; ++Index)
		{
			Result = Index;
			for(int c = 0; c < Num; ++c)
				if(GameServer()->Collision()->CheckPoint(m_aaSpawnPoints[Type][i] + Positions[Index]) ||
					distance(aEnts[c]->GetPos(), m_aaSpawnPoints[Type][i] + Positions[Index]) <= aEnts[c]->GetProximityRadius())
				{
					Result = -1;
					break;
				}
		}
		if(Result == -1)
			continue; // try next spawn point

		vec2 P = m_aaSpawnPoints[Type][i] + Positions[Result];
		float S = pEval->m_RandomSpawn ? (Result + random_float()) : EvaluateSpawnPos(pEval, P);
		if(!pEval->m_Got || pEval->m_Score > S)
		{
			pEval->m_Got = true;
			pEval->m_Score = S;
			pEval->m_Pos = P;
		}
	}
}

// team
bool CGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam) const
{
	if(pPlayer->GetTeam() == TEAM_RED && pPlayer->GetCharacter())
	{
		if(pPlayer->GetCharacter()->IsCaught())
			return false; // I don't think this should be allowed.
	}

	if(JoinTeam == TEAM_SPECTATORS)
		return true;
	if(JoinTeam == TEAM_BLUE)
		return false;

	return true;
}

int CGameController::ClampTeam(int Team) const
{
	if(Team < TEAM_RED)
		return TEAM_SPECTATORS;
	return Team ? TEAM_BLUE : TEAM_RED;
}

void CGameController::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	int OldTeam = pPlayer->GetTeam();
	pPlayer->SetTeam(Team);

	int ClientID = pPlayer->GetCID();

	// notify clients
	CNetMsg_Sv_Team Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Team = Team;
	Msg.m_Silent = DoChatMsg ? 0 : 1;
	Msg.m_CooldownTick = pPlayer->m_TeamChangeTick;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d->%d", ClientID, Server()->ClientName(ClientID), OldTeam, Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	OnPlayerInfoChange(pPlayer);
	GameServer()->OnClientTeamChange(ClientID);

	// reset inactivity counter when joining the game
	if(OldTeam == TEAM_SPECTATORS)
		pPlayer->m_InactivityTickCounter = 0;
	else
		m_aTeamPlayersCount[OldTeam]--;

	if(pPlayer->GetTeam() != TEAM_SPECTATORS)
		m_aTeamPlayersCount[pPlayer->GetTeam()]++;
}

bool CGameController::IsFriendlyFire(int ClientID1, int ClientID2) const
{
	if(ClientID1 == ClientID2)
		return false;

	if(IsTeamplay())
	{
		if(!GameServer()->m_apPlayers[ClientID1] || !GameServer()->m_apPlayers[ClientID2])
			return false;

		if(!Config()->m_SvTeamdamage && GameServer()->m_apPlayers[ClientID1]->GetTeam() == GameServer()->m_apPlayers[ClientID2]->GetTeam())
			return true;
	}

	return false;
}

bool CGameController::IsFriendlyTeamFire(int Team1, int Team2) const
{
	return IsTeamplay() && !Config()->m_SvTeamdamage && Team1 == Team2;
}

void CGameController::RegisterChatCommands(CCommandManager *pManager)
{
}
