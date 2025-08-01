/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>

#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/memheap.h>
#include <engine/storage.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/version.h>
#include <generated/server_data.h>

#include "entities/character.h"
#include "entities/projectile.h"

#include "gamecontext.h"
#include "gamecontroller.h"

#include "player.h"

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	for(int i = 0; i < MAX_PLAYERS; i++)
		m_apPlayers[i] = 0;

	m_pController = nullptr;
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;

	if(Resetting == NO_RESET)
		m_pVoteOptionHeap = new CHeap();
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_PLAYERS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new(this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}

class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamage(vec2 Pos, int Id, vec2 Source, int HealthAmount, int ArmorAmount, bool Self)
{
	float f = angle(Source);
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *) m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_ClientID = Id;
		pEvent->m_Angle = (int) (f * 256.0f);
		pEvent->m_HealthAmount = HealthAmount;
		pEvent->m_ArmorAmount = ArmorAmount;
		pEvent->m_Self = Self;
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *) m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}
}

void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, int MaxDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *) m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}

	// deal damage
	CCharacter *apEnts[MAX_PLAYERS];
	float Radius = g_pData->m_Explosion.m_Radius;
	float InnerRadius = 48.0f;
	float MaxForce = g_pData->m_Explosion.m_MaxForce;
	int Num = m_World.FindEntities(Pos, Radius, (CEntity **) apEnts, MAX_PLAYERS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		vec2 Diff = apEnts[i]->GetPos() - Pos;
		vec2 Force(0, MaxForce);
		float l = length(Diff);
		if(l)
			Force = normalize(Diff) * MaxForce;
		float Factor = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		if((int) (Factor * MaxDamage))
			apEnts[i]->TakeDamage(Force * Factor, Diff * -1, (int) (Factor * MaxDamage), Owner, Weapon);
	}
}

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *) m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int) Pos.x;
		ev->m_Y = (int) Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *) m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int64_t Mask)
{
	if(Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *) m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

// ----- send functions -----
void CGameContext::SendChat(int ChatterClientID, int Mode, int To, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_PLAYERS)
	{
		if(Mode == CHAT_TEAM)
		{
			int TeamID = m_apPlayers[ChatterClientID]->GetTeam();
			str_format(aBuf, sizeof(aBuf), "%d:%d:%d:%s: %s", Mode, TeamID, ChatterClientID, Server()->ClientName(ChatterClientID), pText);
		}
		else
			str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", Mode, ChatterClientID, Server()->ClientName(ChatterClientID), pText);
	}
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);

	const char *pModeStr;
	if(Mode == CHAT_WHISPER)
		pModeStr = 0;
	else if(Mode == CHAT_TEAM)
		pModeStr = "teamchat";
	else
		pModeStr = "chat";

	if(pModeStr)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, pModeStr, aBuf);
	}

	CNetMsg_Sv_Chat Msg;
	Msg.m_Mode = Mode;
	Msg.m_ClientID = ChatterClientID;
	Msg.m_pMessage = pText;
	Msg.m_TargetID = -1;

	if(Mode == CHAT_ALL)
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	else if(Mode == CHAT_TEAM)
	{
		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, -1);

		To = m_apPlayers[ChatterClientID]->GetTeam();

		// send to the clients
		for(int i = 0; i < MAX_PLAYERS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == To)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
		}
	}
	else if(Mode == CHAT_WHISPER)
	{
		// send to the clients
		Msg.m_TargetID = To;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
	else // Mode == CHAT_NONE (CHAT_SYSTEM)
	{
		// send to the target
		Msg.m_Mode = CHAT_WHISPER;
		Msg.m_ClientID = -1;
		Msg.m_TargetID = To;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendMotd(int ClientID)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = Config()->m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendSettings(int ClientID)
{
	CNetMsg_Sv_ServerSettings Msg;
	Msg.m_KickVote = Config()->m_SvVoteKick;
	Msg.m_KickMin = Config()->m_SvVoteKickMin;
	Msg.m_SpecVote = Config()->m_SvVoteSpectate;
	Msg.m_TeamLock = 0;
	Msg.m_TeamBalance = 0;
	Msg.m_PlayerSlots = Config()->m_SvPlayerSlots;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendSkinChange(int ClientID, int TargetID)
{
	CNetMsg_Sv_SkinChange Msg;
	Msg.m_ClientID = ClientID;
	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p];
		Msg.m_aUseCustomColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aUseCustomColors[p];
		Msg.m_aSkinPartColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aSkinPartColors[p];
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, TargetID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Msg.AddInt(ParaI2);
	Msg.AddInt(ParaI3);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendChatCommand(const CCommandManager::CCommand *pCommand, int ClientID)
{
	CNetMsg_Sv_CommandInfo Msg;
	Msg.m_Name = pCommand->m_aName;
	Msg.m_HelpText = pCommand->m_aHelpText;
	Msg.m_ArgsFormat = pCommand->m_aArgsFormat;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendChatCommands(int ClientID)
{
	for(int i = 0; i < CommandManager()->CommandCount(); i++)
	{
		SendChatCommand(CommandManager()->GetCommand(i), ClientID);
	}
}

void CGameContext::SendRemoveChatCommand(const CCommandManager::CCommand *pCommand, int ClientID)
{
	CNetMsg_Sv_CommandInfoRemove Msg;
	Msg.m_Name = pCommand->m_aName;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_CHOICE_PASS;
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = VOTE_CHOICE_PASS;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq() * VOTE_TIME;
	m_VoteCancelTime = time_get() + time_freq() * VOTE_CANCEL_TIME;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(m_VoteType, -1);
	m_VoteUpdate = true;
}

void CGameContext::EndVote(int Type, bool Force)
{
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	if(Force)
		m_VoteCreator = -1;
	SendVoteSet(Type, -1);
}

void CGameContext::SendForceVote(int Type, const char *pDescription, const char *pReason)
{
	CNetMsg_Sv_VoteSet Msg;
	Msg.m_Type = Type;
	Msg.m_Timeout = 0;
	Msg.m_ClientID = -1;
	Msg.m_pDescription = pDescription;
	Msg.m_pReason = pReason;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendVoteSet(int Type, int ToClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_ClientID = m_VoteCreator;
		Msg.m_Type = Type;
		Msg.m_Timeout = (m_VoteCloseTime - time_get()) / time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Type = Type;
		Msg.m_Timeout = 0;
		Msg.m_ClientID = m_VoteCreator;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ToClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes + No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteClearOptions(int ClientID)
{
	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteOptions(int ClientID)
{
	CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
	while(pCurrent)
	{
		// count options for actual packet
		int NumOptions = 0;
		for(CVoteOptionServer *p = pCurrent; p && NumOptions < MAX_VOTE_OPTION_ADD; p = p->m_pNext, ++NumOptions)
			;

		// pack and send vote list packet
		CMsgPacker Msg(NETMSGTYPE_SV_VOTEOPTIONLISTADD);
		Msg.AddInt(NumOptions);
		while(pCurrent && NumOptions--)
		{
			Msg.AddString(pCurrent->m_aDescription, VOTE_DESC_LENGTH);
			pCurrent = pCurrent->m_pNext;
		}
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *) &m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning) / sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendReadyToEnter(CPlayer *pPlayer)
{
	pPlayer->m_IsReadyToEnter = true;
	CNetMsg_Sv_ReadyToEnter m;
	Server()->SendPackMsg(&m, MSGFLAG_VITAL | MSGFLAG_FLUSH, pPlayer->GetCID());
}

void CGameContext::AbortVoteOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && (str_startswith(m_aVoteCommand, "kick ") || str_startswith(m_aVoteCommand, "set_team ") || (str_startswith(m_aVoteCommand, "ban ") && Server()->IsBanned(ClientID))))
		m_VoteCloseTime = -1;
}

void CGameContext::AbortVoteOnTeamChange(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && str_startswith(m_aVoteCommand, "set_team "))
		m_VoteCloseTime = -1;
}

void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(str_comp(m_pController->GetGameType(), "DM") == 0 ||
		str_comp(m_pController->GetGameType(), "TDM") == 0 ||
		str_comp(m_pController->GetGameType(), "CTF") == 0 ||
		str_comp(m_pController->GetGameType(), "LMS") == 0 ||
		str_comp(m_pController->GetGameType(), "LTS") == 0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::OnTick()
{
	// check tuning
	CheckPureTuning();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	// if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
			EndVote(VOTE_END_ABORT, false);
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_PLAYERS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_PLAYERS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_PLAYERS] = {0};
				for(int i = 0; i < MAX_PLAYERS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i]) // don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i + 1; j < MAX_PLAYERS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}
			}

			if(m_VoteEnforce == VOTE_CHOICE_YES || (m_VoteUpdate && Yes >= Total / 2 + 1))
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				if(m_VoteCreator != -1 && m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCallTick = 0;

				EndVote(VOTE_END_PASS, m_VoteEnforce == VOTE_CHOICE_YES);
			}
			else if(m_VoteEnforce == VOTE_CHOICE_NO || (m_VoteUpdate && No >= (Total + 1) / 2) || time_get() > m_VoteCloseTime)
				EndVote(VOTE_END_FAIL, m_VoteEnforce == VOTE_CHOICE_NO);
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

#ifdef CONF_DEBUG
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->IsDummy())
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i & 1) ? -1 : 1;
			m_apPlayers[i]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	int NumFailures = m_NetObjHandler.NumObjFailures();
	if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
	{
		if(Config()->m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT failed on '%s'", m_NetObjHandler.FailedObjOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
	}
	else
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *) pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
	{
		int NumFailures = m_NetObjHandler.NumObjFailures();
		if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
		{
			if(Config()->m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT corrected on '%s'", m_NetObjHandler.FailedObjOn());
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
		else
			m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *) pInput);
	}
}

