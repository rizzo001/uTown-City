/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you miss that file, contact Pikotee, because he changed some stuff here ...			 */
/*	... and would like to be mentioned in credits in case of using his code					 */

#include <engine/shared/config.h>
#include <game/mapitems.h>

#include <game/generated/protocol.h>

#include "entities/pickup.h"
#include "gamecontroller.h"
#include "gamecontext.h"

#include "city/entities/door.h"
#include "city/entities/trigger.h"
#include "city/entities/teleport.h"
#include "city/entities/carousel.h"
#include "city/shopitems/vip.h"
#include "city/shopitems/health.h"
#include "city/home.h"

IGameController::IGameController(class CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();
	m_pGameType = "unknown";

	//
	DoWarmup(g_Config.m_SvWarmup);
	m_GameOverTick = -1;
	m_SuddenDeath = 0;
	m_RoundStartTick = Server()->Tick();
	m_RoundCount = 0;
	m_GameFlags = 0;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;
	m_aMapWish[0] = 0;

	m_UnbalancedTick = -1;
	m_ForceBalanced = false;

	m_aNumSpawnPoints[0] = 0;
	m_aNumSpawnPoints[1] = 0;
	m_aNumSpawnPoints[2] = 0;
}

IGameController::~IGameController()
{
}

float IGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos)
{
	float Score = 0.0f;
	CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
	for(; pC; pC = (CCharacter *)pC->TypeNext())
	{
		// team mates are not as dangerous as enemies
		float Scoremod = 1.0f;
		if(pEval->m_FriendlyTeam != -1 && pC->GetPlayer()->GetTeam() == pEval->m_FriendlyTeam)
			Scoremod = 0.5f;

		float d = distance(Pos, pC->m_Pos);
		Score += Scoremod * (d == 0 ? 1000000000.0f : 1.0f/d);
	}

	return Score;
}

void IGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type)
{
	// get spawn point
	for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
	{
		// check if the position is occupado
		CCharacter *aEnts[MAX_CLIENTS];
		int Num = GameServer()->m_World.FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		vec2 Positions[5] = { vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f) };	// start, left, up, right, down
		int Result = -1;
		for(int Index = 0; Index < 5 && Result == -1; ++Index)
		{
			Result = Index;
			for(int c = 0; c < Num; ++c)
				if(GameServer()->Collision()->CheckPoint(m_aaSpawnPoints[Type][i]+Positions[Index]) ||
					distance(aEnts[c]->m_Pos, m_aaSpawnPoints[Type][i]+Positions[Index]) <= aEnts[c]->m_ProximityRadius)
				{
					Result = -1;
					break;
				}
		}
		if(Result == -1)
			continue;	// try next spawn point

		vec2 P = m_aaSpawnPoints[Type][i]+Positions[Result];
		float S = EvaluateSpawnPos(pEval, P);
		if(!pEval->m_Got || pEval->m_Score > S)
		{
			pEval->m_Got = true;
			pEval->m_Score = S;
			pEval->m_Pos = P;
		}
	}
}

bool IGameController::CanSpawn(int Team, vec2 *pOutPos, int Jailed)
{
	CSpawnEval Eval;

	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;

	if(IsTeamplay())
	{
		Eval.m_FriendlyTeam = Team;

		// first try own team spawn, then normal spawn and then enemy
		EvaluateSpawnType(&Eval, 1+(Team&1));
		if(!Eval.m_Got)
		{
			EvaluateSpawnType(&Eval, 0);
			if(!Eval.m_Got)
				EvaluateSpawnType(&Eval, 1+((Team+1)&1));
		}
	}
	else
	{
		if(Jailed == 1)
			EvaluateSpawnType(&Eval, 1);
		else if(Jailed == 2)
		EvaluateSpawnType(&Eval, 2);
				else
		EvaluateSpawnType(&Eval, 0);
		


		// City
		//EvaluateSpawnType(&Eval, 3);
	}

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}


