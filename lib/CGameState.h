#pragma once



//#ifndef _MSC_VER
#include "CCreatureHandler.h"
#include "VCMI_Lib.h"
#include "mapping/CMap.h"
//#endif

#include "HeroBonus.h"
#include "CCreatureSet.h"
#include "ConstTransitivePtr.h"
#include "IGameCallback.h"
#include "ResourceSet.h"
#include "int3.h"
#include "CObjectHandler.h"
#include "IGameCallback.h"


/*
 * CGameState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

class CTown;
class CScriptCallback;
class CCallback;
class IGameCallback;
class CLuaCallback;
class CCPPObjectScript;
class CCreatureSet;
class CStack;
class CQuest;
class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;
class CGDwelling;
class CObjectScript;
class CGObjectInstance;
class CCreature;
class CMap;
struct StartInfo;
struct SDL_Surface;
class CMapHandler;
class CPathfinder;
struct SetObjectProperty;
struct MetaString;
struct CPack;
class CSpell;
struct TerrainTile;
class CHeroClass;
class CCampaign;
class CCampaignState;
class IModableArt;
class CGGarrison;
class CGameInfo;
struct QuestInfo;
class CQuest;

namespace boost
{
	class shared_mutex;
}

//numbers of creatures are exact numbers if detailed else they are quantity ids (0 - a few, 1 - several and so on; additionally -1 - unknown)
struct ArmyDescriptor : public std::map<SlotID, CStackBasicDescriptor>
{
	bool isDetailed;
	DLL_LINKAGE ArmyDescriptor(const CArmedInstance *army, bool detailed); //not detailed -> quantity ids as count
	DLL_LINKAGE ArmyDescriptor();

	DLL_LINKAGE int getStrength() const;
};

struct DLL_LINKAGE InfoAboutArmy
{
	PlayerColor owner;
	std::string name;

	ArmyDescriptor army;

	InfoAboutArmy();
	InfoAboutArmy(const CArmedInstance *Army, bool detailed);

	void initFromArmy(const CArmedInstance *Army, bool detailed);
};

struct DLL_LINKAGE InfoAboutHero : public InfoAboutArmy
{
private:
	void assign(const InfoAboutHero & iah);
public:
	struct DLL_LINKAGE Details
	{
		std::vector<si32> primskills;
		si32 mana, luck, morale;
	} *details;

	const CHeroClass *hclass;
	int portrait;

	InfoAboutHero();
	InfoAboutHero(const InfoAboutHero & iah);
	InfoAboutHero(const CGHeroInstance *h, bool detailed);
	~InfoAboutHero();

	InfoAboutHero & operator=(const InfoAboutHero & iah);

	void initFromHero(const CGHeroInstance *h, bool detailed);
};

/// Struct which holds a int information about a town
struct DLL_LINKAGE InfoAboutTown : public InfoAboutArmy
{
	struct DLL_LINKAGE Details
	{
		si32 hallLevel, goldIncome;
		bool customRes;
		bool garrisonedHero;

	} *details;

	const CTown *tType;

	si32 built;
	si32 fortLevel; //0 - none

	InfoAboutTown();
	InfoAboutTown(const CGTownInstance *t, bool detailed);
	~InfoAboutTown();
	void initFromTown(const CGTownInstance *t, bool detailed);
};

// typedef si32 TResourceUnit;
// typedef std::vector<si32> TResourceVector;
// typedef std::set<si32> TResourceSet;

struct DLL_LINKAGE SThievesGuildInfo
{
	std::vector<PlayerColor> playerColors; //colors of players that are in-game

	std::vector< std::vector< PlayerColor > > numOfTowns, numOfHeroes, gold, woodOre, mercSulfCrystGems, obelisks, artifacts, army, income; // [place] -> [colours of players]

	std::map<PlayerColor, InfoAboutHero> colorToBestHero; //maps player's color to his best heros'

    std::map<PlayerColor, EAiTactic::EAiTactic> personality; // color to personality // ai tactic
	std::map<PlayerColor, si32> bestCreature; // color to ID // id or -1 if not known

// 	template <typename Handler> void serialize(Handler &h, const int version)
// 	{
// 		h & playerColors & numOfTowns & numOfHeroes & gold & woodOre & mercSulfCrystGems & obelisks & artifacts & army & income;
// 		h & colorToBestHero & personality & bestCreature;
// 	}

};

struct DLL_LINKAGE PlayerState : public CBonusSystemNode
{
public:
	PlayerColor color;
	bool human; //true if human controlled player, false for AI
	ObjectInstanceID currentSelection; //id of hero/town, 0xffffffff if none
	TeamID team;
	TResources resources;
	std::vector<ConstTransitivePtr<CGHeroInstance> > heroes;
	std::vector<ConstTransitivePtr<CGTownInstance> > towns;
	std::vector<ConstTransitivePtr<CGHeroInstance> > availableHeroes; //heroes available in taverns
	std::vector<ConstTransitivePtr<CGDwelling> > dwellings; //used for town growth
	std::vector<QuestInfo> quests; //store info about all received quests

	bool enteredWinningCheatCode, enteredLosingCheatCode; //if true, this player has entered cheat codes for loss / victory
	EPlayerStatus::EStatus status;
	ui8 daysWithoutCastle;

	PlayerState();
	std::string nodeName() const override;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & color & human & currentSelection & team & resources & status;
		h & heroes & towns & availableHeroes & dwellings;
		h & getBonusList(); //FIXME FIXME FIXME
		h & status & daysWithoutCastle;
		h & enteredLosingCheatCode & enteredWinningCheatCode;
		h & static_cast<CBonusSystemNode&>(*this);
	}
};

struct DLL_LINKAGE TeamState : public CBonusSystemNode
{
public:
	TeamID id; //position in gameState::teams
	std::set<PlayerColor> players; // members of this team
	std::vector<std::vector<std::vector<ui8> > >  fogOfWarMap; //true - visible, false - hidden

	TeamState();

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & id & players & fogOfWarMap;
		h & static_cast<CBonusSystemNode&>(*this);
	}

};

struct UpgradeInfo
{
	CreatureID oldID; //creature to be upgraded
	std::vector<CreatureID> newID; //possible upgrades
	std::vector<TResources> cost; // cost[upgrade_serial] -> set of pairs<resource_ID,resource_amount>; cost is for single unit (not entire stack)
	UpgradeInfo(){oldID = CreatureID::NONE;};
};

struct DLL_LINKAGE CGPathNode
{
	enum EAccessibility
	{
		NOT_SET = 0,
		ACCESSIBLE = 1, //tile can be entered and passed
		VISITABLE, //tile can be entered as the last tile in path
		BLOCKVIS,  //visitable from neighbouring tile but not passable
		BLOCKED //tile can't be entered nor visited
	};

	EAccessibility accessible;
	ui8 land;
	ui8 turns;
	ui32 moveRemains;
	CGPathNode * theNodeBefore;
	int3 coord; //coordinates

	CGPathNode();
	bool reachable() const;
};

struct DLL_LINKAGE CGPath
{
	std::vector<CGPathNode> nodes; //just get node by node

	int3 startPos() const; // start point
	int3 endPos() const; //destination point
	void convert(ui8 mode); //mode=0 -> from 'manifest' to 'object'
};

struct DLL_LINKAGE CPathsInfo
{
	bool isValid;
	const CGHeroInstance *hero;
	int3 hpos;
	int3 sizes;
	CGPathNode ***nodes; //[w][h][level]

	bool getPath(const int3 &dst, CGPath &out);
	CPathsInfo(const int3 &Sizes);
	~CPathsInfo();
};

struct DLL_EXPORT DuelParameters
{
	ETerrainType terType;
	BFieldType bfieldType;
	struct DLL_EXPORT SideSettings
	{
		struct DLL_EXPORT StackSettings
		{
			CreatureID type;
			si32 count;
			template <typename Handler> void serialize(Handler &h, const int version)
			{
				h & type & count;
			}

			StackSettings();
			StackSettings(CreatureID Type, si32 Count);
		} stacks[GameConstants::ARMY_SIZE];

		si32 heroId; //-1 if none
		std::vector<si32> heroPrimSkills; //may be empty
		std::map<si32, CArtifactInstance*> artifacts;
		std::vector<std::pair<si32, si8> > heroSecSkills; //may be empty; pairs <id, level>, level [0-3]
		std::set<SpellID> spells;

		SideSettings();
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & stacks & heroId & heroPrimSkills & artifacts & heroSecSkills & spells;
		}
	} sides[2];

	std::vector<shared_ptr<CObstacleInstance> > obstacles;

	static DuelParameters fromJSON(const std::string &fname);

	struct CusomCreature
	{
		int id;
		int attack, defense, dmg, HP, speed, shoots;

		CusomCreature()
		{
			id = attack = defense = dmg = HP = speed = shoots = -1;
		}
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & id & attack & defense & dmg & HP & speed & shoots;
		}
	};

	std::vector<CusomCreature> creatures;

	DuelParameters();
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & terType & bfieldType & sides & obstacles & creatures;
	}
};

class CPathfinder : private CGameInfoCallback
{
private:
	bool useSubterraneanGates;
	bool allowEmbarkAndDisembark;
	CPathsInfo &out;
	const CGHeroInstance *hero;
	const std::vector<std::vector<std::vector<ui8> > > &FoW;

	std::list<CGPathNode*> mq; //BFS queue -> nodes to be checked


	int3 curPos;
	CGPathNode *cp; //current (source) path node -> we took it from the queue
	CGPathNode *dp; //destination node -> it's a neighbour of cp that we consider
	const TerrainTile *ct, *dt; //tile info for both nodes
	ui8 useEmbarkCost; //0 - usual movement; 1 - embark; 2 - disembark
	int destTopVisObjID;


	CGPathNode *getNode(const int3 &coord);
	void initializeGraph();
	bool goodForLandSeaTransition(); //checks if current move will be between sea<->land. If so, checks it legality (returns false if movement is not possible) and sets useEmbarkCost

	CGPathNode::EAccessibility evaluateAccessibility(const TerrainTile *tinfo) const;
	bool canMoveBetween(const int3 &a, const int3 &b) const; //checks only for visitable objects that may make moving between tiles impossible, not other conditions (like tiles itself accessibility)

public:
	CPathfinder(CPathsInfo &_out, CGameState *_gs, const CGHeroInstance *_hero);
	void calculatePaths(int3 src = int3(-1,-1,-1), int movement = -1); //calculates possible paths for hero, by default uses current hero position and movement left; returns pointer to newly allocated CPath or nullptr if path does not exists
};


struct BattleInfo;

class DLL_LINKAGE CGameState : public CNonConstInfoCallback
{
public:
	ConstTransitivePtr<StartInfo> scenarioOps, initialOpts; //second one is a copy of settings received from pregame (not randomized)
	PlayerColor currentPlayer; //ID of player currently having turn
	ConstTransitivePtr<BattleInfo> curB; //current battle
	ui32 day; //total number of days in game
	ConstTransitivePtr<CMap> map;
	std::map<PlayerColor, PlayerState> players;
	std::map<TeamID, TeamState> teams;
	CBonusSystemNode globalEffects;

	struct DLL_LINKAGE HeroesPool
	{
		std::map<ui32, ConstTransitivePtr<CGHeroInstance> > heroesPool; //[subID] - heroes available to buy; nullptr if not available
		std::map<ui32,ui8> pavailable; // [subid] -> which players can recruit hero (binary flags)

		CGHeroInstance * pickHeroFor(bool native, PlayerColor player, const CTown *town, std::map<ui32, ConstTransitivePtr<CGHeroInstance> > &available, const CHeroClass *bannedClass = nullptr) const;

		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & heroesPool & pavailable;
		}
	} hpool; //we have here all heroes available on this map that are not hired

	boost::shared_mutex *mx;

	void init(StartInfo * si);

	bool isUsedHero(HeroTypeID hid) const; //looks in heroes and prisons
	void placeCampaignHeroes(const std::vector<std::pair<CGHeroInstance*, ObjectInstanceID> > &campHeroReplacements);
	std::vector<std::pair<CGHeroInstance*, ObjectInstanceID> > campaignHeroesToReplace(); //returns heroes and placeholders in where heroes will be put; may remove some placeholders
	std::set<HeroTypeID> getUnusedAllowedHeroes(bool alsoIncludeNotAllowed = false) const;
	void initDuel();
	void randomizeObject(CGObjectInstance *cur);
	std::pair<Obj,int> pickObject(CGObjectInstance *obj); //chooses type of object to be randomized, returns <type, subtype>
	int pickHero(PlayerColor owner);
	void giveHeroArtifact(CGHeroInstance *h, ArtifactID aid);

	void apply(CPack *pack);
	BFieldType battleGetBattlefieldType(int3 tile) const;
	UpgradeInfo getUpgradeInfo(const CStackInstance &stack);
	PlayerRelations::PlayerRelations getPlayerRelations(PlayerColor color1, PlayerColor color2);
	bool checkForVisitableDir(const int3 & src, const int3 & dst) const; //check if src tile is visitable from dst tile
	bool checkForVisitableDir(const int3 & src, const TerrainTile *pom, const int3 & dst) const; //check if src tile is visitable from dst tile
	void calculatePaths(const CGHeroInstance *hero, CPathsInfo &out, int3 src = int3(-1,-1,-1), int movement = -1); //calculates possible paths for hero, by default uses current hero position and movement left; returns pointer to newly allocated CPath or nullptr if path does not exists
	int3 guardingCreaturePosition (int3 pos) const;
	std::vector<CGObjectInstance*> guardingCreatures (int3 pos) const;
	int victoryCheck(PlayerColor player) const; //checks if given player is winner; -1 if std victory, 1 if special victory, 0 else
	int lossCheck(PlayerColor player) const; //checks if given player is loser;  -1 if std loss, 1 if special, 0 else
	PlayerColor checkForStandardWin() const; //returns color of player that accomplished standard victory conditions or 255 (NEUTRAL) if no winner
	bool checkForStandardLoss(PlayerColor player) const; //checks if given player lost the game
	void obtainPlayersStats(SThievesGuildInfo & tgi, int level); //fills tgi with info about other players that is available at given level of thieves' guild
	std::map<ui32, ConstTransitivePtr<CGHeroInstance> > unusedHeroesFromPool(); //heroes pool without heroes that are available in taverns
	BattleInfo * setupBattle(int3 tile, const CArmedInstance *armies[2], const CGHeroInstance * heroes[2], bool creatureBank, const CGTownInstance *town);

	void buildBonusSystemTree();
	void attachArmedObjects();
	void buildGlobalTeamPlayerTree();
	void deserializationFix();

	bool isVisible(int3 pos, PlayerColor player);
	bool isVisible(const CGObjectInstance *obj, boost::optional<PlayerColor> player);

	CGameState(); //c-tor
	virtual ~CGameState(); //d-tor
	void getNeighbours(const TerrainTile &srct, int3 tile, std::vector<int3> &vec, const boost::logic::tribool &onLand, bool limitCoastSailing);
	int getMovementCost(const CGHeroInstance *h, const int3 &src, const int3 &dest, int remainingMovePoints=-1, bool checkLast=true);
	int getDate(Date::EDateType mode=Date::DAY) const; //mode=0 - total days in game, mode=1 - day of week, mode=2 - current week, mode=3 - current month
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & scenarioOps & initialOpts & currentPlayer & day & map & players & teams & hpool & globalEffects;
		BONUS_TREE_DESERIALIZATION_FIX
	}

	friend class CCallback;
	friend class CLuaCallback;
	friend class CClient;
	friend void initGameState(CMap * map, CGameInfo * cgi);
	friend class IGameCallback;
	friend class CMapHandler;
	friend class CGameHandler;
};

struct DLL_LINKAGE QuestInfo //universal interface for human and AI
{
	const CQuest * quest;
	const CGObjectInstance * obj; //related object, most likely Seer Hut
	int3 tile;

	QuestInfo(){};
	QuestInfo (const CQuest * Quest, const CGObjectInstance * Obj, int3 Tile) :
		quest (Quest), obj (Obj), tile (Tile){};

	//FIXME: assignment operator should return QuestInfo &
	bool operator= (const QuestInfo &qi)
	{
		quest = qi.quest;
		obj = qi.obj;
		tile = qi.tile;
		return true;
	}

	bool operator== (const QuestInfo & qi) const
	{
		return (quest == qi.quest && obj == qi.obj);
	}

	//std::vector<std::string> > texts //allow additional info for quest log?

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & quest & obj & tile;
	}
};