void CGameContext::OnClientEnter(int ClientID)
{
	// send chat commands
	SendChatCommands(ClientID);

	m_pController->OnPlayerConnect(m_apPlayers[ClientID]);

	m_VoteUpdate = true;

	// update client infos (others before local)
	CNetMsg_Sv_ClientInfo NewClientInfoMsg;
	NewClientInfoMsg.m_ClientID = ClientID;
	NewClientInfoMsg.m_Local = 0;
	NewClientInfoMsg.m_Team = m_apPlayers[ClientID]->GetTeam();
	NewClientInfoMsg.m_pName = Server()->ClientName(ClientID);
	NewClientInfoMsg.m_pClan = Server()->ClientClan(ClientID);
	NewClientInfoMsg.m_Country = Server()->ClientCountry(ClientID);
	NewClientInfoMsg.m_Silent = false;

	if(Config()->m_SvSilentSpectatorMode && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		NewClientInfoMsg.m_Silent = true;

	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		NewClientInfoMsg.m_apSkinPartNames[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p];
		NewClientInfoMsg.m_aUseCustomColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aUseCustomColors[p];
		NewClientInfoMsg.m_aSkinPartColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aSkinPartColors[p];
	}

	for(int i = 0; i < MAX_PLAYERS; ++i)
	{
		if(i == ClientID || !m_apPlayers[i] || (!Server()->ClientIngame(i) && !m_apPlayers[i]->IsDummy()))
			continue;

		// new info for others
		if(Server()->ClientIngame(i))
			Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);

		// existing infos for new player
		CNetMsg_Sv_ClientInfo ClientInfoMsg;
		ClientInfoMsg.m_ClientID = i;
		ClientInfoMsg.m_Local = 0;
		ClientInfoMsg.m_Team = m_apPlayers[i]->GetTeam();
		ClientInfoMsg.m_pName = Server()->ClientName(i);
		ClientInfoMsg.m_pClan = Server()->ClientClan(i);
		ClientInfoMsg.m_Country = Server()->ClientCountry(i);
		ClientInfoMsg.m_Silent = true;
		for(int p = 0; p < NUM_SKINPARTS; p++)
		{
			ClientInfoMsg.m_apSkinPartNames[p] = m_apPlayers[i]->m_TeeInfos.m_aaSkinPartNames[p];
			ClientInfoMsg.m_aUseCustomColors[p] = m_apPlayers[i]->m_TeeInfos.m_aUseCustomColors[p];
			ClientInfoMsg.m_aSkinPartColors[p] = m_apPlayers[i]->m_TeeInfos.m_aSkinPartColors[p];
		}
		Server()->SendPackMsg(&ClientInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);
	}

	// local info
	NewClientInfoMsg.m_Local = 1;
	Server()->SendPackMsg(&NewClientInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, ClientID);

	if(Server()->DemoRecorder_IsRecording())
	{
		CNetMsg_De_ClientEnter Msg;
		Msg.m_pName = NewClientInfoMsg.m_pName;
		Msg.m_ClientID = ClientID;
		Msg.m_Team = NewClientInfoMsg.m_Team;
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	}

	Server()->ExpireServerInfo();
}