bool IGameController::OnEntity(int Index, vec2 Pos)
{
	int Type = -1;
	int SubType = 0;

	if(Index == ENTITY_SPAWN)
		m_aaSpawnPoints[0][m_aNumSpawnPoints[0]++] = Pos;
	else if(Index == ENTITY_JAIL)
		m_aaSpawnPoints[1][m_aNumSpawnPoints[1]++] = Pos;
	else if(Index == ENTITY_INSTA_SPAWN)
		m_aaSpawnPoints[2][m_aNumSpawnPoints[2]++] = Pos;
	else if(Index == ENTITY_ARMOR_1)
		Type = POWERUP_ARMOR;
	else if(Index == ENTITY_HEALTH_1)
		Type = POWERUP_HEALTH;
	else if(Index == ENTITY_WEAPON_SHOTGUN)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_SHOTGUN;
	}
	else if(Index == ENTITY_WEAPON_GRENADE)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_GRENADE;
	}
	else if(Index == ENTITY_WEAPON_RIFLE)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_RIFLE;
	}
	else if(Index == ENTITY_POWERUP_NINJA && g_Config.m_SvPowerups)
	{
		Type = POWERUP_NINJA;
		SubType = WEAPON_NINJA;
	}
		else if(Index == ENTITY_WEAPON_HAMMER)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_HAMMER;
	}
	else if(Index == ENTITY_WEAPON_GUN)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_GUN;
	}

	// City
	else if(Index == ENTITY_HOME)
		new CHome(&GameServer()->m_World, Pos);
	else if(Index == ENTITY_DOOR_ID || Index == ENTITY_DOOR_NR)
		new CDoor(&GameServer()->m_World, Pos, Index == ENTITY_DOOR_ID?true:false);
	else if(Index == ENTITY_TRIGGER_ID || Index == ENTITY_TRIGGER_NR)
		new CTrigger(&GameServer()->m_World, Pos, Index == ENTITY_TRIGGER_ID?true:false);
	else if(Index == ENTITY_FROM)
		new CTeleport(&GameServer()->m_World, Pos, true);
	else if(Index == ENTITY_TO)
	{
		new CTeleport(&GameServer()->m_World, Pos, false);
		GameServer()->m_TeleNum++;
	}
	else if(Index == ENTITY_CAROUSEL)
		new CCarousel(&GameServer()->m_World, Pos);
			else if(Index == ENTITY_FLAGSTAND_BLUE)
		new CVip(&GameServer()->m_World, Pos);
			else if(Index == ENTITY_FLAGSTAND_RED)
		new CBuyHealth(&GameServer()->m_World, Pos);
	/*else if(Index == ENTITY_JAIL)
		m_aaSpawnPoints[3][m_aNumSpawnPoints[3]++] = Pos;*/


	else
		return false;


	if(Type != -1)
	{
		CPickup *pPickup = new CPickup(&GameServer()->m_World, Type, SubType);
		pPickup->m_Pos = Pos;
	}

	return true;
}

void IGameController::EndRound()
{
	if(m_Warmup) // game can't end when we are running warmup
		return;

	GameServer()->m_World.m_Paused = true;
	m_GameOverTick = Server()->Tick();
	m_SuddenDeath = 0;
}

void IGameController::ResetGame()
{
	GameServer()->m_World.m_ResetRequested = true;
}

const char *IGameController::GetTeamName(int Team)
{
	if(IsTeamplay())
	{
		if(Team == TEAM_RED)
			return "red team";
		else if(Team == TEAM_BLUE)
			return "blue team";
	}
	else
	{
		if(Team == 0)
			return "game";
	}

	return "spectators";
}

static bool IsSeparator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

