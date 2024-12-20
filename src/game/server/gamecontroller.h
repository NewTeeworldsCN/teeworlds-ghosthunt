/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/tl/array.h>
#include <base/vmath.h>

#include <game/commands.h>

#include <generated/protocol.h>

/*
	Class: Game Controller
		Controls the main game logic. Keeping track of team and player score,
		winning conditions and specific game logic.
*/
class CGameController
{
	class CGameContext *m_pGameServer;
	class CConfig *m_pConfig;
	class IServer *m_pServer;

	// activity
	void DoActivityCheck();

	void ResetGame();

	// spawn
	struct CSpawnEval
	{
		CSpawnEval()
		{
			m_Got = false;
			m_FriendlyTeam = -1;
			m_Pos = vec2(100, 100);
		}

		vec2 m_Pos;
		bool m_Got;
		bool m_RandomSpawn;
		int m_FriendlyTeam;
		float m_Score;
	};
	vec2 m_aaSpawnPoints[3][64];
	int m_aNumSpawnPoints[3];

	float EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos) const;
	void EvaluateSpawnType(CSpawnEval *pEval, int Type) const;

	// team
	int ClampTeam(int Team) const;

protected:
	CGameContext *GameServer() const { return m_pGameServer; }
	CConfig *Config() const { return m_pConfig; }
	IServer *Server() const { return m_pServer; }

	// game
	int m_GameStartTick;
	int m_GameEndTick;
	bool m_GamePreparing;
	bool m_GameStarted;

	// info
	int m_GameFlags;
	int m_RealPlayerNum; // warning: includes spectators
	int m_aTeamPlayersCount[NUM_TEAMS];
	const char *m_pGameType;

	void SendGameInfo(int ClientID);

public:
	CGameController(class CGameContext *pGameServer);
	~CGameController() {}

	// event
	/*
		Function: on_CCharacter_death
			Called when a CCharacter in the world dies.

		Arguments:
			victim - The CCharacter that died.
			killer - The player that killed it.
			weapon - What weapon that killed it. Can be -1 for undefined
				weapon when switching team or player suicides.
	*/
	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	/*
		Function: on_CCharacter_spawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	void OnCharacterSpawn(class CCharacter *pChr);

	/*
		Function: on_entity
			Called when the map is loaded to process an entity
			in the map.

		Arguments:
			index - Entity index.
			pos - Where the entity is located in the world.

		Returns:
			bool?
	*/
	bool OnEntity(int Index, vec2 Pos);

	void OnPlayerConnect(class CPlayer *pPlayer);
	void OnPlayerDisconnect(class CPlayer *pPlayer);
	void OnPlayerInfoChange(class CPlayer *pPlayer);
	void OnPlayerReadyChange(class CPlayer *pPlayer);
	void OnPlayerCommand(class CPlayer *pPlayer, const char *pCommandName, const char *pCommandArgs) {};

	void OnReset();

	// general
	void Snap(int SnappingClient);
	void Tick();

	bool CanChangeTeam(class CPlayer *pPlayer, int JoinTeam) const;
	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true);

	bool IsFriendlyFire(int ClientID1, int ClientID2) const;
	bool IsFriendlyTeamFire(int Team1, int Team2) const;
	bool IsTeamplay() const { return m_GameFlags & GAMEFLAG_TEAMS; }

	// info
	const char *GetGameType() const { return m_pGameType; }
	int GetRealPlayerNum() const { return m_RealPlayerNum; }

	// spawn
	bool CanSpawn(int Team, vec2 *pPos) const;

	// static void Com_Example(IConsole::IResult *pResult, void *pContext);
	void RegisterChatCommands(CCommandManager *pManager);
};

#endif