void CGameContext::OnClientConnected(int ClientID, bool Dummy, bool AsSpec)
{
	dbg_assert(!m_apPlayers[ClientID], "non-free player slot");

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, Dummy, AsSpec);

	if(Dummy)
		return;

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(m_VoteType, ClientID);

	// send motd
	SendMotd(ClientID);

	// send settings
	SendSettings(ClientID);
}

void CGameContext::OnClientTeamChange(int ClientID)
{
	if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		AbortVoteOnTeamChange(ClientID);

	// mark client's projectile has team projectile
	CProjectile *p = (CProjectile *) m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE);
	for(; p; p = (CProjectile *) p->TypeNext())
	{
		if(p->GetOwner() == ClientID)
			p->LoseOwner();
	}
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	AbortVoteOnDisconnect(ClientID);
	m_pController->OnPlayerDisconnect(m_apPlayers[ClientID]);

	// update clients on drop
	if(Server()->ClientIngame(ClientID) || IsClientBot(ClientID))
	{
		if(Server()->DemoRecorder_IsRecording())
		{
			CNetMsg_De_ClientLeave Msg;
			Msg.m_ClientID = ClientID;
			Msg.m_pName = Server()->ClientName(ClientID);
			Msg.m_pReason = pReason;
			Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
		}

		CNetMsg_Sv_ClientDrop Msg;
		Msg.m_ClientID = ClientID;
		Msg.m_pReason = pReason;
		Msg.m_Silent = false;
		if(Config()->m_SvSilentSpectatorMode && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
			Msg.m_Silent = true;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);
	}

	// mark client's projectile has team projectile
	CProjectile *p = (CProjectile *) m_World.FindFirst(CGameWorld::ENTTYPE_PROJECTILE);
	for(; p; p = (CProjectile *) p->TypeNext())
	{
		if(p->GetOwner() == ClientID)
			p->LoseOwner();
	}

	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	m_VoteUpdate = true;

	Server()->ExpireServerInfo();
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(Config()->m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(Config()->m_SvSpamprotection && pPlayer->m_LastChatTeamTick && pPlayer->m_LastChatTeamTick + Server()->TickSpeed() > Server()->Tick())
				return;

			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *) pRawMsg;

			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(!str_utf8_is_whitespace(Code))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 127)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 20 characters per second)
			if(Length == 0 || (Config()->m_SvSpamprotection && pPlayer->m_LastChatTeamTick && pPlayer->m_LastChatTeamTick + Server()->TickSpeed() * (Length / 20) > Server()->Tick()))
				return;

			// call command when chat, this is for ddnet players (18.4+, not include 18.4)
			if(pMsg->m_pMessage[0] == '/')
			{
				char aCommand[16];
				str_format(aCommand, sizeof(aCommand), "%.*s", str_span(pMsg->m_pMessage + 1, " "), pMsg->m_pMessage + 1);

				if(CommandManager()->OnCommand(aCommand, str_skip_whitespaces_const(str_skip_to_whitespace_const(pMsg->m_pMessage)), ClientID))
					SendChat(-1, CHAT_NONE, ClientID, "No such command");
				return;
			}

			pPlayer->m_LastChatTeamTick = Server()->Tick();

			int Mode = pMsg->m_Mode;
			if(Mode != CHAT_NONE)
				SendChat(ClientID, Mode, pMsg->m_Target, pMsg->m_pMessage);
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *) pRawMsg;
			int64_t Now = Server()->Tick();

			if(pMsg->m_Force)
			{
				if(!Server()->IsAuthed(ClientID))
					return;
			}
			else
			{
				if((Config()->m_SvSpamprotection && ((pPlayer->m_LastVoteTryTick && pPlayer->m_LastVoteTryTick + Server()->TickSpeed() * 3 > Now) ||
									    (pPlayer->m_LastVoteCallTick && pPlayer->m_LastVoteCallTick + Server()->TickSpeed() * VOTE_COOLDOWN > Now))) ||
					pPlayer->GetTeam() == TEAM_SPECTATORS || m_VoteCloseTime)
					return;

				pPlayer->m_LastVoteTryTick = Now;
			}

			m_VoteType = VOTE_UNKNOWN;
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

			if(str_comp_nocase(pMsg->m_Type, "option") == 0)
			{
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						char aBuf[128];
						str_format(aBuf, sizeof(aBuf),
							"'%d:%s' voted %s '%s' reason='%s' cmd='%s' force=%d",
							ClientID, Server()->ClientName(ClientID), pMsg->m_Type,
							aDesc, pReason, aCmd, pMsg->m_Force);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
						if(pMsg->m_Force)
						{
							Server()->SetRconCID(ClientID);
							Console()->ExecuteLine(aCmd);
							Server()->SetRconCID(IServer::RCON_CID_SERV);
							SendForceVote(VOTE_START_OP, aDesc, pReason);
							return;
						}
						m_VoteType = VOTE_START_OP;
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
					return;
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				if(!Config()->m_SvVoteKick || m_pController->GetRealPlayerNum() < Config()->m_SvVoteKickMin)
					return;

				int KickID = str_toint(pMsg->m_Value);
				if(KickID < 0 || KickID >= MAX_PLAYERS || !m_apPlayers[KickID] || KickID == ClientID || Server()->IsAuthed(KickID))
					return;

				str_format(aDesc, sizeof(aDesc), "%2d: %s", KickID, Server()->ClientName(KickID));
				if(!Config()->m_SvVoteKickBantime)
					str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, Config()->m_SvVoteKickBantime);
				}
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf),
					"'%d:%s' voted %s '%d:%s' reason='%s' cmd='%s' force=%d",
					ClientID, Server()->ClientName(ClientID), pMsg->m_Type,
					KickID, Server()->ClientName(KickID), pReason, aCmd, pMsg->m_Force);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				if(pMsg->m_Force)
				{
					Server()->SetRconCID(ClientID);
					Console()->ExecuteLine(aCmd);
					Server()->SetRconCID(IServer::RCON_CID_SERV);
					return;
				}
				m_VoteType = VOTE_START_KICK;
				m_VoteClientID = KickID;
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!Config()->m_SvVoteSpectate)
					return;

				int SpectateID = str_toint(pMsg->m_Value);
				if(SpectateID < 0 || SpectateID >= MAX_PLAYERS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS || SpectateID == ClientID)
					return;

				str_format(aDesc, sizeof(aDesc), "%2d: %s", SpectateID, Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, Config()->m_SvVoteSpectateRejoindelay);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf),
					"'%d:%s' voted %s '%d:%s' reason='%s' cmd='%s' force=%d",
					ClientID, Server()->ClientName(ClientID), pMsg->m_Type,
					SpectateID, Server()->ClientName(SpectateID), pReason, aCmd, pMsg->m_Force);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				if(pMsg->m_Force)
				{
					Server()->SetRconCID(ClientID);
					Console()->ExecuteLine(aCmd);
					Server()->SetRconCID(IServer::RCON_CID_SERV);
					SendForceVote(VOTE_START_SPEC, aDesc, pReason);
					return;
				}
				m_VoteType = VOTE_START_SPEC;
				m_VoteClientID = SpectateID;
			}

			if(m_VoteType != VOTE_UNKNOWN)
			{
				m_VoteCreator = ClientID;
				StartVote(aDesc, aCmd, pReason);
				pPlayer->m_Vote = VOTE_CHOICE_YES;
				pPlayer->m_VotePos = m_VotePos = 1;
				pPlayer->m_LastVoteCallTick = Now;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(!m_VoteCloseTime)
				return;

			if(pPlayer->m_Vote == VOTE_CHOICE_PASS)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *) pRawMsg;
				if(pMsg->m_Vote == VOTE_CHOICE_PASS)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
			else if(m_VoteCreator == pPlayer->GetCID())
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *) pRawMsg;
				if(pMsg->m_Vote != VOTE_CHOICE_NO || m_VoteCancelTime < time_get())
					return;

				m_VoteCloseTime = -1;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SETTEAM)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *) pRawMsg;

			if(pPlayer->GetTeam() == pMsg->m_Team ||
				(Config()->m_SvSpamprotection && pPlayer->m_LastSetTeamTick && pPlayer->m_LastSetTeamTick + Server()->TickSpeed() * 3 > Server()->Tick()) ||
				pPlayer->m_TeamChangeTick > Server()->Tick())
				return;

			pPlayer->m_LastSetTeamTick = Server()->Tick();

			// Switch team on given client and kill/respawn him
			if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
			{
				if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
					m_VoteUpdate = true;
				pPlayer->m_TeamChangeTick = Server()->Tick() + Server()->TickSpeed() * 3;
				m_pController->DoTeamChange(pPlayer, pMsg->m_Team);
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *) pRawMsg;

			if(Config()->m_SvSpamprotection && pPlayer->m_LastSetSpectatorModeTick && pPlayer->m_LastSetSpectatorModeTick + Server()->TickSpeed() > Server()->Tick())
				return;

			pPlayer->m_LastSetSpectatorModeTick = Server()->Tick();
			if(!pPlayer->SetSpectatorID(pMsg->m_SpecMode, pMsg->m_SpectatorID))
				SendGameMsg(GAMEMSG_SPEC_INVALID_ID, ClientID);
		}
		else if(MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *) pRawMsg;

			if(Config()->m_SvSpamprotection && pPlayer->m_LastEmoteTick && pPlayer->m_LastEmoteTick + Server()->TickSpeed() * 3 > Server()->Tick())
				return;

			pPlayer->m_LastEmoteTick = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
		}
		else if(MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if(pPlayer->m_LastKillTick && pPlayer->m_LastKillTick + Server()->TickSpeed() * 3 > Server()->Tick())
				return;

			pPlayer->m_LastKillTick = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
		else if(MsgID == NETMSGTYPE_CL_READYCHANGE)
		{
			if(pPlayer->m_LastReadyChangeTick && pPlayer->m_LastReadyChangeTick + Server()->TickSpeed() * 1 > Server()->Tick())
				return;

			pPlayer->m_LastReadyChangeTick = Server()->Tick();
			m_pController->OnPlayerReadyChange(pPlayer);
		}
		else if(MsgID == NETMSGTYPE_CL_SKINCHANGE)
		{
			if(pPlayer->m_LastChangeInfoTick && pPlayer->m_LastChangeInfoTick + Server()->TickSpeed() * 5 > Server()->Tick())
				return;

			pPlayer->m_LastChangeInfoTick = Server()->Tick();
			CNetMsg_Cl_SkinChange *pMsg = (CNetMsg_Cl_SkinChange *) pRawMsg;

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			// update all clients
			for(int i = 0; i < MAX_PLAYERS; ++i)
			{
				if(!m_apPlayers[i] || (!Server()->ClientIngame(i) && !m_apPlayers[i]->IsDummy()) || Server()->GetClientVersion(i) < MIN_SKINCHANGE_CLIENTVERSION)
					continue;

				SendSkinChange(pPlayer->GetCID(), i);
			}

			Server()->ExpireServerInfo();
			m_pController->OnPlayerInfoChange(pPlayer);
		}
		else if(MsgID == NETMSGTYPE_CL_COMMAND)
		{
			CNetMsg_Cl_Command *pMsg = (CNetMsg_Cl_Command *) pRawMsg;
			CommandManager()->OnCommand(pMsg->m_Name, pMsg->m_Arguments, ClientID);
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReadyToEnter)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *) pRawMsg;
			pPlayer->m_LastChangeInfoTick = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			m_pController->OnPlayerInfoChange(pPlayer);

			SendVoteClearOptions(ClientID);
			SendVoteOptions(ClientID);
			SendTuningParams(ClientID);
			SendReadyToEnter(pPlayer);

			Server()->ExpireServerInfo();
		}
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	const char *pParamName = pResult->GetString(0);

	char aBuf[256];
	if(pResult->NumArguments() == 2)
	{
		float NewValue = pResult->GetFloat(1);
		if(pSelf->Tuning()->Set(pParamName, NewValue) && pSelf->Tuning()->Get(pParamName, &NewValue))
		{
			str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
			pSelf->SendTuningParams(-1);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		}
	}
	else
	{
		float Value;
		if(pSelf->Tuning()->Get(pParamName, &Value))
		{
			str_format(aBuf, sizeof(aBuf), "%s %.2f", pParamName, Value);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		}
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	CTuningParams TuningParams;

	if(pResult->NumArguments())
	{
		const char *pParamName = pResult->GetString(0);
		float DefaultValue = 0.0f;
		char aBuf[256];
		if(TuningParams.Get(pParamName, &DefaultValue) && pSelf->Tuning()->Set(pParamName, DefaultValue))
		{
			str_format(aBuf, sizeof(aBuf), "%s reset to %.2f", pParamName, DefaultValue);
			pSelf->SendTuningParams(-1);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
	else
	{
		*pSelf->Tuning() = TuningParams;
		pSelf->SendTuningParams(-1);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
	}
}

void CGameContext::ConTunes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float Value;
		pSelf->Tuning()->Get(i, &Value);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->GetName(i), Value);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	pSelf->SendChat(-1, CHAT_ALL, -1, pResult->GetString(0));
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int) MAX_PLAYERS - 1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID] || !pSelf->m_pController->CanChangeTeam(pSelf->m_apPlayers[ClientID], Team))
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick() + pSelf->Server()->TickSpeed() * Delay * 60;
	pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[ClientID], Team);
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	pSelf->SendGameMsg(GAMEMSG_TEAM_ALL, Team, -1);

	for(int i = 0; i < MAX_PLAYERS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_pController->CanChangeTeam(pSelf->m_apPlayers[i], Team))
			pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[i], Team, false);
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	pDescription = str_skip_whitespaces_const(pDescription);
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	for(CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	CVoteOptionServer *pOption = (CVoteOptionServer *) pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len + 1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *) pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len + 1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	pSelf->SendVoteClearOptions(-1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = VOTE_CHOICE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = VOTE_CHOICE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *) pUserData;
		pSelf->SendMotd(-1);
	}
}