void IGameController::StartRound()
{
	ResetGame();

	m_RoundStartTick = Server()->Tick();
	m_SuddenDeath = 0;
	m_GameOverTick = -1;
	GameServer()->m_World.m_Paused = false;
	m_aTeamscore[TEAM_RED] = 0;
	m_aTeamscore[TEAM_BLUE] = 0;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags&GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void IGameController::ChangeMap(const char *pToMap)
{
	str_copy(m_aMapWish, pToMap, sizeof(m_aMapWish));
	EndRound();
}

void IGameController::CycleMap()
{
	if(m_aMapWish[0] != 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "rotating map to %s", m_aMapWish);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		str_copy(g_Config.m_SvMap, m_aMapWish, sizeof(g_Config.m_SvMap));
		m_aMapWish[0] = 0;
		m_RoundCount = 0;
		return;
	}
	if(!str_length(g_Config.m_SvMaprotation))
		return;

	if(m_RoundCount < g_Config.m_SvRoundsPerMap-1)
		return;

	// handle maprotation
	const char *pMapRotation = g_Config.m_SvMaprotation;
	const char *pCurrentMap = g_Config.m_SvMap;

	int CurrentMapLen = str_length(pCurrentMap);
	const char *pNextMap = pMapRotation;
	while(*pNextMap)
	{
		int WordLen = 0;
		while(pNextMap[WordLen] && !IsSeparator(pNextMap[WordLen]))
			WordLen++;

		if(WordLen == CurrentMapLen && str_comp_num(pNextMap, pCurrentMap, CurrentMapLen) == 0)
		{
			// map found
			pNextMap += CurrentMapLen;
			while(*pNextMap && IsSeparator(*pNextMap))
				pNextMap++;

			break;
		}

		pNextMap++;
	}

	// restart rotation
	if(pNextMap[0] == 0)
		pNextMap = pMapRotation;

	// cut out the next map
	char aBuf[512];
	for(int i = 0; i < 512; i++)
	{
		aBuf[i] = pNextMap[i];
		if(IsSeparator(pNextMap[i]) || pNextMap[i] == 0)
		{
			aBuf[i] = 0;
			break;
		}
	}

	// skip spaces
	int i = 0;
	while(IsSeparator(aBuf[i]))
		i++;

	m_RoundCount = 0;

	char aBufMsg[256];
	str_format(aBufMsg, sizeof(aBufMsg), "rotating map to %s", &aBuf[i]);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	str_copy(g_Config.m_SvMap, &aBuf[i], sizeof(g_Config.m_SvMap));
}

void IGameController::PostReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i])
		{
			GameServer()->m_apPlayers[i]->Respawn();
			GameServer()->m_apPlayers[i]->m_Score = 0;
			GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
			GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
		}
	}
}

void IGameController::OnPlayerInfoChange(class CPlayer *pP)
{
	const int aTeamColors[2] = {65387, 10223467};
	if(IsTeamplay())
	{
		pP->m_TeeInfos.m_UseCustomColor = 1;
		if(pP->GetTeam() >= TEAM_RED && pP->GetTeam() <= TEAM_BLUE)
		{
			pP->m_TeeInfos.m_ColorBody = aTeamColors[pP->GetTeam()];
			pP->m_TeeInfos.m_ColorFeet = aTeamColors[pP->GetTeam()];
		}
		else
		{
			pP->m_TeeInfos.m_ColorBody = 12895054;
			pP->m_TeeInfos.m_ColorFeet = 12895054;
		}
	}
}


int IGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	// do scoreing
	if(!pKiller || Weapon == WEAPON_GAME)
		return 0;
	if(pKiller == pVictim->GetPlayer())
		{
		if(pVictim->GetPlayer()->m_AccData.m_Money >= 50)
			pVictim->GetPlayer()->m_AccData.m_Money -= 50;

		//pVictim->GetPlayer()->m_Score--; // suicide
	}
	else
	{
		if(IsTeamplay() && pVictim->GetPlayer()->GetTeam() == pKiller->GetTeam())
			pKiller->m_Score--; // teamkill
		else
		{
			if(!pKiller->m_Insta)
			{
				char aKillmsg[50];
						pKiller->m_AccData.m_Money += 500;
			pKiller->m_AccData.m_ExpPoints += 1;
			pKiller->m_Score = pKiller->m_AccData.m_Level;
			if(pKiller->m_pAccount->GetPlayerExp() >= pKiller->m_pAccount->GetPlayerLevel()*3)
			{
				str_format(aKillmsg, sizeof(aKillmsg), "Player level up! || Now you Level:%i",pKiller->m_AccData.m_Level);
					pKiller->GetCharacter()->GameServer()->SendChatTarget(pKiller->GetCID(), aKillmsg);
				pKiller->m_AccData.m_ExpPoints = 0;
				pKiller->m_AccData.m_Level++;
			}
			else
			{
				str_format(aKillmsg, sizeof(aKillmsg), "+1 Exp || current:%i",pKiller->m_AccData.m_ExpPoints);
					pKiller->GetCharacter()->GameServer()->SendChatTarget(pKiller->GetCID(), aKillmsg);
			}
			char aBuf[256];
			str_format(aKillmsg, sizeof(aKillmsg), "+500 TC || current %i TC",pKiller->m_AccData.m_Money);
				pKiller->GetCharacter()->GameServer()->SendChatTarget(pKiller->GetCID(), aKillmsg);
			pKiller->m_pAccount->PlayerLevelUp();
			str_format(aBuf, sizeof(aBuf), "Level: %i || Exp: %i",pKiller->m_AccData.m_Level, pKiller->m_AccData.m_ExpPoints);
				GameServer()->SendBroadcast(aBuf, pKiller->GetCID());
			}
			else
			{
				char aKillmsg[200];
				char aBuf[50];
				pKiller->m_AccData.m_ExpPoints += 1;
				pKiller->GetCharacter()->m_InstaKills++;
				pKiller->m_AccData.m_Money += 1000+(pKiller->GetCharacter()->m_InstaKills*100);
				if(pKiller->m_pAccount->GetPlayerExp() >= pKiller->m_pAccount->GetPlayerLevel()*3)
				{
					pKiller->m_AccData.m_Level++;
					str_format(aKillmsg, sizeof(aKillmsg), "Player level up! || Now you Level:%i",pKiller->m_AccData.m_Level);
					pKiller->GetCharacter()->GameServer()->SendChatTarget(pKiller->GetCID(), aKillmsg);
					pKiller->m_AccData.m_ExpPoints = 0;
				}
				else
				{
					pKiller->m_AccData.m_ExpPoints += 2;
					str_format(aKillmsg, sizeof(aKillmsg), "+2 Exp || Current: %i Exp || %i Insta-Kills",pKiller->m_AccData.m_ExpPoints,pKiller->GetCharacter()->m_InstaKills);
						pKiller->GetCharacter()->GameServer()->SendChatTarget(pKiller->GetCID(), aKillmsg);
					str_format(aKillmsg, sizeof(aKillmsg), "+%i TC || Current: %i TC",pKiller->GetCharacter()->m_InstaKills*100+1000,pKiller->m_AccData.m_Money,pKiller->GetCharacter()->m_InstaKills);
						pKiller->GetCharacter()->GameServer()->SendChatTarget(pKiller->GetCID(), aKillmsg);
					str_format(aBuf, sizeof(aBuf), "Level: %i || Exp: %i",pKiller->m_AccData.m_Level, pKiller->m_AccData.m_ExpPoints);
						GameServer()->SendBroadcast(aBuf, pKiller->GetCID());
				}

				if(pVictim->m_InstaKills >= 5)
				{
					str_format(aBuf, sizeof(aBuf), "%s killed %s with %i kills", Server()->ClientName(pKiller->GetCID()), Server()->ClientName(pVictim->GetPlayer()->GetCID()),pVictim->m_InstaKills);
					GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
				}

				//Kill msg 4 all <3
				if(!(pKiller->GetCharacter()->m_InstaKills%5) && pKiller->GetCharacter()->m_InstaKills <= 50)
				{
					if(pKiller->GetCharacter()->m_InstaKills == 5)
						str_format(aBuf, sizeof(aBuf), "%s is on a killing spree with 5 kills", Server()->ClientName(pKiller->GetCID()));
					else if(pKiller->GetCharacter()->m_InstaKills == 10)
						str_format(aBuf, sizeof(aBuf), "%s is unstoppable with 10 kills", Server()->ClientName(pKiller->GetCID()));
					else if(pKiller->GetCharacter()->m_InstaKills >= 15 && pKiller->GetCharacter()->m_InstaKills <= 45)
						str_format(aBuf, sizeof(aBuf), "%s is dominating with %i kills", Server()->ClientName(pKiller->GetCID()), pKiller->GetCharacter()->m_InstaKills);
					else if(pKiller->m_AccData.m_Money,pKiller->GetCharacter()->m_InstaKills == 50)
						str_format(aBuf, sizeof(aBuf), "%s is Godlike! (50 kills)", Server()->ClientName(pKiller->GetCID()));

					GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
				}
				
			}


		}
	}
	if(Weapon == WEAPON_SELF)
		pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*3.0f;
	return 0;
}

void IGameController::OnCharacterSpawn(class CCharacter *pChr)
{
	// default health / armor
	pChr->IncreaseHealth(pChr->GetPlayer()->m_AccData.m_Health*pChr->GetPlayer()->m_AccData.m_Level);
	pChr->IncreaseArmor(pChr->GetPlayer()->m_AccData.m_Armor*pChr->GetPlayer()->m_AccData.m_Level);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_GUN, 10);
	
	if(pChr->GetPlayer()->m_AccData.m_AllWeapons)
	{
		pChr->GiveWeapon(WEAPON_RIFLE, 10);
		pChr->GiveWeapon(WEAPON_GRENADE, 10);
		pChr->GiveWeapon(WEAPON_SHOTGUN, 10);
	}

	if(pChr->GetPlayer()->m_AccData.m_NinjaStart)
		pChr->GiveNinja();
}

