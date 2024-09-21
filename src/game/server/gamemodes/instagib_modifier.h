#pragma once

#include <base/vmath.h>
#include <engine/shared/protocol.h>

class CCharacter;
class CGameContext;
class IGameController;
class CLaser;
struct CNetObj_Character;

class CInstagibModifier
{
protected:
	CGameContext* m_pGameServer;
	bool m_Activated;
	bool m_ConfigLaserJump;
	bool m_ConfigShield;
	char m_GameType[64];
	int m_ShieldCD[MAX_CLIENTS];

public:
	CInstagibModifier()
	{
		m_pGameServer = 0;
		m_Activated = false;
		m_ConfigLaserJump = true;
		m_ConfigShield = true;
		mem_zero(m_ShieldCD, sizeof(m_ShieldCD));
	}

	void ScanGametypeForActivation(CGameContext* pGameServer, char* pGameTypeStr);
	bool IsActivated() const { return m_Activated; }
	const char *GetGameType() const { return m_GameType; }
	void OnInit(IGameController* pGameController);
	void OnTick();
	void OnCharacterSpawn(CCharacter* pChar);
	void OnCharacterFireLaser(CCharacter* pChar);
	bool OnCharacterTakeDamage(CCharacter* pChar, int Weapon, vec2 Force, int FromCID);
	void CharacterDoWeaponSwitch(CCharacter* pChar);
	void OnCharacterSnap(CCharacter* pChar, CNetObj_Character* pNetChar);

	bool LaserDoBounce(CLaser *pLaser);
};