void CGameContext::ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *) pUserData;
		pSelf->SendSettings(-1);
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("tune", "s[tuning] ?i[value]", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value or show current value");
	Console()->Register("tune_reset", "?s[tuning]", CFGFLAG_SERVER, ConTuneReset, this, "Reset all or one tuning variable to default");
	Console()->Register("tunes", "", CFGFLAG_SERVER, ConTunes, this, "List all tuning variables and their values");

	Console()->Register("say", "r[text]", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("broadcast", "r[text]", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("set_team", "i[id] i[team] ?i[delay]", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i[team]", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");

	Console()->Register("add_vote", "s[option] r[command]", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s[option]", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r['yes'|'no']", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");
}

void CGameContext::NewCommandHook(const CCommandManager::CCommand *pCommand, void *pContext)
{
	CGameContext *pSelf = (CGameContext *) pContext;
	pSelf->SendChatCommand(pCommand, -1);
}

void CGameContext::RemoveCommandHook(const CCommandManager::CCommand *pCommand, void *pContext)
{
	CGameContext *pSelf = (CGameContext *) pContext;
	pSelf->SendRemoveChatCommand(pCommand, -1);
}

void CGameContext::OnInit()
{
	// init everything
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	m_CommandManager.Init(m_pConsole, this, NewCommandHook, RemoveCommandHook);

	// HACK: only set static size for items, which were available in the first 0.7 release
	// so new items don't break the snapshot delta
	static const int OLD_NUM_NETOBJTYPES = 23;
	for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	m_pController = new CGameController(this);
	m_pController->RegisterChatCommands(CommandManager());

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *) Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);
	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y * pTileMap->m_Width + x].m_Index;

			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				m_pController->OnEntity(Index - ENTITY_OFFSET, Pos);
			}
		}
	}

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	Console()->Chain("sv_vote_kick", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_kick_min", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_spectate", ConchainSettingUpdate, this);
	Console()->Chain("sv_player_slots", ConchainSettingUpdate, this);
	Console()->Chain("sv_max_clients", ConchainSettingUpdate, this);

	// clamp sv_player_slots to 0..MaxClients
	if(Config()->m_SvMaxClients < Config()->m_SvPlayerSlots)
		Config()->m_SvPlayerSlots = Config()->m_SvMaxClients;