void IGameController::DoWarmup(int Seconds)
{
	if(Seconds < 0)
		m_Warmup = 0;
	else
		m_Warmup = Seconds*Server()->TickSpeed();
}

bool IGameController::IsFriendlyFire(int ClientID1, int ClientID2)
{
	if(ClientID1 == ClientID2)
		return false;

	if(IsTeamplay())
	{
		if(!GameServer()->m_apPlayers[ClientID1] || !GameServer()->m_apPlayers[ClientID2])
			return false;

		if(GameServer()->m_apPlayers[ClientID1]->GetTeam() == GameServer()->m_apPlayers[ClientID2]->GetTeam())
			return true;
	}

	return false;
}

bool IGameController::IsForceBalanced()
{
	if(m_ForceBalanced)
	{
		m_ForceBalanced = false;
		return true;
	}
	else
		return false;
}

bool IGameController::CanBeMovedOnBalance(int ClientID)
{
	return true;
}

void IGameController::Tick()
{
	// do warmup
	if(m_Warmup)
	{
		m_Warmup--;
		if(!m_Warmup)
			StartRound();
	}

	if(m_GameOverTick != -1)
	{
		// game over.. wait for restart
		if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*10)
		{
			CycleMap();
			StartRound();
			m_RoundCount++;
		}
	}

	// do team-balancing
	if (IsTeamplay() && m_UnbalancedTick != -1 && Server()->Tick() > m_UnbalancedTick+g_Config.m_SvTeambalanceTime*Server()->TickSpeed()*60)
	{
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "Balancing teams");

		int aT[2] = {0,0};
		float aTScore[2] = {0,0};
		float aPScore[MAX_CLIENTS] = {0.0f};
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			{
				aT[GameServer()->m_apPlayers[i]->GetTeam()]++;
				aPScore[i] = GameServer()->m_apPlayers[i]->m_Score*Server()->TickSpeed()*60.0f/
					(Server()->Tick()-GameServer()->m_apPlayers[i]->m_ScoreStartTick);
				aTScore[GameServer()->m_apPlayers[i]->GetTeam()] += aPScore[i];
			}
		}

		// are teams unbalanced?
		if(absolute(aT[0]-aT[1]) >= 2)
		{
			int M = (aT[0] > aT[1]) ? 0 : 1;
			int NumBalance = absolute(aT[0]-aT[1]) / 2;

			do
			{
				CPlayer *pP = 0;
				float PD = aTScore[M];
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!GameServer()->m_apPlayers[i] || !CanBeMovedOnBalance(i))
						continue;
					// remember the player who would cause lowest score-difference
					if(GameServer()->m_apPlayers[i]->GetTeam() == M && (!pP || absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i])) < PD))
					{
						pP = GameServer()->m_apPlayers[i];
						PD = absolute((aTScore[M^1]+aPScore[i]) - (aTScore[M]-aPScore[i]));
					}
				}

				// move the player to the other team
				int Temp = pP->m_LastActionTick;
				pP->SetTeam(M^1);
				pP->m_LastActionTick = Temp;

				pP->Respawn();
				pP->m_ForceBalanced = true;
			} while (--NumBalance);

			m_ForceBalanced = true;
		}
		m_UnbalancedTick = -1;
	}

	// check for inactive players
	if(g_Config.m_SvInactiveKickTime > 0)
	{
		
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			// Dummy
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && !Server()->IsAuthed(i) && !GameServer()->m_apPlayers[i]->m_IsDummy)
			{
				if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick+g_Config.m_SvInactiveKickTime*Server()->TickSpeed()*60)
				{
					switch(g_Config.m_SvInactiveKick)
					{
					case 0:
						{
							// move player to spectator
							GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 1:
						{
							// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
							int Spectators = 0;

							for(int j = 0; j < MAX_CLIENTS; ++j)
								if(GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
									++Spectators;
							if(Spectators >= g_Config.m_SvSpectatorSlots)
								Server()->Kick(i, "Kicked for inactivity");
							else
								GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
						}
						break;
					case 2:
						{
							// kick the player
							Server()->Kick(i, "Kicked for inactivity");
						}
					}
				}
			}
		}
	}

	DoWincheck();
}