#ifdef CONF_DEBUG
	// clamp dbg_dummies to 0..MAX_PLAYERS-1
	if(MAX_PLAYERS <= Config()->m_DbgDummies)
		Config()->m_DbgDummies = MAX_PLAYERS;
	if(Config()->m_DbgDummies)
	{
		for(int i = 0; i < Config()->m_DbgDummies; i++)
			OnClientConnected(MAX_PLAYERS - i - 1, true, false);
	}
#endif
}

void CGameContext::OnShutdown()
{
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CNetObj_De_TuneParams *pTuneParams = static_cast<CNetObj_De_TuneParams *>(Server()->SnapNewItem(NETOBJTYPE_DE_TUNEPARAMS, 0, sizeof(CNetObj_De_TuneParams)));
		if(!pTuneParams)
			return;

		mem_copy(pTuneParams->m_aTuneParams, &m_Tuning, sizeof(pTuneParams->m_aTuneParams));
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_World.PostSnap();
	m_Events.Clear();
}

bool CGameContext::IsClientBot(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->IsDummy();
}

bool CGameContext::IsClientReady(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReadyToEnter;
}

bool CGameContext::IsClientPlayer(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() != TEAM_SPECTATORS;
}

bool CGameContext::IsClientSpectator(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS;
}

const char *CGameContext::GameType() const { return m_pController && m_pController->GetGameType() ? m_pController->GetGameType() : ""; }
const char *CGameContext::Version() const { return GAME_VERSION; }
const char *CGameContext::NetVersion() const { return GAME_NETVERSION; }
const char *CGameContext::NetVersionHashUsed() const { return GAME_NETVERSION_HASH_FORCED; }
const char *CGameContext::NetVersionHashReal() const { return GAME_NETVERSION_HASH; }

IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::OnUpdatePlayerServerInfo(CJsonStringWriter *pJSonWriter, int Id)
{
	if(!m_apPlayers[Id])
		return;

	STeeInfo &TeeInfo = m_apPlayers[Id]->m_TeeInfos;

	pJSonWriter->WriteAttribute("skin");
	pJSonWriter->BeginObject();

	const char *apPartNames[NUM_SKINPARTS] = {"body", "marking", "decoration", "hands", "feet", "eyes"};

	for(int i = 0; i < NUM_SKINPARTS; ++i)
	{
		pJSonWriter->WriteAttribute(apPartNames[i]);
		pJSonWriter->BeginObject();

		pJSonWriter->WriteAttribute("name");
		pJSonWriter->WriteStrValue(TeeInfo.m_aaSkinPartNames[i]);

		if(TeeInfo.m_aUseCustomColors[i])
		{
			pJSonWriter->WriteAttribute("color");
			pJSonWriter->WriteIntValue(TeeInfo.m_aSkinPartColors[i]);
		}

		pJSonWriter->EndObject();
	}

	pJSonWriter->EndObject();

	pJSonWriter->WriteAttribute("afk");
	pJSonWriter->WriteBoolValue(false);

	const int Team = m_apPlayers[Id]->GetTeam() == TEAM_SPECTATORS ? -1 : 0;

	pJSonWriter->WriteAttribute("team");
	pJSonWriter->WriteIntValue(Team);
}

int NetworkClipped(int SnappingClient, vec2 CheckPos, CGameContext *pGameServer)
{
	if(SnappingClient == -1)
		return 0;

	float dx = pGameServer->m_apPlayers[SnappingClient]->m_ViewPos.x - CheckPos.x;
	float dy = pGameServer->m_apPlayers[SnappingClient]->m_ViewPos.y - CheckPos.y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return 1;

	if(distance(pGameServer->m_apPlayers[SnappingClient]->m_ViewPos, CheckPos) > 1100.0f)
		return 1;

	return 0;
}