bool IGameController::IsTeamplay() const
{
	return m_GameFlags&GAMEFLAG_TEAMS;
}

void IGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameServer()->m_World.m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	pGameInfoObj->m_WarmupTimer = m_Warmup;

	pGameInfoObj->m_ScoreLimit = g_Config.m_SvScorelimit;
	pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;

	pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount+1;
}

int IGameController::GetAutoTeam(int NotThisID)
{
	// this will force the auto balancer to work overtime aswell
	if(g_Config.m_DbgStress)
		return 0;

	int aNumplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	int Team = 0;
	if(IsTeamplay())
		Team = aNumplayers[TEAM_RED] > aNumplayers[TEAM_BLUE] ? TEAM_BLUE : TEAM_RED;

	if(CanJoinTeam(Team, NotThisID))
		return Team;
	return -1;
}

bool IGameController::CanJoinTeam(int Team, int NotThisID)
{
	if(Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID] && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = {0,0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	return (aNumplayers[0] + aNumplayers[1]) < g_Config.m_SvMaxClients-g_Config.m_SvSpectatorSlots;
}

bool IGameController::CheckTeamBalance()
{
	if(!IsTeamplay() || !g_Config.m_SvTeambalanceTime)
		return true;

	int aT[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	char aBuf[256];
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		str_format(aBuf, sizeof(aBuf), "Teams are NOT balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		if(GameServer()->m_pController->m_UnbalancedTick == -1)
			GameServer()->m_pController->m_UnbalancedTick = Server()->Tick();
		return false;
	}
	else
	{
		str_format(aBuf, sizeof(aBuf), "Teams are balanced (red=%d blue=%d)", aT[0], aT[1]);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		GameServer()->m_pController->m_UnbalancedTick = -1;
		return true;
	}
}

bool IGameController::CanChangeTeam(CPlayer *pPlayer, int JoinTeam)
{
	int aT[2] = {0, 0};

	if (!IsTeamplay() || JoinTeam == TEAM_SPECTATORS || !g_Config.m_SvTeambalanceTime)
		return true;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GameServer()->m_apPlayers[i];
		if(pP && pP->GetTeam() != TEAM_SPECTATORS)
			aT[pP->GetTeam()]++;
	}

	// simulate what would happen if changed team
	aT[JoinTeam]++;
	if (pPlayer->GetTeam() != TEAM_SPECTATORS)
		aT[JoinTeam^1]--;

	// there is a player-difference of at least 2
	if(absolute(aT[0]-aT[1]) >= 2)
	{
		// player wants to join team with less players
		if ((aT[0] < aT[1] && JoinTeam == TEAM_RED) || (aT[0] > aT[1] && JoinTeam == TEAM_BLUE))
			return true;
		else
			return false;
	}
	else
		return true;
}

void IGameController::DoWincheck()
{
	if(m_GameOverTick == -1 && !m_Warmup && !GameServer()->m_World.m_ResetRequested)
	{
		if(IsTeamplay())
		{
			// check score win condition
			if((g_Config.m_SvScorelimit > 0 && (m_aTeamscore[TEAM_RED] >= g_Config.m_SvScorelimit || m_aTeamscore[TEAM_BLUE] >= g_Config.m_SvScorelimit)) ||
				(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
			{
				if(m_aTeamscore[TEAM_RED] != m_aTeamscore[TEAM_BLUE])
					EndRound();
				else
					m_SuddenDeath = 1;
			}
		}
		else
		{
			// gather some stats
			int Topscore = 0;
			int TopscoreCount = 0;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameServer()->m_apPlayers[i])
				{
					if(GameServer()->m_apPlayers[i]->m_Score > Topscore)
					{
						Topscore = GameServer()->m_apPlayers[i]->m_Score;
						TopscoreCount = 1;
					}
					else if(GameServer()->m_apPlayers[i]->m_Score == Topscore)
						TopscoreCount++;
				}
			}

			// check score win condition
			if((g_Config.m_SvScorelimit > 0 && Topscore >= g_Config.m_SvScorelimit) ||
				(g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
			{
				if(TopscoreCount == 1)
					EndRound();
				else
					m_SuddenDeath = 1;
			}
		}
	}
}

int IGameController::ClampTeam(int Team)
{
	if(Team < 0)
		return TEAM_SPECTATORS;
	if(IsTeamplay())
		return Team&1;
	return 0;
}
