#include "CBattleInterface.h"
#include "CGameInfo.h"
#include "../hch/CLodHandler.h"
#include "SDL_Extensions.h"
#include "CAdvmapInterface.h"
#include "AdventureMapButton.h"
#include "../hch/CObjectHandler.h"
#include "../hch/CHeroHandler.h"
#include "../hch/CDefHandler.h"
#include "../hch/CSpellHandler.h"
#include "CMessage.h"
#include "CCursorHandler.h"
#include "../CCallback.h"
#include "../lib/CGameState.h"
#include "../hch/CGeneralTextHandler.h"
#include "CCreatureAnimation.h"
#include "Graphics.h"
#include "CSpellWindow.h"
#include "CConfigHandler.h"
#include <queue>
#include <sstream>
#include "../lib/CondSh.h"
#include "../lib/NetPacks.h"
#include "CPlayerInterface.h"
#include "../hch/CVideoHandler.h"
#include "../hch/CTownHandler.h"
#include <boost/assign/list_of.hpp>
#include <boost/lexical_cast.hpp>
#ifndef __GNUC__
const double M_PI = 3.14159265358979323846;
#else
#define _USE_MATH_DEFINES
#include <cmath>
#endif

/*
 * CBattleInterface.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

extern SDL_Surface * screen;
extern TTF_Font * TNRB16, *TNR, *GEOR13, *GEORXX, *GEORM, *GEOR16;
extern SDL_Color zwykly;

BattleSettings CBattleInterface::settings;
CondSh<bool> CBattleInterface::animsAreDisplayed;

struct CMP_stack2
{
	inline bool operator ()(const CStack& a, const CStack& b)
	{
		return (a.Speed())>(b.Speed());
	}
} cmpst2 ;

static void transformPalette(SDL_Surface * surf, float rCor, float gCor, float bCor)
{
	SDL_Color * colorsToChange = surf->format->palette->colors;
	for(int g=0; g<surf->format->palette->ncolors; ++g)
	{
		if((colorsToChange+g)->b != 132 &&
			(colorsToChange+g)->g != 231 &&
			(colorsToChange+g)->r != 255) //it's not yellow border
		{
			(colorsToChange+g)->r = (float)((colorsToChange+g)->r) * rCor;
			(colorsToChange+g)->g = (float)((colorsToChange+g)->g) * gCor;
			(colorsToChange+g)->b = (float)((colorsToChange+g)->b) * bCor;
		}
	}
}
////////////////////////Battle helpers

//general anim

void CBattleAnimation::endAnim()
{
	for(std::list<std::pair<CBattleAnimation *, bool> >::iterator it = owner->pendingAnims.begin(); it != owner->pendingAnims.end(); ++it)
	{
		if(it->first == this)
		{
			it->first = NULL;
		}
	}

}

bool CBattleAnimation::isEarliest(bool perStackConcurrency)
{
	int lowestMoveID = owner->animIDhelper + 5;
	for(std::list<std::pair<CBattleAnimation *, bool> >::iterator it = owner->pendingAnims.begin(); it != owner->pendingAnims.end(); ++it)
	{
		CBattleStackAnimation * stAnim = dynamic_cast<CBattleStackAnimation *>(it->first);
		CBattleStackAnimation * thAnim = dynamic_cast<CBattleStackAnimation *>(this);
		if(perStackConcurrency && stAnim && thAnim && stAnim->stackID != thAnim->stackID)
			continue;

		CReverseAnim * revAnim = dynamic_cast<CReverseAnim *>(stAnim);

		if(revAnim && thAnim && stAnim && stAnim->stackID == thAnim->stackID && revAnim->priority)
			return false;

		if(it->first)
			amin(lowestMoveID, it->first->ID);
	}
	return ID == lowestMoveID;
}

CBattleAnimation::CBattleAnimation(CBattleInterface * _owner)
: owner(_owner), ID(_owner->animIDhelper++)
{}

//effect animation
bool CSpellEffectAnim::init()
{
	if(!isEarliest(false))
		return false;

	if(effect == 12) //armageddon
	{
		if(effect == -1 || graphics->battleACToDef[effect].size() != 0)
		{
			CDefHandler * anim;
			if(customAnim.size())
				anim = CDefHandler::giveDef(customAnim);
			else
				anim = CDefHandler::giveDef(graphics->battleACToDef[effect][0]);

			for(int i=0; i * anim->width < owner->pos.w ; ++i)
			{
				for(int j=0; j * anim->height < owner->pos.h ; ++j)
				{
					SBattleEffect be;
					be.effectID = ID;
					be.anim = CDefHandler::giveDef(graphics->battleACToDef[effect][0]);
					be.frame = 0;
					be.maxFrame = be.anim->ourImages.size();
					be.x = i * anim->width + owner->pos.x;
					be.y = j * anim->height + owner->pos.y;

					owner->battleEffects.push_back(be);
				}
			}
		}
	}
	else // Effects targeted at a specific creature/hex.
	{
		if(effect == -1 || graphics->battleACToDef[effect].size() != 0)
		{
			const CStack* destStack = LOCPLINT->cb->battleGetStackByPos(destTile, false);
			Rect &tilePos = owner->bfield[destTile].pos;
			SBattleEffect be;
			be.effectID = ID;
			if(customAnim.size())
				be.anim = CDefHandler::giveDef(customAnim);
			else
				be.anim = CDefHandler::giveDef(graphics->battleACToDef[effect][0]);
			be.frame = 0;
			be.maxFrame = be.anim->ourImages.size();

			switch (effect)
			{
			case -1:
				be.x = x;
				be.y = y;
				break;
			case 0: // Prayer and Lightning Bolt.
			case 1:
				// Position effect with it's bottom center touching the bottom center of affected tile(s).
				be.x = tilePos.x + tilePos.w/2 - be.anim->width/2;
				be.y = tilePos.y + tilePos.h - be.anim->height;
				break;

			default:
				// Position effect with it's center touching the top center of affected tile(s).
				be.x = tilePos.x + tilePos.w/2 - be.anim->width/2;
				be.y = tilePos.y - be.anim->height/2;
				break;
			}

			// Correction for 2-hex creatures.
			if (destStack != NULL && destStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
				be.x += (destStack->attackerOwned ? -1 : 1)*tilePos.w/2;

			owner->battleEffects.push_back(be);
		}
	}
	//battleEffects 
	return true;
}

void CSpellEffectAnim::nextFrame()
{
	for(std::list<SBattleEffect>::iterator it = owner->battleEffects.begin(); it != owner->battleEffects.end(); ++it)
	{
		++(it->frame);

		if(it->frame == it->maxFrame)
		{
			endAnim();
			break;
		}
		else
		{
			it->x += dx;
			it->y += dy;
		}
	}
}

void CSpellEffectAnim::endAnim()
{
	CBattleAnimation::endAnim();

	std::vector<std::list<SBattleEffect>::iterator> toDel;

	for(std::list<SBattleEffect>::iterator it = owner->battleEffects.begin(); it != owner->battleEffects.end(); ++it)
	{
		if(it->effectID == ID)
		{
			toDel.push_back(it);
		}
	}

	for(int b=0; b<toDel.size(); ++b)
	{
		delete toDel[b]->anim;
		owner->battleEffects.erase(toDel[b]);
	}

	delete this;
}

CSpellEffectAnim::CSpellEffectAnim(CBattleInterface * _owner, ui32 _effect, int _destTile, int _dx, int _dy)
:CBattleAnimation(_owner), effect(_effect), destTile(_destTile), customAnim(""), dx(_dx), dy(_dy)
{
}

CSpellEffectAnim::CSpellEffectAnim(CBattleInterface * _owner, std::string _customAnim, int _x, int _y, int _dx, int _dy)
:CBattleAnimation(_owner), effect(-1), destTile(0), customAnim(_customAnim), x(_x), y(_y), dx(_dx), dy(_dy)
{
}

//stack's aniamtion

CBattleStackAnimation::CBattleStackAnimation(CBattleInterface * _owner, int stack)
: CBattleAnimation(_owner), stackID(stack)
{
}

bool CBattleStackAnimation::isToReverseHlp(int hexFrom, int hexTo, bool curDir)
{
	int fromMod = hexFrom % BFIELD_WIDTH;
	int fromDiv = hexFrom / BFIELD_WIDTH;
	int toMod = hexTo % BFIELD_WIDTH;

	if(curDir && fromMod < toMod)
		return false;
	else if(curDir && fromMod > toMod)
		return true;
	else if(curDir && fromMod == toMod)
	{
		return fromDiv % 2 == 0;
	}
	else if(!curDir && fromMod < toMod)
		return true;
	else if(!curDir && fromMod > toMod)
		return false;
	else if(!curDir && fromMod == toMod)
	{
		return fromDiv % 2 == 1;
	}
	tlog1 << "Catastrope in CBattleStackAnimation::isToReverse!" << std::endl;
	return false; //should never happen
}

bool CBattleStackAnimation::isToReverse(int hexFrom, int hexTo, bool curDir, bool toDoubleWide, bool toDir)
{
	if(toDoubleWide)
	{
		return isToReverseHlp(hexFrom, hexTo, curDir) &&
			(toDir ? isToReverseHlp(hexFrom, hexTo-1, curDir) : isToReverseHlp(hexFrom, hexTo+1, curDir) );
	}
	else
	{
		return isToReverseHlp(hexFrom, hexTo, curDir);
	}
}

//revering animation

bool CReverseAnim::init()
{
	if(owner->creAnims[stackID] == NULL || owner->creAnims[stackID]->getType() == 5)
	{
		endAnim();

		return false; //there is no such creature
	}

	if(!priority && !isEarliest(false))
		return false;
	
	owner->creAnims[stackID]->setType(8);

	return true;
}

void CReverseAnim::nextFrame()
{
	if(partOfAnim == 1) //first part of animation
	{
		if(owner->creAnims[stackID]->onLastFrameInGroup())
		{
			partOfAnim = 2;
		}
	}
	else if(partOfAnim == 2)
	{
		if(!secondPartSetup)
		{
			owner->creDir[stackID] = !owner->creDir[stackID];

			const CStack * curs = LOCPLINT->cb->battleGetStackByID(stackID, false);

			Point coords = CBattleHex::getXYUnitAnim(hex, owner->creDir[stackID], curs, owner);
			owner->creAnims[stackID]->pos.x = coords.x;
			//creAnims[stackID]->pos.y = coords.second;

			if(curs->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
			{
				if(curs->attackerOwned)
				{
					if(!owner->creDir[stackID])
						owner->creAnims[stackID]->pos.x -= 44;
				}
				else
				{
					if(owner->creDir[stackID])
						owner->creAnims[stackID]->pos.x += 44;
				}
			}

			owner->creAnims[stackID]->setType(7);
			secondPartSetup = true;
		}

		if(owner->creAnims[stackID]->onLastFrameInGroup())
		{
			endAnim();
		}
	}
}

void CReverseAnim::endAnim()
{
	CBattleAnimation::endAnim();
	if( LOCPLINT->cb->battleGetStackByID(stackID) )//don't do that if stack is dead
		owner->creAnims[stackID]->setType(2);

	delete this;
}

CReverseAnim::CReverseAnim(CBattleInterface * _owner, int stack, int dest, bool _priority)
: CBattleStackAnimation(_owner, stack), partOfAnim(1), hex(dest), secondPartSetup(false), priority(_priority)
{
}


//defence anim

bool CDefenceAnim::init()
{
	//checking initial conditions

	//if(owner->creAnims[stackID]->getType() != 2)
	//{
	//	return false;
	//}

	if(IDby == -1 && owner->battleEffects.size() > 0)
		return false;

	int lowestMoveID = owner->animIDhelper + 5;
	for(std::list<std::pair<CBattleAnimation *, bool> >::iterator it = owner->pendingAnims.begin(); it != owner->pendingAnims.end(); ++it)
	{
		if(dynamic_cast<CDefenceAnim *>(it->first))
			continue;

		CBattleAttack * attAnim = dynamic_cast<CBattleAttack *>(it->first);
		if(attAnim && attAnim->stackID != stackID)
			continue;

		const CStack * attacker = LOCPLINT->cb->battleGetStackByID(IDby, false);
		if(IDby != -1)
		{
			int attackerAnimType = owner->creAnims[IDby]->getType();
			if( attackerAnimType == 11 && attackerAnimType == 12 && attackerAnimType == 13 && owner->creAnims[IDby]->getFrame() < attacker->creature->attackClimaxFrame )
				return false;
		}

		CReverseAnim * animAsRev = dynamic_cast<CReverseAnim *>(it->first);

		if(animAsRev && animAsRev->priority)
			return false;

		if(it->first)
			amin(lowestMoveID, it->first->ID);
	}
	if(ID > lowestMoveID)
		return false;

	
	const CStack * attacker = LOCPLINT->cb->battleGetStackByID(IDby, false);
	const CStack * attacked = LOCPLINT->cb->battleGetStackByID(stackID, false);

	//reverse unit if necessary
	if(attacker && isToReverse(attacked->position, attacker->position, owner->creDir[stackID], attacker->hasFeatureOfType(StackFeature::DOUBLE_WIDE), owner->creDir[IDby]))
	{
		owner->addNewAnim(new CReverseAnim(owner, stackID, attacked->position, true));
		return false;
	}
	//unit reversed

	if(byShooting) //delay hit animation
	{		
		for(std::list<SProjectileInfo>::const_iterator it = owner->projectiles.begin(); it != owner->projectiles.end(); ++it)
		{
			if(it->creID == attacker->creature->idNumber)
			{
				return false;
			}
		}
	}

	//initializing
	int maxLen = 0;
		
	if(killed)
	{
		CGI->soundh->playSound(attacked->creature->sounds.killed);
		owner->creAnims[stackID]->setType(5); //death
	}
	else
	{
		// TODO: this block doesn't seems correct if the unit is defending.
		CGI->soundh->playSound(attacked->creature->sounds.wince);
		owner->creAnims[stackID]->setType(3); //getting hit
	}

	return true; //initialized successfuly
}

void CDefenceAnim::nextFrame()
{
	if(!killed && owner->creAnims[stackID]->getType() != 3)
	{
		owner->creAnims[stackID]->setType(3);
	}

	if(!owner->creAnims[stackID]->onLastFrameInGroup())
	{
		if( owner->creAnims[stackID]->getType() == 5 && (owner->animCount+1)%(4/CBattleInterface::settings.animSpeed)==0
			&& !owner->creAnims[stackID]->onLastFrameInGroup() )
		{
			owner->creAnims[stackID]->incrementFrame();
		}
	}
	else
	{
		endAnim();
	}
	
}

void CDefenceAnim::endAnim()
{
	//restoring animType

	if(owner->creAnims[stackID]->getType() == 3)
		owner->creAnims[stackID]->setType(2);

	//printing info to console

	if(IDby!=-1)
		owner->printConsoleAttacked(stackID, dmg, amountKilled, IDby);

	const CStack * attacker = LOCPLINT->cb->battleGetStackByID(IDby, false);
	const CStack * attacked = LOCPLINT->cb->battleGetStackByID(stackID, false);

	CBattleAnimation::endAnim();

	delete this;
}

CDefenceAnim::CDefenceAnim(SStackAttackedInfo _attackedInfo, CBattleInterface * _owner)
: CBattleStackAnimation(_owner, _attackedInfo.ID), dmg(_attackedInfo.dmg),
	amountKilled(_attackedInfo.amountKilled), IDby(_attackedInfo.IDby), byShooting(_attackedInfo.byShooting),
	killed(_attackedInfo.killed)
{
}

////move anim

bool CBattleStackMoved::init()
{
	if( !isEarliest(false) )
		return false;

	//a few useful variables
	steps = owner->creAnims[stackID]->framesInGroup(0)*owner->getAnimSpeedMultiplier()-1;
	whichStep = 0;
	int hexWbase = 44, hexHbase = 42;
	const CStack * movedStack = LOCPLINT->cb->battleGetStackByID(stackID, false);
	if(!movedStack || owner->creAnims[stackID]->getType() == 5)
	{
		endAnim();
		return false;
	}
	bool twoTiles = movedStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE);
	
	Point begPosition = CBattleHex::getXYUnitAnim(curStackPos, movedStack->attackerOwned, movedStack, owner);
	Point endPosition = CBattleHex::getXYUnitAnim(destHex, movedStack->attackerOwned, movedStack, owner);

	int mutPos = BattleInfo::mutualPosition(curStackPos, destHex);
	
	//reverse unit if necessary
	if((begPosition.x > endPosition.x) && owner->creDir[stackID] == true)
	{
		owner->addNewAnim(new CReverseAnim(owner, stackID, curStackPos, true));
		return false;
	}
	else if ((begPosition.x < endPosition.x) && owner->creDir[stackID] == false)
	{
		owner->addNewAnim(new CReverseAnim(owner, stackID, curStackPos, true));
		return false;
	}

	if(owner->creAnims[stackID]->getType() != 0)
	{
		owner->creAnims[stackID]->setType(0);
	}
	//unit reversed

	if(owner->moveSh <= 0)
		owner->moveSh = CGI->soundh->playSound(movedStack->creature->sounds.move, -1);

	//step shift calculation
	posX = owner->creAnims[stackID]->pos.x, posY = owner->creAnims[stackID]->pos.y; // for precise calculations ;]
	if(mutPos == -1 && movedStack->hasFeatureOfType(StackFeature::FLYING)) 
	{
		steps *= distance;
		steps /= 2; //to make animation faster

		stepX = (endPosition.x - (float)begPosition.x)/steps;
		stepY = (endPosition.y - (float)begPosition.y)/steps;
	}
	else
	{
		switch(mutPos)
		{
		case 0:
			stepX = (-1.0)*((float)hexWbase)/(2.0f*steps);
			stepY = (-1.0)*((float)hexHbase)/((float)steps);
			break;
		case 1:
			stepX = ((float)hexWbase)/(2.0f*steps);
			stepY = (-1.0)*((float)hexHbase)/((float)steps);
			break;
		case 2:
			stepX = ((float)hexWbase)/((float)steps);
			stepY = 0.0;
			break;
		case 3:
			stepX = ((float)hexWbase)/(2.0f*steps);
			stepY = ((float)hexHbase)/((float)steps);
			break;
		case 4:
			stepX = (-1.0)*((float)hexWbase)/(2.0f*steps);
			stepY = ((float)hexHbase)/((float)steps);
			break;
		case 5:
			stepX = (-1.0)*((float)hexWbase)/((float)steps);
			stepY = 0.0;
			break;
		}
	}
	//step shifts calculated

	return true;
}

void CBattleStackMoved::nextFrame()
{
	//moving instructions
	posX += stepX;
	owner->creAnims[stackID]->pos.x = posX;
	posY += stepY;
	owner->creAnims[stackID]->pos.y = posY;

	++whichStep;
	if(whichStep == steps)
	{
		endAnim();
	}
}

void CBattleStackMoved::endAnim()
{
	const CStack * movedStack = LOCPLINT->cb->battleGetStackByID(stackID);

	CBattleAnimation::endAnim();

	if(movedStack)
	{
		bool twoTiles = movedStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE);

		if(endMoving)
		{
			owner->addNewAnim(new CBattleMoveEnd(owner, stackID, destHex));
		}

		Point coords = CBattleHex::getXYUnitAnim(destHex, owner->creDir[stackID], movedStack, owner);
		owner->creAnims[stackID]->pos = coords;

		if(!endMoving && twoTiles && bool(movedStack->attackerOwned) && (owner->creDir[stackID] != bool(movedStack->attackerOwned) )) //big attacker creature is reversed
			owner->creAnims[stackID]->pos.x -= 44;
		else if(!endMoving && twoTiles && (! bool(movedStack->attackerOwned) ) && (owner->creDir[stackID] != bool(movedStack->attackerOwned) )) //big defender creature is reversed
			owner->creAnims[stackID]->pos.x += 44;
	}

	if(owner->moveSh >= 0)
	{
		CGI->soundh->stopSound(owner->moveSh);
		owner->moveSh = -1;
	}

	delete this;
}

CBattleStackMoved::CBattleStackMoved(CBattleInterface * _owner, int _number, int _destHex, bool _endMoving, int _distance)
: CBattleStackAnimation(_owner, _number), destHex(_destHex), endMoving(_endMoving), distance(_distance), stepX(0.0f), stepY(0.0f)
{
	curStackPos = LOCPLINT->cb->battleGetPos(stackID);
}

//move started

bool CBattleMoveStart::init()
{
	if( !isEarliest(false) )
		return false;

	const CStack * movedStack = LOCPLINT->cb->battleGetStackByID(stackID, false);

	if(!movedStack || owner->creAnims[stackID]->getType() == 5)
	{
		CBattleMoveStart::endAnim();
		return false;
	}

	if (movedStack->creature->sounds.startMoving)
		CGI->soundh->playSound(movedStack->creature->sounds.startMoving);

	return true;
}

void CBattleMoveStart::nextFrame()
{
	if(owner->creAnims[stackID]->onLastFrameInGroup())
	{
		endAnim();
	}
	else
	{
		if((owner->animCount+1)%(4/CBattleInterface::settings.animSpeed)==0)
			owner->creAnims[stackID]->incrementFrame();
	}
}

void CBattleMoveStart::endAnim()
{
	CBattleAnimation::endAnim();

	delete this;
}

CBattleMoveStart::CBattleMoveStart(CBattleInterface * _owner, int stack)
: CBattleStackAnimation(_owner, stack)
{
}

//move finished

bool CBattleMoveEnd::init()
{
	if( !isEarliest(true) )
		return false;

	const CStack * movedStack = LOCPLINT->cb->battleGetStackByID(stackID, false);
	if(!movedStack || owner->creAnims[stackID]->framesInGroup(21) == 0 || owner->creAnims[stackID]->getType() == 5)
	{
		endAnim();

		return false;
	}

	
	if (movedStack->creature->sounds.endMoving)
	{
		CGI->soundh->playSound(movedStack->creature->sounds.endMoving);
	}

	owner->creAnims[stackID]->setType(21);

	return true;
}

void CBattleMoveEnd::nextFrame()
{
	if(owner->creAnims[stackID]->onLastFrameInGroup())
	{
		endAnim();
	}
}

void CBattleMoveEnd::endAnim()
{
	CBattleAnimation::endAnim();

	if(owner->creAnims[stackID]->getType() != 5)
		owner->creAnims[stackID]->setType(2); //resetting to default

	CGI->curh->show();
	delete this;
}

CBattleMoveEnd::CBattleMoveEnd(CBattleInterface * _owner, int stack, int destTile)
: CBattleStackAnimation(_owner, stack), destinationTile(destTile)
{
}

//general attack anim

void CBattleAttack::nextFrame()
{
	if(owner->creAnims[stackID]->getType() != group)
		owner->creAnims[stackID]->setType(group);

	if(owner->creAnims[stackID]->getFrame() == 0)
	{
		if(shooting)
		{
			// TODO: I see that we enter this function twice with
			// attackingInfo->frame==0, so all the inits are done
			// twice. The following is just a workaround until
			// that is fixed. Once done, we can get rid of
			// sh
			if (sh == -1)
				sh = CGI->soundh->playSound(attackingStack->creature->sounds.shoot);
			owner->creAnims[stackID]->setType(group);
		}
		else
		{
			// TODO: see comment above
			if (sh == -1)
				sh = CGI->soundh->playSound(attackingStack->creature->sounds.attack);

			static std::map<int, int> dirToType = boost::assign::map_list_of (0, 11)(1, 11)(2, 12)(3, 13)(4, 13)(5, 12);
			int type; //dependent on attack direction
			if(attackingStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
			{
				type = dirToType[ BattleInfo::mutualPosition(attackingStackPosBeforeReturn + posShiftDueToDist, dest) ]; //attack direction
			}
			else //else for if(aStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
			{
				type = BattleInfo::mutualPosition(attackingStackPosBeforeReturn, dest);
			}
			owner->creAnims[stackID]->setType(type);
		}
	}
	else if(owner->creAnims[stackID]->onLastFrameInGroup())
	{
		owner->creAnims[stackID]->setType(2);
		endAnim();
		return; //execution of endAnim deletes this !!!
	}
}

bool CBattleAttack::checkInitialConditions()
{
	return isEarliest(false);
}

CBattleAttack::CBattleAttack(CBattleInterface * _owner, int _stackID, int _dest)
: CBattleStackAnimation(_owner, _stackID), sh(-1), dest(_dest)
{
	attackedStack = LOCPLINT->cb->battleGetStackByPos(_dest, false);
	attackingStack = LOCPLINT->cb->battleGetStackByID(_stackID, false);
	attackingStackPosBeforeReturn = attackingStack->position;
}

////melee attack

bool CMeleeAttack::init()
{
	if( !CBattleAttack::checkInitialConditions() )
		return false;

	//if(owner->creAnims[stackID]->getType()!=2)
	//{
	//	return false;
	//}

	if(!attackingStack || owner->creAnims[stackID]->getType() == 5)
	{
		endAnim();
		
		return false;
	}

	int reversedShift = 0; //shift of attacking stack's position due to reversing
	if(attackingStack->attackerOwned)
	{
		if(attackingStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE) && BattleInfo::mutualPosition(attackingStackPosBeforeReturn, dest) == -1)
		{
			if(BattleInfo::mutualPosition(attackingStackPosBeforeReturn + (attackingStack->attackerOwned ? -1 : 1), dest) >= 0) //if reversing stack will make its position adjacent to dest
			{
				reversedShift = (attackingStack->attackerOwned ? -1 : 1);
			}
		}
	}
	else //if(astack->attackerOwned)
	{
		if(attackingStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE) && BattleInfo::mutualPosition(attackingStackPosBeforeReturn, dest) == -1)
		{
			if(BattleInfo::mutualPosition(attackingStackPosBeforeReturn + (attackingStack->attackerOwned ? -1 : 1), dest) >= 0) //if reversing stack will make its position adjacent to dest
			{
				reversedShift = (attackingStack->attackerOwned ? -1 : 1);
			}
		}

	}

	//reversing stack if necessary
	if(isToReverse(attackingStackPosBeforeReturn, dest, owner->creDir[stackID], attackedStack->hasFeatureOfType(StackFeature::DOUBLE_WIDE), owner->creDir[attackedStack->ID]))
	{
		owner->addNewAnim(new CReverseAnim(owner, stackID, attackingStackPosBeforeReturn, true));
		return false;
	}
	//reversed

	IDby = attackedStack->ID;
	shooting = false;
	posShiftDueToDist = reversedShift;

	static const int mutPosToGroup[] = {11, 11, 12, 13, 13, 12};

	int mutPos = BattleInfo::mutualPosition(attackingStackPosBeforeReturn + reversedShift, dest);
	switch(mutPos) //attack direction
	{
	case 0: case 1: case 2: case 3: case 4: case 5:
		group = mutPosToGroup[mutPos];
		break;
	default:
		tlog1<<"Critical Error! Wrong dest in stackAttacking! dest: "<<dest<<" attacking stack pos: "<<attackingStackPosBeforeReturn<<" reversed shift: "<<reversedShift<<std::endl;
	}

	return true;
}

void CMeleeAttack::nextFrame()
{
	/*for(std::list<std::pair<CBattleAnimation *, bool> >::const_iterator it = owner->pendingAnims.begin(); it != owner->pendingAnims.end(); ++it)
	{
		CBattleMoveStart * anim = dynamic_cast<CBattleMoveStart *>(it->first);
		CReverseAnim * anim2 = dynamic_cast<CReverseAnim *>(it->first);
		if( (anim && anim->stackID == stackID) || (anim2 && anim2->stackID == stackID ) )
			return;
	}*/

	CBattleAttack::nextFrame();
}

void CMeleeAttack::endAnim()
{
	CBattleAnimation::endAnim();

	delete this;
}

CMeleeAttack::CMeleeAttack(CBattleInterface * _owner, int attacker, int _dest)
: CBattleAttack(_owner, attacker, _dest)
{
}

//shooting anim

bool CShootingAnim::init()
{
	if( !CBattleAttack::checkInitialConditions() )
		return false;

	const CStack * shooter = attackingStack;

	if(!shooter || owner->creAnims[stackID]->getType() == 5)
	{
		endAnim();
		return false;
	}

	//projectile
	float projectileAngle; //in radians; if positive, projectiles goes up
	float straightAngle = 0.2f; //maximal angle in radians between straight horizontal line and shooting line for which shot is considered to be straight (absoulte value)
	int fromHex = shooter->position;
	projectileAngle = atan2(float(abs(dest - fromHex)/BFIELD_WIDTH), float(abs(dest - fromHex)%BFIELD_WIDTH));
	if(fromHex < dest)
		projectileAngle = -projectileAngle;

	SProjectileInfo spi;
	spi.creID = shooter->creature->idNumber;
	spi.reverse = !shooter->attackerOwned;

	spi.step = 0;
	spi.frameNum = 0;
	spi.spin = CGI->creh->idToProjectileSpin[spi.creID];

	Point xycoord = CBattleHex::getXYUnitAnim(shooter->position, true, shooter, owner);
	Point destcoord = CBattleHex::getXYUnitAnim(dest, false, attackedStack, owner); 
	destcoord.x += 250; destcoord.y += 210; //TODO: find a better place to shoot

	if(projectileAngle > straightAngle) //upper shot
	{
		spi.x = xycoord.x + 200 + shooter->creature->upperRightMissleOffsetX;
		spi.y = xycoord.y + 100 - shooter->creature->upperRightMissleOffsetY;
	}
	else if(projectileAngle < -straightAngle) //lower shot
	{
		spi.x = xycoord.x + 200 + shooter->creature->lowerRightMissleOffsetX;
		spi.y = xycoord.y + 150 - shooter->creature->lowerRightMissleOffsetY;
	}
	else //straight shot
	{
		spi.x = xycoord.x + 200 + shooter->creature->rightMissleOffsetX;
		spi.y = xycoord.y + 125 - shooter->creature->rightMissleOffsetY;
	}
	spi.lastStep = sqrt((float)((destcoord.x - spi.x)*(destcoord.x - spi.x) + (destcoord.y - spi.y) * (destcoord.y - spi.y))) / 40;
	if(spi.lastStep == 0)
		spi.lastStep = 1;
	spi.dx = (destcoord.x - spi.x) / spi.lastStep;
	spi.dy = (destcoord.y - spi.y) / spi.lastStep;
	//set starting frame
	if(spi.spin)
	{
		spi.frameNum = 0;
	}
	else
	{
		spi.frameNum = ((M_PI/2.0f - projectileAngle) / (2.0f *M_PI) + 1/((float)(2*(owner->idToProjectile[spi.creID]->ourImages.size()-1)))) * (owner->idToProjectile[spi.creID]->ourImages.size()-1);
	}
	//set delay
	spi.animStartDelay = CGI->creh->creatures[spi.creID].attackClimaxFrame;
	owner->projectiles.push_back(spi);

	//attack aniamtion
	IDby = attackedStack->ID;
	posShiftDueToDist = 0;
	shooting = true;

	if(projectileAngle > straightAngle) //upper shot
		group = 14;
	else if(projectileAngle < -straightAngle) //lower shot
		group = 16;
	else //straight shot
		group = 15;

	return true;
}

void CShootingAnim::nextFrame()
{
	for(std::list<std::pair<CBattleAnimation *, bool> >::const_iterator it = owner->pendingAnims.begin(); it != owner->pendingAnims.end(); ++it)
	{
		CBattleMoveStart * anim = dynamic_cast<CBattleMoveStart *>(it->first);
		CReverseAnim * anim2 = dynamic_cast<CReverseAnim *>(it->first);
		if( (anim && anim->stackID == stackID) || (anim2 && anim2->stackID == stackID && anim2->priority ) )
			return;
	}

	CBattleAttack::nextFrame();
}

void CShootingAnim::endAnim()
{
	CBattleAnimation::endAnim();

	delete this;
}

CShootingAnim::CShootingAnim(CBattleInterface * _owner, int attacker, int _dest)
: CBattleAttack(_owner, attacker, _dest)
{
}

////////////////////////

void CBattleInterface::addNewAnim(CBattleAnimation * anim)
{
	pendingAnims.push_back( std::make_pair(anim, false) );
	animsAreDisplayed.setn(true);
}

CBattleInterface::CBattleInterface(CCreatureSet * army1, CCreatureSet * army2, CGHeroInstance *hero1, CGHeroInstance *hero2, const SDL_Rect & myRect)
	: attackingHeroInstance(hero1), defendingHeroInstance(hero2), animCount(0), activeStack(-1), stackToActivate(-1),
	  mouseHoveredStack(-1), previouslyHoveredHex(-1), currentlyHoveredHex(-1), spellDestSelectMode(false),
	  spellToCast(NULL), givenCommand(NULL), myTurn(false), resWindow(NULL), animIDhelper(0),
	  moveStarted(false), moveSh(-1), siegeH(NULL), bresult(NULL), queue(NULL)
{
	ObjectConstruction h__l__p(this);

	animsAreDisplayed.setn(false);
	pos = myRect;
	strongInterest = true;
	givenCommand = new CondSh<BattleAction *>(NULL);
	
	//create stack queue
	bool embedQueue = screen->h < 700;
	queue = new CStackQueue(embedQueue);
 	if(!embedQueue && settings.showQueue)
	{
		pos.y += queue->pos.h / 2; //center whole window
		queue->moveTo(Point(pos.x, pos.y - queue->pos.h));
// 		queue->pos.x = pos.x;
// 		queue->pos.y = pos.y - queue->pos.h;
//  		pos.h += queue->pos.h;
//  		center();
 	}
	queue->update();

	//preparing siege info
	const CGTownInstance * town = LOCPLINT->cb->battleGetDefendedTown();
	if(town)
	{
		siegeH = new SiegeHelper(town, this);
	}

	LOCPLINT->battleInt = this;

	//initializing armies
	this->army1 = army1;
	this->army2 = army2;
	std::map<int, CStack> stacks = LOCPLINT->cb->battleGetStacks();
	for(std::map<int, CStack>::iterator b=stacks.begin(); b!=stacks.end(); ++b)
	{
		newStack(b->second.ID);
	}

	//preparing menu background and terrain
	if(siegeH)
	{
		background = BitmapHandler::loadBitmap( siegeH->getSiegeName(0), false );
		ui8 siegeLevel = LOCPLINT->cb->battleGetSiegeLevel();
		if(siegeLevel >= 2) //citadel or castle
		{
			//print moat/mlip
			SDL_Surface * moat = BitmapHandler::loadBitmap( siegeH->getSiegeName(13) ),
				* mlip = BitmapHandler::loadBitmap( siegeH->getSiegeName(14) );

			std::pair<int, int> moatPos = CGI->heroh->wallPositions[siegeH->town->town->typeID][10],
				mlipPos = CGI->heroh->wallPositions[siegeH->town->town->typeID][11];

			if(moat) //eg. tower has no moat
				blitAt(moat, moatPos.first,moatPos.second, background);
			if(mlip) //eg. tower has no mlip
				blitAt(mlip, mlipPos.first, mlipPos.second, background);

			SDL_FreeSurface(moat);
			SDL_FreeSurface(mlip);
		}
	}
	else
	{
		std::vector< std::string > & backref = graphics->battleBacks[ LOCPLINT->cb->battleGetBattlefieldType() ];
		background = BitmapHandler::loadBitmap(backref[ rand() % backref.size()], false );
	}
	
	//preparing menu background
	menu = BitmapHandler::loadBitmap("CBAR.BMP");
	graphics->blueToPlayersAdv(menu, hero1->tempOwner);

	//preparing graphics for displaying amounts of creatures
	amountNormal = BitmapHandler::loadBitmap("CMNUMWIN.BMP");
	CSDL_Ext::alphaTransform(amountNormal);
	transformPalette(amountNormal, 0.59f, 0.19f, 0.93f);
	
	amountPositive = BitmapHandler::loadBitmap("CMNUMWIN.BMP");
	CSDL_Ext::alphaTransform(amountPositive);
	transformPalette(amountPositive, 0.18f, 1.00f, 0.18f);

	amountNegative = BitmapHandler::loadBitmap("CMNUMWIN.BMP");
	CSDL_Ext::alphaTransform(amountNegative);
	transformPalette(amountNegative, 1.00f, 0.18f, 0.18f);

	amountEffNeutral = BitmapHandler::loadBitmap("CMNUMWIN.BMP");
	CSDL_Ext::alphaTransform(amountEffNeutral);
	transformPalette(amountEffNeutral, 1.00f, 1.00f, 0.18f);

	////blitting menu background and terrain
	blitAt(background, pos.x, pos.y);
	blitAt(menu, pos.x, 556 + pos.y);
	CSDL_Ext::update();

	//preparing buttons and console
	bOptions = new AdventureMapButton (CGI->generaltexth->zelp[381].first, CGI->generaltexth->zelp[381].second, boost::bind(&CBattleInterface::bOptionsf,this), 3 + pos.x, 561 + pos.y, "icm003.def", SDLK_o);
	bSurrender = new AdventureMapButton (CGI->generaltexth->zelp[379].first, CGI->generaltexth->zelp[379].second, boost::bind(&CBattleInterface::bSurrenderf,this), 54 + pos.x, 561 + pos.y, "icm001.def", SDLK_s);
	bFlee = new AdventureMapButton (CGI->generaltexth->zelp[380].first, CGI->generaltexth->zelp[380].second, boost::bind(&CBattleInterface::bFleef,this), 105 + pos.x, 561 + pos.y, "icm002.def", SDLK_r);
	bAutofight  = new AdventureMapButton (CGI->generaltexth->zelp[382].first, CGI->generaltexth->zelp[382].second, boost::bind(&CBattleInterface::bAutofightf,this), 157 + pos.x, 561 + pos.y, "icm004.def", SDLK_a);
	bSpell = new AdventureMapButton (CGI->generaltexth->zelp[385].first, CGI->generaltexth->zelp[385].second, boost::bind(&CBattleInterface::bSpellf,this), 645 + pos.x, 561 + pos.y, "icm005.def", SDLK_c);
	bWait = new AdventureMapButton (CGI->generaltexth->zelp[386].first, CGI->generaltexth->zelp[386].second, boost::bind(&CBattleInterface::bWaitf,this), 696 + pos.x, 561 + pos.y, "icm006.def", SDLK_w);
	bDefence = new AdventureMapButton (CGI->generaltexth->zelp[387].first, CGI->generaltexth->zelp[387].second, boost::bind(&CBattleInterface::bDefencef,this), 747 + pos.x, 561 + pos.y, "icm007.def", SDLK_d);
	bDefence->assignedKeys.insert(SDLK_SPACE);
	bConsoleUp = new AdventureMapButton (std::string(), std::string(), boost::bind(&CBattleInterface::bConsoleUpf,this), 624 + pos.x, 561 + pos.y, "ComSlide.def", SDLK_UP);
	bConsoleDown = new AdventureMapButton (std::string(), std::string(), boost::bind(&CBattleInterface::bConsoleDownf,this), 624 + pos.x, 580 + pos.y, "ComSlide.def", SDLK_DOWN);
	bConsoleDown->bitmapOffset = 2;
	console = new CBattleConsole();
	console->pos.x = 211 + pos.x;
	console->pos.y = 560 + pos.y;
	console->pos.w = 406;
	console->pos.h = 38;

	//loading hero animations
	if(hero1) // attacking hero
	{
		attackingHero = new CBattleHero(graphics->battleHeroes[hero1->type->heroType], 0, 0, false, hero1->tempOwner, hero1->tempOwner == LOCPLINT->playerID ? hero1 : NULL, this);
		attackingHero->pos = genRect(attackingHero->dh->ourImages[0].bitmap->h, attackingHero->dh->ourImages[0].bitmap->w, -40 + pos.x, pos.y);
	}
	else
	{
		attackingHero = NULL;
	}
	if(hero2) // defending hero
	{
		defendingHero = new CBattleHero(graphics->battleHeroes[hero2->type->heroType], 0, 0, true, hero2->tempOwner, hero2->tempOwner == LOCPLINT->playerID ? hero2 : NULL, this);
		defendingHero->pos = genRect(defendingHero->dh->ourImages[0].bitmap->h, defendingHero->dh->ourImages[0].bitmap->w, 690 + pos.x, pos.y);
	}
	else
	{
		defendingHero = NULL;
	}

	//preparing cells and hexes
	cellBorder = BitmapHandler::loadBitmap("CCELLGRD.BMP");
	CSDL_Ext::alphaTransform(cellBorder);
	cellShade = BitmapHandler::loadBitmap("CCELLSHD.BMP");
	CSDL_Ext::alphaTransform(cellShade);
	for(int h=0; h<BFIELD_SIZE; ++h)
	{
		bfield[h].myNumber = h;

		int x = 14 + ((h/BFIELD_WIDTH)%2==0 ? 22 : 0) + 44*(h%BFIELD_WIDTH);
		int y = 86 + 42 * (h/BFIELD_WIDTH);
		bfield[h].pos = genRect(cellShade->h, cellShade->w, x + pos.x, y + pos.y);
		bfield[h].accesible = true;
		bfield[h].myInterface = this;
	}
	//locking occupied positions on batlefield
	for(std::map<int, CStack>::iterator it = stacks.begin(); it!=stacks.end(); ++it) //stacks gained at top of this function
	{
		if(it->second.position >= 0) //turrets have position < 0
			bfield[it->second.position].accesible = false;
	}

	//loading projectiles for units
	for(std::map<int, CStack>::iterator g = stacks.begin(); g != stacks.end(); ++g)
	{
		int creID = (g->second.creature->idNumber == 149) ? CGI->creh->factionToTurretCreature[siegeH->town->town->typeID] : g->second.creature->idNumber; //id of creature whose shots should be loaded
		if(g->second.creature->isShooting() && CGI->creh->idToProjectile[creID] != std::string())
		{	
			idToProjectile[g->second.creature->idNumber] = CDefHandler::giveDef(CGI->creh->idToProjectile[creID]);

			if(idToProjectile[g->second.creature->idNumber]->ourImages.size() > 2) //add symmetric images
			{
				for(int k = idToProjectile[g->second.creature->idNumber]->ourImages.size()-2; k > 1; --k)
				{
					Cimage ci;
					ci.bitmap = CSDL_Ext::rotate01(idToProjectile[g->second.creature->idNumber]->ourImages[k].bitmap);
					ci.groupNumber = 0;
					ci.imName = std::string();
					idToProjectile[g->second.creature->idNumber]->ourImages.push_back(ci);
				}
			}
			for(int s=0; s<idToProjectile[g->second.creature->idNumber]->ourImages.size(); ++s) //alpha transforming
			{
				CSDL_Ext::alphaTransform(idToProjectile[g->second.creature->idNumber]->ourImages[s].bitmap);
			}
		}
	}


	//preparing graphic with cell borders
	cellBorders = CSDL_Ext::newSurface(background->w, background->h, cellBorder);
	//copying palette
	for(int g=0; g<cellBorder->format->palette->ncolors; ++g) //we assume that cellBorders->format->palette->ncolors == 256
	{
		cellBorders->format->palette->colors[g] = cellBorder->format->palette->colors[g];
	}
	//palette copied
	for(int i=0; i<BFIELD_HEIGHT; ++i) //rows
	{
		for(int j=0; j<BFIELD_WIDTH-2; ++j) //columns
		{
			int x = 58 + (i%2==0 ? 22 : 0) + 44*j;
			int y = 86 + 42 * i;
			for(int cellX = 0; cellX < cellBorder->w; ++cellX)
			{
				for(int cellY = 0; cellY < cellBorder->h; ++cellY)
				{
					if(y+cellY < cellBorders->h && x+cellX < cellBorders->w)
						* ((Uint8*)cellBorders->pixels + (y+cellY) * cellBorders->pitch + (x+cellX)) |= * ((Uint8*)cellBorder->pixels + cellY * cellBorder->pitch + cellX);
				}
			}
		}
	}

	backgroundWithHexes = CSDL_Ext::newSurface(background->w, background->h, screen);

	//preparing obstacle defs
	std::vector<CObstacleInstance> obst = LOCPLINT->cb->battleGetAllObstacles();
	for(int t=0; t<obst.size(); ++t)
	{
		idToObstacle[obst[t].ID] = CDefHandler::giveDef(CGI->heroh->obstacles[obst[t].ID].defName);
		for(int n=0; n<idToObstacle[obst[t].ID]->ourImages.size(); ++n)
		{
			SDL_SetColorKey(idToObstacle[obst[t].ID]->ourImages[n].bitmap, SDL_SRCCOLORKEY, SDL_MapRGB(idToObstacle[obst[t].ID]->ourImages[n].bitmap->format,0,255,255));
		}
	}

	for (int i = 0; i < ARRAY_COUNT(bfield); i++)
	{
		children.push_back(&bfield[i]);
	}
}

CBattleInterface::~CBattleInterface()
{
	SDL_FreeSurface(background);
	SDL_FreeSurface(menu);
	SDL_FreeSurface(amountNormal);
	SDL_FreeSurface(amountNegative);
	SDL_FreeSurface(amountPositive);
	SDL_FreeSurface(amountEffNeutral);
	SDL_FreeSurface(cellBorders);
	SDL_FreeSurface(backgroundWithHexes);
	delete bOptions;
	delete bSurrender;
	delete bFlee;
	delete bAutofight;
	delete bSpell;
	delete bWait;
	delete bDefence;
	delete bConsoleUp;
	delete bConsoleDown;
	delete console;
	delete givenCommand;

	delete attackingHero;
	delete defendingHero;
	delete queue;

	SDL_FreeSurface(cellBorder);
	SDL_FreeSurface(cellShade);

	for(std::map< int, CCreatureAnimation * >::iterator g=creAnims.begin(); g!=creAnims.end(); ++g)
		delete g->second;

	for(std::map< int, CDefHandler * >::iterator g=idToProjectile.begin(); g!=idToProjectile.end(); ++g)
		delete g->second;

	for(std::map< int, CDefHandler * >::iterator g=idToObstacle.begin(); g!=idToObstacle.end(); ++g)
		delete g->second;

	delete siegeH;
	LOCPLINT->battleInt = NULL;
}

void CBattleInterface::setPrintCellBorders(bool set)
{
	settings.printCellBorders = set;
	redrawBackgroundWithHexes(activeStack);
	GH.totalRedraw();
}

void CBattleInterface::setPrintStackRange(bool set)
{
	settings.printStackRange = set;
	redrawBackgroundWithHexes(activeStack);
	GH.totalRedraw();
}

void CBattleInterface::setPrintMouseShadow(bool set)
{
	settings.printMouseShadow = set;
}

void CBattleInterface::activate()
{
	activateKeys();
	activateMouseMove();
	activateRClick();
	bOptions->activate();
	bSurrender->activate();
	bFlee->activate();
	bAutofight->activate();
	bSpell->activate();
	bWait->activate();
	bDefence->activate();
	bConsoleUp->activate();
	bConsoleDown->activate();
	for(int b=0; b<BFIELD_SIZE; ++b)
	{
		bfield[b].activate();
	}
	if(attackingHero)
		attackingHero->activate();
	if(defendingHero)
		defendingHero->activate();
	if(settings.showQueue)
		queue->activate();

	LOCPLINT->cingconsole->activate();
}

void CBattleInterface::deactivate()
{
	deactivateKeys();
	deactivateMouseMove();
	deactivateRClick();
	bOptions->deactivate();
	bSurrender->deactivate();
	bFlee->deactivate();
	bAutofight->deactivate();
	bSpell->deactivate();
	bWait->deactivate();
	bDefence->deactivate();
	bConsoleUp->deactivate();
	bConsoleDown->deactivate();
	for(int b=0; b<BFIELD_SIZE; ++b)
	{
		bfield[b].deactivate();
	}
	if(attackingHero)
		attackingHero->deactivate();
	if(defendingHero)
		defendingHero->deactivate();
	if(settings.showQueue)
		queue->deactivate();

	LOCPLINT->cingconsole->deactivate();
}

void CBattleInterface::show(SDL_Surface * to)
{
	std::map<int, CStack> stacks = LOCPLINT->cb->battleGetStacks(); //used in a few places
	++animCount;
	if(!to) //"evaluating" to
		to = screen;
	
	SDL_Rect buf;
	SDL_GetClipRect(to, &buf);
	SDL_SetClipRect(to, &pos);

	//printing background and hexes
	if(activeStack != -1 && creAnims[activeStack]->getType() != 0) //show everything with range
	{
		blitAt(backgroundWithHexes, pos.x, pos.y, to);
	}
	else
	{
		//showing background
		blitAt(background, pos.x, pos.y, to);
		if(settings.printCellBorders)
		{
			CSDL_Ext::blit8bppAlphaTo24bpp(cellBorders, NULL, to, &pos);
		}
	}
	//printing hovered cell
	for(int b=0; b<BFIELD_SIZE; ++b)
	{
		if(bfield[b].strictHovered && bfield[b].hovered)
		{
			if(previouslyHoveredHex == -1) previouslyHoveredHex = b; //something to start with
			if(currentlyHoveredHex == -1) currentlyHoveredHex = b; //something to start with
			if(currentlyHoveredHex != b) //repair hover info
			{
				previouslyHoveredHex = currentlyHoveredHex;
				currentlyHoveredHex = b;
			}
			//print shade
			if(spellToCast) //when casting spell
			{
				//calculating spell schoold level
				const CSpell & spToCast =  CGI->spellh->spells[spellToCast->additionalInfo];
				ui8 schoolLevel = 0;
				if( LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned )
				{
					if(attackingHeroInstance)
						schoolLevel = attackingHeroInstance->getSpellSchoolLevel(&spToCast);
				}
				else
				{
					if(defendingHeroInstance)
						schoolLevel = defendingHeroInstance->getSpellSchoolLevel(&spToCast);
				}
				//obtaining range and printing it
				std::set<ui16> shaded = spToCast.rangeInHexes(b, schoolLevel);
				for(std::set<ui16>::iterator it = shaded.begin(); it != shaded.end(); ++it) //for spells with range greater then one hex
				{
					if(settings.printMouseShadow && (*it % BFIELD_WIDTH != 0) && (*it % BFIELD_WIDTH != 16))
					{
						int x = 14 + ((*it/BFIELD_WIDTH)%2==0 ? 22 : 0) + 44*(*it%BFIELD_WIDTH) + pos.x;
						int y = 86 + 42 * (*it/BFIELD_WIDTH) + pos.y;
						CSDL_Ext::blit8bppAlphaTo24bpp(cellShade, NULL, to, &genRect(cellShade->h, cellShade->w, x, y));
					}
				}
			}
			else if(settings.printMouseShadow) //when not casting spell
			{
				int x = 14 + ((b/BFIELD_WIDTH)%2==0 ? 22 : 0) + 44*(b%BFIELD_WIDTH) + pos.x;
				int y = 86 + 42 * (b/BFIELD_WIDTH) + pos.y;
				CSDL_Ext::blit8bppAlphaTo24bpp(cellShade, NULL, to, &genRect(cellShade->h, cellShade->w, x, y));
			}
		}
	}

	
	SDL_SetClipRect(to, &buf); //restoring previous clip_rect

	//showing buttons
	bOptions->show(to);
	bSurrender->show(to);
	bFlee->show(to);
	bAutofight->show(to);
	bSpell->show(to);
	bWait->show(to);
	bDefence->show(to);
	bConsoleUp->show(to);
	bConsoleDown->show(to);

	//prevents blitting outside this window
	SDL_GetClipRect(to, &buf);
	SDL_SetClipRect(to, &pos);

	//showing obstacles
	std::vector<CObstacleInstance> obstacles = LOCPLINT->cb->battleGetAllObstacles();
	for(int b=0; b<obstacles.size(); ++b)
	{
		std::pair<si16, si16> shift = CGI->heroh->obstacles[obstacles[b].ID].posShift;
		int x = ((obstacles[b].pos/BFIELD_WIDTH)%2==0 ? 22 : 0) + 44*(obstacles[b].pos%BFIELD_WIDTH) + pos.x + shift.first;
		int y = 86 + 42 * (obstacles[b].pos/BFIELD_WIDTH) + pos.y + shift.second;
		std::vector<Cimage> &images = idToObstacle[obstacles[b].ID]->ourImages; //reference to animation of obstacle
		blitAt(images[((animCount+1)/(4/settings.animSpeed))%images.size()].bitmap, x, y, to);
	}

	//showing hero animations
	if(attackingHero)
		attackingHero->show(to);
	if(defendingHero)
		defendingHero->show(to);

	////showing units //a lot of work...
	std::vector<int> stackAliveByHex[BFIELD_SIZE];
	//double loop because dead stacks should be printed first
	for(std::map<int, CStack>::iterator j=stacks.begin(); j!=stacks.end(); ++j)
	{
		if(creAnims[j->second.ID]->getType() != 5 && j->second.position >= 0) //don't show turrets here
			stackAliveByHex[j->second.position].push_back(j->second.ID);
	}
	std::vector<int> stackDeadByHex[BFIELD_SIZE];
	for(std::map<int, CStack>::iterator j=stacks.begin(); j!=stacks.end(); ++j)
	{
		if(creAnims[j->second.ID]->getType() == 5)
			stackDeadByHex[j->second.position].push_back(j->second.ID);
	}

	//handle animations
	for(std::list<std::pair<CBattleAnimation *, bool> >::iterator it = pendingAnims.begin(); it != pendingAnims.end(); ++it)
	{
		if(!it->first) //this animation should be deleted
			continue;

		if(!it->second)
		{
			it->second = it->first->init();
		}
		if(it->second && it->first)
			it->first->nextFrame();
	}

	//delete anims
	int preSize = pendingAnims.size();
	for(std::list<std::pair<CBattleAnimation *, bool> >::iterator it = pendingAnims.begin(); it != pendingAnims.end(); ++it)
	{
		if(it->first == NULL)
		{
			pendingAnims.erase(it);
			it = pendingAnims.begin();
			break;
		}
	}

	if(preSize > 0 && pendingAnims.size() == 0)
	{
		//restoring good directions of stacks
		for(std::map<int, CStack>::const_iterator it = stacks.begin(); it != stacks.end(); ++it)
		{
			if(creDir[it->second.ID] != bool(it->second.attackerOwned) && it->second.alive())
			{
				addNewAnim(new CReverseAnim(this, it->second.ID, it->second.position, false));
			}
		}

		//activation of next stack
		if(pendingAnims.size() == 0 && stackToActivate != -1)
		{
			activateStack();
		}
		//anims ended
		animsAreDisplayed.setn(false);
	}

	for(int b=0; b<BFIELD_SIZE; ++b) //showing dead stacks
	{
		for(size_t v=0; v<stackDeadByHex[b].size(); ++v)
		{
			creAnims[stackDeadByHex[b][v]]->nextFrame(to, creAnims[stackDeadByHex[b][v]]->pos.x, creAnims[stackDeadByHex[b][v]]->pos.y, creDir[stackDeadByHex[b][v]], animCount, false); //increment always when moving, never if stack died
		}
	}
	for(int b=0; b<BFIELD_SIZE; ++b) //showing alive stacks
	{
		for(size_t v=0; v<stackAliveByHex[b].size(); ++v)
		{
			int curStackID = stackAliveByHex[b][v];
			
			showAliveStack(stackAliveByHex[b][v], stacks, to);
		}

		showPieceOfWall(to, b, stacks);
	}
	//units shown
	projectileShowHelper(to);//showing projectiles

	//showing spell effects
	if(battleEffects.size())
	{
		for(std::list<SBattleEffect>::iterator it = battleEffects.begin(); it!=battleEffects.end(); ++it)
		{
			SDL_Surface * bitmapToBlit = it->anim->ourImages[(it->frame)%it->anim->ourImages.size()].bitmap;
			SDL_BlitSurface(bitmapToBlit, NULL, to, &genRect(bitmapToBlit->h, bitmapToBlit->w, it->x, it->y));
		}
	}

	SDL_SetClipRect(to, &buf); //restoring previous clip_rect

	//showing menu background and console
	blitAt(menu, pos.x, 556 + pos.y, to);
	console->show(to);

	//showing window with result of battle
	if(resWindow)
	{
		resWindow->show(to);
	}

	//showing in-game console
	LOCPLINT->cingconsole->show(to);

	Rect posWithQueue = Rect(pos.x, pos.y, 800, 600);

	if(settings.showQueue)
	{
		if(!queue->embedded)
		{
			posWithQueue.y -= queue->pos.h;
			posWithQueue.h += queue->pos.h;
		}

		//showing queue
		if(!bresult)
			queue->showAll(to);
		else
			queue->blitBg(to); //blit only background, stacks are deleted
	}

	//printing border around interface
	if(screen->w != 800 || screen->h !=600)
	{
		CMessage::drawBorder(LOCPLINT->playerID,to,posWithQueue.w + 28, posWithQueue.h + 28, posWithQueue.x-14, posWithQueue.y-15);
	}
}
void CBattleInterface::keyPressed(const SDL_KeyboardEvent & key)
{
	if(key.keysym.sym == SDLK_q && key.state == SDL_PRESSED)
	{
		if(settings.showQueue) //hide queue
			hideQueue();
		else
			showQueue();
	}
	else if(key.keysym.sym == SDLK_ESCAPE && spellDestSelectMode)
	{
		endCastingSpell();
	}
}
void CBattleInterface::mouseMoved(const SDL_MouseMotionEvent &sEvent)
{
	if(activeStack>=0 && !spellDestSelectMode)
	{
		mouseHoveredStack = -1;
		int myNumber = -1; //number of hovered tile
		for(int g=0; g<BFIELD_SIZE; ++g)
		{
			if(bfield[g].hovered && bfield[g].strictHovered)
			{
				myNumber = g;
				break;
			}
		}
		if(myNumber == -1)
		{
			CGI->curh->changeGraphic(1, 6);
			if(console->whoSetAlter == 0)
			{
				console->alterTxt = "";
			}
		}
		else
		{
			if(std::find(shadedHexes.begin(),shadedHexes.end(),myNumber) == shadedHexes.end())
			{
				const CStack *shere = LOCPLINT->cb->battleGetStackByPos(myNumber);
				const CStack *sactive = LOCPLINT->cb->battleGetStackByID(activeStack);
				if(shere)
				{
					if(shere->owner == LOCPLINT->playerID) //our stack
					{
						CGI->curh->changeGraphic(1,5);
						//setting console text
						char buf[500];
						sprintf(buf, CGI->generaltexth->allTexts[297].c_str(), shere->amount == 1 ? shere->creature->nameSing.c_str() : shere->creature->namePl.c_str());
						console->alterTxt = buf;
						console->whoSetAlter = 0;
						mouseHoveredStack = shere->ID;
						if(creAnims[shere->ID]->getType() == 2 && creAnims[shere->ID]->framesInGroup(1) > 0)
						{
							creAnims[shere->ID]->playOnce(1);
						}
					}
					else if(LOCPLINT->cb->battleCanShoot(activeStack,myNumber)) //we can shoot enemy
					{
						CGI->curh->changeGraphic(1,3);
						//setting console text
						char buf[500];
						//calculating esimated dmg
						std::pair<ui32, ui32> estimatedDmg = LOCPLINT->cb->battleEstimateDamage(sactive->ID, shere->ID);
						std::ostringstream estDmg;
						estDmg << estimatedDmg.first << " - " << estimatedDmg.second;
						//printing
						sprintf(buf, CGI->generaltexth->allTexts[296].c_str(), shere->amount == 1 ? shere->creature->nameSing.c_str() : shere->creature->namePl.c_str(), sactive->shots, estDmg.str().c_str());
						console->alterTxt = buf;
						console->whoSetAlter = 0;
					}
					else if(isTileAttackable(myNumber)) //available enemy (melee attackable)
					{
						CCursorHandler *cursor = CGI->curh;
						const CBattleHex &hoveredHex = bfield[myNumber];

						const double subdividingAngle = 2.0*M_PI/6.0; // Divide a hex into six sectors.
						const double hexMidX = hoveredHex.pos.x + hoveredHex.pos.w/2;
						const double hexMidY = hoveredHex.pos.y + hoveredHex.pos.h/2;
						const double cursorHexAngle = M_PI - atan2(hexMidY - cursor->ypos, cursor->xpos - hexMidX) + subdividingAngle/2;
						const double sector = fmod(cursorHexAngle/subdividingAngle, 6.0);
						const int zigzagCorrection = !((myNumber/BFIELD_WIDTH)%2); // Off-by-one correction needed to deal with the odd battlefield rows.

						std::vector<int> sectorCursor; // From left to bottom left.
						sectorCursor.push_back(8);
						sectorCursor.push_back(9);
						sectorCursor.push_back(10);
						sectorCursor.push_back(11);
						sectorCursor.push_back(12);
						sectorCursor.push_back(7);

						const bool doubleWide = LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::DOUBLE_WIDE);
						bool aboveAttackable = true, belowAttackable = true;

						// Exclude directions which cannot be attacked from.
						// Check to the left.
						if (myNumber%BFIELD_WIDTH <= 1 || !vstd::contains(shadedHexes, myNumber - 1)) {
							sectorCursor[0] = -1;
						}
						// Check top left, top right as well as above for 2-hex creatures.
						if (myNumber/BFIELD_WIDTH == 0) {
								sectorCursor[1] = -1;
								sectorCursor[2] = -1;
								aboveAttackable = false;
						} else {
							if (doubleWide) {
								bool attackRow[4] = {true, true, true, true};

								if (myNumber%BFIELD_WIDTH <= 1 || !vstd::contains(shadedHexes, myNumber - BFIELD_WIDTH - 2 + zigzagCorrection))
									attackRow[0] = false;
								if (!vstd::contains(shadedHexes, myNumber - BFIELD_WIDTH - 1 + zigzagCorrection))
									attackRow[1] = false;
								if (!vstd::contains(shadedHexes, myNumber - BFIELD_WIDTH + zigzagCorrection))
									attackRow[2] = false;
								if (myNumber%BFIELD_WIDTH >= BFIELD_WIDTH - 2 || !vstd::contains(shadedHexes, myNumber - BFIELD_WIDTH + 1 + zigzagCorrection))
									attackRow[3] = false;

								if (!(attackRow[0] && attackRow[1]))
									sectorCursor[1] = -1;
								if (!(attackRow[1] && attackRow[2]))
									aboveAttackable = false;
								if (!(attackRow[2] && attackRow[3]))
									sectorCursor[2] = -1;
							} else {
								if (!vstd::contains(shadedHexes, myNumber - BFIELD_WIDTH - 1 + zigzagCorrection))
									sectorCursor[1] = -1;
								if (!vstd::contains(shadedHexes, myNumber - BFIELD_WIDTH + zigzagCorrection))
									sectorCursor[2] = -1;
							}
						}
						// Check to the right.
						if (myNumber%BFIELD_WIDTH >= BFIELD_WIDTH - 2 || !vstd::contains(shadedHexes, myNumber + 1)) {
							sectorCursor[3] = -1;
						}
						// Check bottom right, bottom left as well as below for 2-hex creatures.
						if (myNumber/BFIELD_WIDTH == BFIELD_HEIGHT - 1) {
							sectorCursor[4] = -1;
							sectorCursor[5] = -1;
							belowAttackable = false;
						} else {
							if (doubleWide) {
								bool attackRow[4] = {true, true, true, true};

								if (myNumber%BFIELD_WIDTH <= 1 || !vstd::contains(shadedHexes, myNumber + BFIELD_WIDTH - 2 + zigzagCorrection))
									attackRow[0] = false;
								if (!vstd::contains(shadedHexes, myNumber + BFIELD_WIDTH - 1 + zigzagCorrection))
									attackRow[1] = false;
								if (!vstd::contains(shadedHexes, myNumber + BFIELD_WIDTH + zigzagCorrection))
									attackRow[2] = false;
								if (myNumber%BFIELD_WIDTH >= BFIELD_WIDTH - 2 || !vstd::contains(shadedHexes, myNumber + BFIELD_WIDTH + 1 + zigzagCorrection))
									attackRow[3] = false;

								if (!(attackRow[0] && attackRow[1]))
									sectorCursor[5] = -1;
								if (!(attackRow[1] && attackRow[2]))
									belowAttackable = false;
								if (!(attackRow[2] && attackRow[3]))
									sectorCursor[4] = -1;
							} else {
								if (!vstd::contains(shadedHexes, myNumber + BFIELD_WIDTH + zigzagCorrection))
									sectorCursor[4] = -1;
								if (!vstd::contains(shadedHexes, myNumber + BFIELD_WIDTH - 1 + zigzagCorrection))
									sectorCursor[5] = -1;
							}
						}

						// Determine index from sector.
						int cursorIndex;
						if (doubleWide) {
							sectorCursor.insert(sectorCursor.begin() + 5, belowAttackable ? 13 : -1);
							sectorCursor.insert(sectorCursor.begin() + 2, aboveAttackable ? 14 : -1);

							if (sector < 1.5)
								cursorIndex = sector;
							else if (sector >= 1.5 && sector < 2.5)
								cursorIndex = 2;
							else if (sector >= 2.5 && sector < 4.5)
								cursorIndex = (int) sector + 1;
							else if (sector >= 4.5 && sector < 5.5)
								cursorIndex = 6;
							else
								cursorIndex = (int) sector + 2;
						} else {
							cursorIndex = sector;
						}

						// Find the closest direction attackable, starting with the right one.
						// FIXME: Is this really how the original H3 client does it?
						int i = 0;
						while (sectorCursor[(cursorIndex + i)%sectorCursor.size()] == -1)
							i = i <= 0 ? 1 - i : -i; // 0, 1, -1, 2, -2, 3, -3 etc..
						cursor->changeGraphic(1, sectorCursor[(cursorIndex + i)%sectorCursor.size()]);

						//setting console info
						char buf[500];
						//calculating esimated dmg
						std::pair<ui32, ui32> estimatedDmg = LOCPLINT->cb->battleEstimateDamage(sactive->ID, shere->ID);
						std::ostringstream estDmg;
						estDmg << estimatedDmg.first << " - " << estimatedDmg.second;
						//printing
						sprintf(buf, CGI->generaltexth->allTexts[36].c_str(), shere->amount == 1 ? shere->creature->nameSing.c_str() : shere->creature->namePl.c_str(), estDmg.str().c_str());
						console->alterTxt = buf;
						console->whoSetAlter = 0;
					}
					else //unavailable enemy
					{
						CGI->curh->changeGraphic(1,0);
						console->alterTxt = "";
						console->whoSetAlter = 0;
					}
				}
				else if( sactive->hasFeatureOfType(StackFeature::CATAPULT) && isCatapultAttackable(myNumber) ) //catapulting
				{
					CGI->curh->changeGraphic(1,16);
					console->alterTxt = "";
					console->whoSetAlter = 0;
				}
				else //empty unavailable tile
				{
					CGI->curh->changeGraphic(1,0);
					console->alterTxt = "";
					console->whoSetAlter = 0;
				}
			}
			else //available tile
			{
				//setting console text and cursor
				const CStack *sactive = LOCPLINT->cb->battleGetStackByID(activeStack);
				char buf[500];
				if(LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::FLYING))
				{
					CGI->curh->changeGraphic(1,2);
					sprintf(buf, CGI->generaltexth->allTexts[295].c_str(), sactive->amount == 1 ? sactive->creature->nameSing.c_str() : sactive->creature->namePl.c_str());
				}
				else
				{
					CGI->curh->changeGraphic(1,1);
					sprintf(buf, CGI->generaltexth->allTexts[294].c_str(), sactive->amount == 1 ? sactive->creature->nameSing.c_str() : sactive->creature->namePl.c_str());
				}

				console->alterTxt = buf;
				console->whoSetAlter = 0;
			}
		}
	}
	else if(spellDestSelectMode)
	{
		int myNumber = -1; //number of hovered tile
		for(int g=0; g<BFIELD_SIZE; ++g)
		{
			if(bfield[g].hovered && bfield[g].strictHovered)
			{
				myNumber = g;
				break;
			}
		}
		if(myNumber == -1)
		{
			CGI->curh->changeGraphic(1, 0);
			//setting console text
			console->alterTxt = CGI->generaltexth->allTexts[23];
			console->whoSetAlter = 0;
		}
		else
		{
			//get dead stack if we cast resurrection or animate dead
			const CStack * stackUnder = LOCPLINT->cb->battleGetStackByPos(myNumber, spellToCast->additionalInfo != 38 && spellToCast->additionalInfo != 39);

			if(stackUnder && spellToCast->additionalInfo == 39 && !stackUnder->hasFeatureOfType(StackFeature::UNDEAD)) //animate dead can be cast only on undead creatures
				stackUnder = NULL;

			bool whichCase; //for cases 1, 2 and 3
			switch(spellSelMode)
			{
			case 1:
				whichCase = stackUnder && LOCPLINT->playerID == stackUnder->owner;
				break;
			case 2:
				whichCase = stackUnder && LOCPLINT->playerID != stackUnder->owner;
				break;
			case 3:
				whichCase = stackUnder;
				break;
			}

			switch(spellSelMode)
			{
			case 0:
				CGI->curh->changeGraphic(3, 0);
				//setting console text
				char buf[500];
				sprintf(buf, CGI->generaltexth->allTexts[26].c_str(), CGI->spellh->spells[spellToCast->additionalInfo].name.c_str());
				console->alterTxt = buf;
				console->whoSetAlter = 0;
				break;
			case 1: case 2: case 3:
				if( whichCase )
				{
					CGI->curh->changeGraphic(3, 0);
					//setting console text
					char buf[500];
					std::string creName = stackUnder->amount > 1 ? stackUnder->creature->namePl : stackUnder->creature->nameSing;
						sprintf(buf, CGI->generaltexth->allTexts[27].c_str(), CGI->spellh->spells[spellToCast->additionalInfo].name.c_str(), creName.c_str());
					console->alterTxt = buf;
					console->whoSetAlter = 0;
					break;
				}
				else
				{
					CGI->curh->changeGraphic(1, 0);
					//setting console text
					console->alterTxt = CGI->generaltexth->allTexts[23];
					console->whoSetAlter = 0;
				}
				break;
			case 4: //TODO: implement this case
				if( blockedByObstacle(myNumber) )
				{
					CGI->curh->changeGraphic(3, 0);
				}
				else
				{
					CGI->curh->changeGraphic(1, 0);
				}
				break;
			}
		}
	}
}

void CBattleInterface::clickRight(tribool down, bool previousState)
{
	if(!down && spellDestSelectMode)
	{
		endCastingSpell();
	}
}

void CBattleInterface::bOptionsf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	CGI->curh->changeGraphic(0,0);

	SDL_Rect temp_rect = genRect(431, 481, 160, 84);
	CBattleOptionsWindow * optionsWin = new CBattleOptionsWindow(temp_rect, this);
	GH.pushInt(optionsWin);
}

void CBattleInterface::bSurrenderf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;
}

void CBattleInterface::bFleef()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	if( LOCPLINT->cb->battleCanFlee() )
	{
		CFunctionList<void()> ony = boost::bind(&CBattleInterface::reallyFlee,this);
		LOCPLINT->showYesNoDialog(CGI->generaltexth->allTexts[28],std::vector<SComponent*>(), ony, 0, false);
	}
	else
	{
		std::vector<SComponent*> comps;
		std::string heroName;
		//calculating fleeing hero's name
		if(attackingHeroInstance)
			if(attackingHeroInstance->tempOwner == LOCPLINT->cb->getMyColor())
				heroName = attackingHeroInstance->name;
		if(defendingHeroInstance)
			if(defendingHeroInstance->tempOwner == LOCPLINT->cb->getMyColor())
				heroName = defendingHeroInstance->name;
		//calculating text
		char buffer[1000];
		sprintf(buffer, CGI->generaltexth->allTexts[340].c_str(), heroName.c_str());

		//printing message
		LOCPLINT->showInfoDialog(std::string(buffer), comps);
	}
}

void CBattleInterface::reallyFlee()
{
	giveCommand(4,0,0);
	CGI->curh->changeGraphic(0, 0);
}

void CBattleInterface::bAutofightf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;
}

void CBattleInterface::bSpellf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	CGI->curh->changeGraphic(0,0);

	const CGHeroInstance * chi = NULL;
	if(attackingHeroInstance->tempOwner == LOCPLINT->playerID)
		chi = attackingHeroInstance;
	else
		chi = defendingHeroInstance;
	CSpellWindow * spellWindow = new CSpellWindow(genRect(595, 620, (conf.cc.resx - 620)/2, (conf.cc.resy - 595)/2), chi);
	GH.pushInt(spellWindow);
}

void CBattleInterface::bWaitf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	if(activeStack != -1)
		giveCommand(8,0,activeStack);
}

void CBattleInterface::bDefencef()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	if(activeStack != -1)
 		giveCommand(3,0,activeStack);
}

void CBattleInterface::bConsoleUpf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	console->scrollUp();
}

void CBattleInterface::bConsoleDownf()
{
	if(spellDestSelectMode) //we are casting a spell
		return;

	console->scrollDown();
}

void CBattleInterface::newStack(int stackID)
{
	const CStack * newStack = LOCPLINT->cb->battleGetStackByID(stackID);

	Point coords = CBattleHex::getXYUnitAnim(newStack->position, newStack->owner == attackingHeroInstance->tempOwner, newStack, this);;

	if(newStack->position < 0) //turret
	{
		const CCreature & turretCreature = CGI->creh->creatures[ CGI->creh->factionToTurretCreature[siegeH->town->town->typeID] ];
		creAnims[stackID] = new CCreatureAnimation(turretCreature.animDefName);	
	}
	else
	{
		creAnims[stackID] = new CCreatureAnimation(newStack->creature->animDefName);	
	}
	creAnims[stackID]->setType(2);
	creAnims[stackID]->pos = Rect(coords.x, coords.y, creAnims[newStack->ID]->fullWidth, creAnims[newStack->ID]->fullHeight);
	creDir[stackID] = newStack->attackerOwned;
}

void CBattleInterface::stackRemoved(int stackID)
{
	delete creAnims[stackID];
	creAnims.erase(stackID);
	creDir.erase(stackID);
}

void CBattleInterface::stackActivated(int number)
{
	//givenCommand = NULL;
	stackToActivate = number;
	if(pendingAnims.size() == 0)
		activateStack();
}

void CBattleInterface::stackMoved(int number, int destHex, bool endMoving, int distance)
{
	addNewAnim(new CBattleStackMoved(this, number, destHex, endMoving, distance));
}

void CBattleInterface::stacksAreAttacked(std::vector<SStackAttackedInfo> attackedInfos)
{
	for(int h = 0; h < attackedInfos.size(); ++h)
	{
		addNewAnim(new CDefenceAnim(attackedInfos[h], this));
	}
}

void CBattleInterface::stackAttacking(int ID, int dest)
{
	addNewAnim(new CMeleeAttack(this, ID, dest));
}

void CBattleInterface::newRound(int number)
{
	console->addText(CGI->generaltexth->allTexts[412]);

	//unlock spellbook
	bSpell->block(!LOCPLINT->cb->battleCanCastSpell());

	//handle regeneration
	std::map<int, CStack> stacks = LOCPLINT->cb->battleGetStacks();
	for(std::map<int, CStack>::const_iterator it = stacks.begin(); it != stacks.end(); ++it)
	{
		if( it->second.hasFeatureOfType(StackFeature::HP_REGENERATION) )
			displayEffect(74, it->second.position);

		if( it->second.hasFeatureOfType(StackFeature::FULL_HP_REGENERATION, 0) )
			displayEffect(4, it->second.position);

		if( it->second.hasFeatureOfType(StackFeature::FULL_HP_REGENERATION, 1) )
			displayEffect(74, it->second.position);
	}
}

void CBattleInterface::giveCommand(ui8 action, ui16 tile, ui32 stack, si32 additional)
{
	if(!LOCPLINT->cb->battleGetStackByID(stack) && action != 1 && action != 4 && action != 5)
	{
		return;
	}
	BattleAction * ba = new BattleAction(); //is deleted in CPlayerInterface::activeStack()
	ba->side = defendingHeroInstance ? (LOCPLINT->playerID == defendingHeroInstance->tempOwner) : false;
	ba->actionType = action;
	ba->destinationTile = tile;
	ba->stackNumber = stack;
	ba->additionalInfo = additional;
	givenCommand->setn(ba);
	myTurn = false;
	activeStack = -1;
}

bool CBattleInterface::isTileAttackable(const int & number) const
{
	for(size_t b=0; b<shadedHexes.size(); ++b)
	{
		if(BattleInfo::mutualPosition(shadedHexes[b], number) != -1 || shadedHexes[b] == number)
			return true;
	}
	return false;
}

bool CBattleInterface::blockedByObstacle(int hex) const
{
	std::vector<CObstacleInstance> obstacles = LOCPLINT->cb->battleGetAllObstacles();
	std::set<int> coveredHexes;
	for(int b = 0; b < obstacles.size(); ++b)
	{
		std::vector<int> blocked = CGI->heroh->obstacles[obstacles[b].ID].getBlocked(obstacles[b].pos);
		for(int w = 0; w < blocked.size(); ++w)
			coveredHexes.insert(blocked[w]);
	}
	return vstd::contains(coveredHexes, hex);
}

bool CBattleInterface::isCatapultAttackable(int hex) const
{
	if(!siegeH)
		return false;

	int wallUnder = LOCPLINT->cb->battleGetWallUnderHex(hex);
	if(wallUnder == -1)
		return false;

	return LOCPLINT->cb->battleGetWallState(wallUnder) < 3;
}

void CBattleInterface::hexLclicked(int whichOne)
{
	const CStack * actSt = LOCPLINT->cb->battleGetStackByID(activeStack);
	if( ((whichOne%BFIELD_WIDTH)!=0 && (whichOne%BFIELD_WIDTH)!=(BFIELD_WIDTH-1)) //if player is trying to attack enemey unit or move creature stack
		|| (actSt->hasFeatureOfType(StackFeature::CATAPULT) && !spellDestSelectMode )
		)
	{
		if(!myTurn)
			return; //we are not permit to do anything
		if(spellDestSelectMode)
		{
			//checking destination
			bool allowCasting = true;
			bool onlyAlive = spellToCast->additionalInfo != 38 && spellToCast->additionalInfo != 39; //when casting resurrection or animate dead we should be allow to select dead stack
			switch(spellSelMode)
			{
			case 1:
				if(!LOCPLINT->cb->battleGetStackByPos(whichOne, onlyAlive) || LOCPLINT->playerID != LOCPLINT->cb->battleGetStackByPos(whichOne, onlyAlive)->owner )
					allowCasting = false;
				break;
			case 2:
				if(!LOCPLINT->cb->battleGetStackByPos(whichOne, onlyAlive) || LOCPLINT->playerID == LOCPLINT->cb->battleGetStackByPos(whichOne, onlyAlive)->owner )
					allowCasting = false;
				break;
			case 3:
				if(!LOCPLINT->cb->battleGetStackByPos(whichOne, onlyAlive))
					allowCasting = false;
				break;
			case 4:
				if(!blockedByObstacle(whichOne))
					allowCasting = false;
				break;
			}
			//destination checked
			if(allowCasting)
			{
				spellToCast->destinationTile = whichOne;
				LOCPLINT->cb->battleMakeAction(spellToCast);
				endCastingSpell();
			}
		}
		else //we don't cast any spell
		{
			const CStack* dest = LOCPLINT->cb->battleGetStackByPos(whichOne); //creature at destination tile; -1 if there is no one
			if(!dest || !dest->alive()) //no creature at that tile
			{
				const CStack * sactive = LOCPLINT->cb->battleGetStackByID(activeStack);
				if(std::find(shadedHexes.begin(),shadedHexes.end(),whichOne)!=shadedHexes.end())// and it's in our range
				{
					CGI->curh->changeGraphic(1, 6); //cursor should be changed
					if(LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
					{
						std::vector<int> acc = LOCPLINT->cb->battleGetAvailableHexes(activeStack, false);
						int shiftedDest = whichOne + (LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned ? 1 : -1);
						if(vstd::contains(acc, whichOne))
							giveCommand(2,whichOne,activeStack);
						else if(vstd::contains(acc, shiftedDest))
							giveCommand(2,shiftedDest,activeStack);
					}
					else
					{
						giveCommand(2,whichOne,activeStack);
					}
				}
				else if(sactive->hasFeatureOfType(StackFeature::CATAPULT) && isCatapultAttackable(whichOne)) //attacking (catapult)
				{
					giveCommand(9,whichOne,activeStack);
				}
			}
			else if(dest->owner != attackingHeroInstance->tempOwner
				&& LOCPLINT->cb->battleCanShoot(activeStack, whichOne) ) //shooting
			{
				CGI->curh->changeGraphic(1, 6); //cursor should be changed
				giveCommand(7,whichOne,activeStack);
			}
			else if(dest->owner != attackingHeroInstance->tempOwner) //attacking
			{
				const CStack * actStack = LOCPLINT->cb->battleGetStackByID(activeStack);
				int attackFromHex = -1; //hex from which we will attack chosen stack
				switch(CGI->curh->number)
				{
				case 12: //from bottom right
					{
						bool doubleWide = LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::DOUBLE_WIDE);
						int destHex = whichOne + ( (whichOne/BFIELD_WIDTH)%2 ? BFIELD_WIDTH : BFIELD_WIDTH+1 ) + doubleWide;
						if(vstd::contains(shadedHexes, destHex))
							attackFromHex = destHex;
						else if(actStack->attackerOwned) //if we are attacker
						{
							if(vstd::contains(shadedHexes, destHex+1))
								attackFromHex = destHex+1;
						}
						else //if we are defender
						{
							if(vstd::contains(shadedHexes, destHex-1))
								attackFromHex = destHex-1;
						}
						break;
					}
				case 7: //from bottom left
					{
						int destHex = whichOne + ( (whichOne/BFIELD_WIDTH)%2 ? BFIELD_WIDTH-1 : BFIELD_WIDTH );
						if(vstd::contains(shadedHexes, destHex))
							attackFromHex = destHex;
						else if(actStack->attackerOwned) //if we are attacker
						{
							if(vstd::contains(shadedHexes, destHex+1))
								attackFromHex = destHex+1;
						}
						else //if we are defender
						{
							if(vstd::contains(shadedHexes, destHex-1))
								attackFromHex = destHex-1;
						}
						break;
					}
				case 8: //from left
					{
						if(LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::DOUBLE_WIDE) && !LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned)
						{
							std::vector<int> acc = LOCPLINT->cb->battleGetAvailableHexes(activeStack, false);
							if(vstd::contains(acc, whichOne))
								attackFromHex = whichOne - 1;
							else
								attackFromHex = whichOne - 2;
						}
						else
						{
							attackFromHex = whichOne - 1;
						}
						break;
					}
				case 9: //from top left
					{
						int destHex = whichOne - ( (whichOne/BFIELD_WIDTH)%2 ? BFIELD_WIDTH+1 : BFIELD_WIDTH );
						if(vstd::contains(shadedHexes, destHex))
							attackFromHex = destHex;
						else if(actStack->attackerOwned) //if we are attacker
						{
							if(vstd::contains(shadedHexes, destHex+1))
								attackFromHex = destHex+1;
						}
						else //if we are defender
						{
							if(vstd::contains(shadedHexes, destHex-1))
								attackFromHex = destHex-1;
						}
						break;
					}
				case 10: //from top right
					{
						bool doubleWide = LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::DOUBLE_WIDE);
						int destHex = whichOne - ( (whichOne/BFIELD_WIDTH)%2 ? BFIELD_WIDTH : BFIELD_WIDTH-1 ) + doubleWide;
						if(vstd::contains(shadedHexes, destHex))
							attackFromHex = destHex;
						else if(actStack->attackerOwned) //if we are attacker
						{
							if(vstd::contains(shadedHexes, destHex+1))
								attackFromHex = destHex+1;
						}
						else //if we are defender
						{
							if(vstd::contains(shadedHexes, destHex-1))
								attackFromHex = destHex-1;
						}
						break;
					}
				case 11: //from right
					{
						if(LOCPLINT->cb->battleGetStackByID(activeStack)->hasFeatureOfType(StackFeature::DOUBLE_WIDE) && LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned)
						{
							std::vector<int> acc = LOCPLINT->cb->battleGetAvailableHexes(activeStack, false);
							if(vstd::contains(acc, whichOne))
								attackFromHex = whichOne + 1;
							else
								attackFromHex = whichOne + 2;
						}
						else
						{
							attackFromHex = whichOne + 1;
						}
						break;
					}
				case 13: //from bottom
					{
						int destHex = whichOne + ( (whichOne/BFIELD_WIDTH)%2 ? BFIELD_WIDTH : BFIELD_WIDTH+1 );
						if(vstd::contains(shadedHexes, destHex))
							attackFromHex = destHex;
						else if(attackingHeroInstance->tempOwner == LOCPLINT->cb->getMyColor()) //if we are attacker
						{
							if(vstd::contains(shadedHexes, destHex+1))
								attackFromHex = destHex+1;
						}
						else //if we are defender
						{
							if(vstd::contains(shadedHexes, destHex-1))
								attackFromHex = destHex-1;
						}
						break;
					}
				case 14: //from top
					{
						int destHex = whichOne - ( (whichOne/BFIELD_WIDTH)%2 ? BFIELD_WIDTH : BFIELD_WIDTH-1 );
						if(vstd::contains(shadedHexes, destHex))
							attackFromHex = destHex;
						else if(attackingHeroInstance->tempOwner == LOCPLINT->cb->getMyColor()) //if we are attacker
						{
							if(vstd::contains(shadedHexes, destHex+1))
								attackFromHex = destHex+1;
						}
						else //if we are defender
						{
							if(vstd::contains(shadedHexes, destHex-1))
								attackFromHex = destHex-1;
						}
						break;
					}
				}

				giveCommand(6, attackFromHex, activeStack, whichOne);

				CGI->curh->changeGraphic(1, 6); //cursor should be changed
			}
		}
	}
}

void CBattleInterface::stackIsShooting(int ID, int dest)
{
	addNewAnim(new CShootingAnim(this, ID, dest));
}

void CBattleInterface::battleFinished(const BattleResult& br)
{
	bresult = &br;
	LOCPLINT->pim->unlock();
	animsAreDisplayed.waitUntil(false);
	LOCPLINT->pim->lock();
	displayBattleFinished();
}

void CBattleInterface::displayBattleFinished()
{
	CGI->curh->changeGraphic(0,0);
	
	SDL_Rect temp_rect = genRect(561, 470, (screen->w - 800)/2 + 165, (screen->h - 600)/2 + 19);
	CGI->musich->stopMusic();
	resWindow = new CBattleResultWindow(*bresult, temp_rect, this);
	GH.pushInt(resWindow);
}

void CBattleInterface::spellCast(SpellCast * sc)
{
	CSpell &spell = CGI->spellh->spells[sc->id];

	if(sc->side == !LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned)
		bSpell->block(true);

	std::vector< std::string > anims; //for magic arrow and ice bolt

	if (spell.soundID != soundBase::invalid)
		CGI->soundh->playSound(spell.soundID);

	switch(sc->id)
	{
	case 15: //magic arrow
		{
			//initialization of anims
			anims.push_back("C20SPX0.DEF"); anims.push_back("C20SPX1.DEF"); anims.push_back("C20SPX2.DEF"); anims.push_back("C20SPX3.DEF"); anims.push_back("C20SPX4.DEF");
		}
	case 16: //ice bolt
		{
			if(anims.size() == 0) //initialization of anims
			{
				anims.push_back("C08SPW0.DEF"); anims.push_back("C08SPW1.DEF"); anims.push_back("C08SPW2.DEF"); anims.push_back("C08SPW3.DEF"); anims.push_back("C08SPW4.DEF");
			}
		} //end of ice bolt only part
		{ //common ice bolt and magic arrow part
			//initial variables
			std::string animToDisplay;
			Point srccoord = (sc->side ? Point(770, 60) : Point(30, 60)) + pos;
			Point destcoord = CBattleHex::getXYUnitAnim(sc->tile, !sc->side, LOCPLINT->cb->battleGetStackByPos(sc->tile), this); //position attacked by arrow
			destcoord.x += 250; destcoord.y += 240;

			//animation angle
			float angle = atan2(float(destcoord.x - srccoord.x), float(destcoord.y - srccoord.y));

			//choosing animation by angle
			if(angle > 1.50)
				animToDisplay = anims[0];
			else if(angle > 1.20)
				animToDisplay = anims[1];
			else if(angle > 0.90)
				animToDisplay = anims[2];
			else if(angle > 0.60)
				animToDisplay = anims[3];
			else
				animToDisplay = anims[4];

			//displaying animation
			CDefEssential * animDef = CDefHandler::giveDefEss(animToDisplay);
			int steps = sqrt((float)((destcoord.x - srccoord.x)*(destcoord.x - srccoord.x) + (destcoord.y - srccoord.y) * (destcoord.y - srccoord.y))) / 40;
			if(steps <= 0)
				steps = 1;

			int dx = (destcoord.x - srccoord.x - animDef->ourImages[0].bitmap->w)/steps, dy = (destcoord.y - srccoord.y - animDef->ourImages[0].bitmap->h)/steps;

			delete animDef;
			addNewAnim(new CSpellEffectAnim(this, animToDisplay, srccoord.x, srccoord.y, dx, dy));

			break; //for 15 and 16 cases
		}
	case 17: //lightning bolt
		displayEffect(1, sc->tile);
		displayEffect(spell.mainEffectAnim, sc->tile);
		break;
	case 35: //dispel
	case 37: //cure
	case 38: //resurrection
	case 39: //animate dead
		for(std::set<ui32>::const_iterator it = sc->affectedCres.begin(); it != sc->affectedCres.end(); ++it)
		{
			displayEffect(spell.mainEffectAnim, LOCPLINT->cb->battleGetStackByID(*it, false)->position);
		}
		break;
	} //switch(sc->id)

	//support for resistance
	for(int j=0; j<sc->resisted.size(); ++j)
	{
		int tile = LOCPLINT->cb->battleGetStackByID(sc->resisted[j])->position;
		displayEffect(78, tile);
	}
}

void CBattleInterface::battleStacksEffectsSet(const SetStackEffect & sse)
{
	for(std::set<ui32>::const_iterator ci = sse.stacks.begin(); ci!=sse.stacks.end(); ++ci)
	{
		displayEffect(CGI->spellh->spells[sse.effect.id].mainEffectAnim, LOCPLINT->cb->battleGetStackByID(*ci)->position);
	}
	redrawBackgroundWithHexes(activeStack);
}

void CBattleInterface::castThisSpell(int spellID)
{
	BattleAction * ba = new BattleAction;
	ba->actionType = 1;
	ba->additionalInfo = spellID; //spell number
	ba->destinationTile = -1;
	ba->stackNumber = (attackingHeroInstance->tempOwner == LOCPLINT->playerID) ? -1 : -2;
	ba->side = defendingHeroInstance ? (LOCPLINT->playerID == defendingHeroInstance->tempOwner) : false;
	spellToCast = ba;
	spellDestSelectMode = true;

	//choosing possible tragets
	const CGHeroInstance * castingHero = (attackingHeroInstance->tempOwner == LOCPLINT->playerID) ? attackingHeroInstance : attackingHeroInstance;
	spellSelMode = 0;
	if(CGI->spellh->spells[spellID].attributes.find("CREATURE_TARGET") != std::string::npos) //spell to be cast on one specific creature
	{
		switch(CGI->spellh->spells[spellID].positiveness)
		{
		case -1 :
			spellSelMode = 2;
			break;
		case 0:
			spellSelMode = 3;
			break;
		case 1:
			spellSelMode = 1;
			break;
		}
	}
	if(CGI->spellh->spells[spellID].attributes.find("CREATURE_TARGET_1") != std::string::npos ||
		CGI->spellh->spells[spellID].attributes.find("CREATURE_TARGET_2") != std::string::npos) //spell to be cast on a specific creature but massive on expert
	{
		if(castingHero && castingHero->getSpellSecLevel(spellID) < 3)
		{
			switch(CGI->spellh->spells[spellID].positiveness)
			{
			case -1 :
				spellSelMode = 2;
				break;
			case 0:
				spellSelMode = 3;
				break;
			case 1:
				spellSelMode = 1;
				break;
			}
		}
		else
		{
			spellSelMode = -1;
		}
	}
	if(CGI->spellh->spells[spellID].attributes.find("OBSTACLE_TARGET") != std::string::npos) //spell to be cast on an obstacle
	{
		spellSelMode = 4;
	}
	if(spellSelMode == -1) //user does not have to select location
	{
		spellToCast->destinationTile = -1;
		LOCPLINT->cb->battleMakeAction(spellToCast);
		delete spellToCast;
		spellToCast = NULL;
		spellDestSelectMode = false;
		CGI->curh->changeGraphic(1, 6);
	}
	else
	{
		CGI->curh->changeGraphic(3, 0); 
	}
}

void CBattleInterface::displayEffect(ui32 effect, int destTile)
{
	addNewAnim(new CSpellEffectAnim(this, effect, destTile));
}

void CBattleInterface::setAnimSpeed(int set)
{
	settings.animSpeed = set;
}

int CBattleInterface::getAnimSpeed() const
{
	return settings.animSpeed;
}

void CBattleInterface::activateStack()
{
	activeStack = stackToActivate;
	queue->update();
	stackToActivate = -1;
	myTurn = true;
	redrawBackgroundWithHexes(activeStack);
	bWait->block(vstd::contains(LOCPLINT->cb->battleGetStackByID(activeStack)->state,WAITING)); //block waiting button if stack has been already waiting

	//block cast spell button if hero doesn't have a spellbook
	if(attackingHeroInstance && LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned)
	{
		if(!attackingHeroInstance->getArt(17)) //don't unlock if already locked
			bSpell->block(!attackingHeroInstance->getArt(17));
	}
	else if(defendingHeroInstance && !LOCPLINT->cb->battleGetStackByID(activeStack)->attackerOwned)
	{
		if(!defendingHeroInstance->getArt(17)) //don't unlock if already locked
			bSpell->block(!defendingHeroInstance->getArt(17));
	}
}

float CBattleInterface::getAnimSpeedMultiplier() const
{
	switch(settings.animSpeed)
	{
	case 1:
		return 3.5f;
	case 2:
		return 2.2f;
	case 4:
		return 1.0f;
	default:
		return 0.0f;
	}
}

void CBattleInterface::endCastingSpell()
{
	assert(spellDestSelectMode);

	delete spellToCast;
	spellToCast = NULL;
	spellDestSelectMode = false;
	CGI->curh->changeGraphic(1, 6);
}

void CBattleInterface::showAliveStack(int ID, const std::map<int, CStack> & stacks, SDL_Surface * to)
{
	if(creAnims.find(ID) == creAnims.end()) //eg. for summoned but not yet handled stacks
		return;

	const CStack &curStack = stacks.find(ID)->second;
	int animType = creAnims[ID]->getType();

	int affectingSpeed = settings.animSpeed;
	if(animType == 1 || animType == 2) //standing stacks should not stand faster :)
		affectingSpeed = 2;
	if(animType == 3 || animType == 7 || animType == 8 || animType == 9 || animType == 10 || animType == 11 || animType == 12 || animType == 13) //defend & attack should be slower
		affectingSpeed = 1;
	bool incrementFrame = (animCount%(4/affectingSpeed)==0) && animType!=5 && animType!=20 && animType!=2;

	if(animType == 2)
	{
		if(standingFrame.find(ID)!=standingFrame.end())
		{
			incrementFrame = (animCount%(8/affectingSpeed)==0);
			if(incrementFrame)
			{
				++standingFrame[ID];
				if(standingFrame[ID] == creAnims[ID]->framesInGroup(2))
				{
					standingFrame.erase(standingFrame.find(ID));
				}
			}
		}
		else
		{
			if((rand()%50) == 0)
			{
				standingFrame.insert(std::make_pair(ID, 0));
			}
		}
	}

	creAnims[ID]->nextFrame(to, creAnims[ID]->pos.x, creAnims[ID]->pos.y, creDir[ID], animCount, incrementFrame, ID==activeStack, ID==mouseHoveredStack); //increment always when moving, never if stack died

	//printing amount
	if(curStack.amount > 0 //don't print if stack is not alive
		&& (!LOCPLINT->curAction
			|| (LOCPLINT->curAction->stackNumber != ID //don't print if stack is currently taking an action
				&& (LOCPLINT->curAction->actionType != 6  ||  curStack.position != LOCPLINT->curAction->additionalInfo) //nor if it's an object of attack
				&& (LOCPLINT->curAction->destinationTile != curStack.position) //nor if it's on destination tile for current action
				)
			)
			&& !curStack.hasFeatureOfType(StackFeature::SIEGE_WEAPON) //and not a war machine...
	)
	{
		int xAdd = curStack.attackerOwned ? 220 : 202;

		//blitting amoutn background box
		SDL_Surface *amountBG = NULL;
		if(curStack.effects.size() == 0)
		{
			amountBG = amountNormal;
		}
		else
		{
			int pos=0; //determining total positiveness of effects
			for(int c=0; c<curStack.effects.size(); ++c)
			{
				pos += CGI->spellh->spells[ curStack.effects[c].id ].positiveness;
			}
			if(pos > 0)
			{
				amountBG = amountPositive;
			}
			else if(pos < 0)
			{
				amountBG = amountNegative;
			}
			else
			{
				amountBG = amountEffNeutral;
			}
		}
		SDL_BlitSurface(amountBG, NULL, to, &genRect(amountNormal->h, amountNormal->w, creAnims[ID]->pos.x + xAdd, creAnims[ID]->pos.y + 260));
		//blitting amount
		CSDL_Ext::printAtMiddle(
			makeNumberShort(curStack.amount),
			creAnims[ID]->pos.x + xAdd + 15,
			creAnims[ID]->pos.y + 260 + 5,
			FONT_TINY,
			zwykly,
			to
        );
	}
}

void CBattleInterface::showPieceOfWall(SDL_Surface * to, int hex, const std::map<int, CStack> & stacks)
{
	if(!siegeH)
		return;

	static const std::map<int, int> hexToPart = boost::assign::map_list_of(12, 8)(16, 1)(29, 7)(50, 2)(62, 12)(78, 6)(112, 10)(147, 5)(165, 11)(182, 3)(186, 0);
	
	//additionally print bottom wall
	if(hex == 182)
	{
		siegeH->printPartOfWall(to, 4);
	}

	std::map<int, int>::const_iterator it = hexToPart.find(hex);
	if(it != hexToPart.end())
	{
		siegeH->printPartOfWall(to, it->second);

		//print creature in turret
		int posToSeek = -1;
		switch(it->second)
		{
		case 3: //bottom turret
			posToSeek = -3;
			break;
		case 8: //upper turret
			posToSeek = -4;
			break;
		case 2: //keep
			posToSeek = -2;
			break;
		}

		if(posToSeek != -1)
		{
			int ID = -1;
			for(std::map<int, CStack>::const_iterator it = stacks.begin(); it != stacks.end(); ++it)
			{
				if(it->second.position == posToSeek)
				{
					ID = it->second.ID;
					break;
				}
			}
			if(ID != -1)
			{
				showAliveStack(ID, stacks, to);
				//blitting creature cover
				switch(posToSeek)
				{
				case -3: //bottom turret
					siegeH->printPartOfWall(to, 16);
					break;
				case -4: //upper turret
					siegeH->printPartOfWall(to, 17);
					break;
				case -2: //keep
					siegeH->printPartOfWall(to, 15);
					break;
				}
			}

		}
	}

}

void CBattleInterface::redrawBackgroundWithHexes(int activeStack)
{
	shadedHexes = LOCPLINT->cb->battleGetAvailableHexes(activeStack, true);

	//preparating background graphic with hexes and shaded hexes
	blitAt(background, 0, 0, backgroundWithHexes);
	if(settings.printCellBorders)
		CSDL_Ext::blit8bppAlphaTo24bpp(cellBorders, NULL, backgroundWithHexes, NULL);

	if(settings.printStackRange)
	{
		for(size_t m=0; m<shadedHexes.size(); ++m) //rows
		{
			int i = shadedHexes[m]/BFIELD_WIDTH; //row
			int j = shadedHexes[m]%BFIELD_WIDTH-1; //column
			int x = 58 + (i%2==0 ? 22 : 0) + 44*j;
			int y = 86 + 42 * i;
			CSDL_Ext::blit8bppAlphaTo24bpp(cellShade, NULL, backgroundWithHexes, &genRect(cellShade->h, cellShade->w, x, y));
		}
	}
}

void CBattleInterface::printConsoleAttacked(int ID, int dmg, int killed, int IDby)
{
	char tabh[200];
	const CStack * attacker = LOCPLINT->cb->battleGetStackByID(IDby, false);
	const CStack * defender = LOCPLINT->cb->battleGetStackByID(ID, false);
	int end = sprintf(tabh, CGI->generaltexth->allTexts[attacker->amount > 1 ? 377 : 376].c_str(),
		(attacker->amount > 1 ? attacker->creature->namePl.c_str() : attacker->creature->nameSing.c_str()),
		dmg);
	if(killed > 0)
	{
		if(killed > 1)
		{
			sprintf(tabh + end, CGI->generaltexth->allTexts[379].c_str(), killed, defender->creature->namePl.c_str());
		}
		else //killed == 1
		{
			sprintf(tabh + end, CGI->generaltexth->allTexts[378].c_str(), defender->creature->nameSing.c_str());
		}
	}

	console->addText(std::string(tabh));
}

void CBattleInterface::projectileShowHelper(SDL_Surface * to)
{
	if(to == NULL)
		to = screen;
	std::list< std::list<SProjectileInfo>::iterator > toBeDeleted;
	for(std::list<SProjectileInfo>::iterator it=projectiles.begin(); it!=projectiles.end(); ++it)
	{
		if(it->animStartDelay>0)
		{
			--(it->animStartDelay);
			continue;
		}
		SDL_Rect dst;
		dst.h = idToProjectile[it->creID]->ourImages[it->frameNum].bitmap->h;
		dst.w = idToProjectile[it->creID]->ourImages[it->frameNum].bitmap->w;
		dst.x = it->x;
		dst.y = it->y;
		if(it->reverse)
		{
			SDL_Surface * rev = CSDL_Ext::rotate01(idToProjectile[it->creID]->ourImages[it->frameNum].bitmap);
			CSDL_Ext::blit8bppAlphaTo24bpp(rev, NULL, to, &dst);
			SDL_FreeSurface(rev);
		}
		else
		{
			CSDL_Ext::blit8bppAlphaTo24bpp(idToProjectile[it->creID]->ourImages[it->frameNum].bitmap, NULL, to, &dst);
		}
		//actualizing projectile
		++it->step;
		if(it->step == it->lastStep)
		{
			toBeDeleted.insert(toBeDeleted.end(), it);
		}
		else
		{
			it->x += it->dx;
			it->y += it->dy;
			if(it->spin)
			{
				++(it->frameNum);
				it->frameNum %= idToProjectile[it->creID]->ourImages.size();
			}
		}
	}
	for(std::list< std::list<SProjectileInfo>::iterator >::iterator it = toBeDeleted.begin(); it!= toBeDeleted.end(); ++it)
	{
		projectiles.erase(*it);
	}
}

void CBattleInterface::endAction(const BattleAction* action)
{	
	//if((action->actionType==2 || (action->actionType==6 && action->destinationTile!=cb->battleGetPos(action->stackNumber)))) //activating interface when move is finished
	{
		activate();
	}
	if(action->actionType == 1)
	{
		if(action->side)
			defendingHero->setPhase(0);
		else
			attackingHero->setPhase(0);
	}
	if(action->actionType == 2 && creAnims[action->stackNumber]->getType() != 2) //walk or walk & attack
	{
		pendingAnims.push_back(std::make_pair(new CBattleMoveEnd(this, action->stackNumber, action->destinationTile), false));
	}
}

void CBattleInterface::hideQueue()
{
	settings.showQueue = false;
	//if(queue->active)
		queue->deactivate();

	if(!queue->embedded)
	{
		moveBy(Point(0, -queue->pos.h / 2));
		GH.totalRedraw();
	}
}

void CBattleInterface::showQueue()
{
	settings.showQueue = true;
	//if(!queue->active)
		queue->activate();

	if(!queue->embedded)
	{
		moveBy(Point(0, +queue->pos.h / 2));
		GH.totalRedraw();
	}
}

void CBattleHero::show(SDL_Surface *to)
{
	//animation of flag
	if(flip)
	{
		CSDL_Ext::blit8bppAlphaTo24bpp(
			flag->ourImages[flagAnim].bitmap,
			NULL,
			screen,
			&genRect(
				flag->ourImages[flagAnim].bitmap->h,
				flag->ourImages[flagAnim].bitmap->w,
				62 + pos.x,
				39 + pos.y
			)
		);
	}
	else
	{
		CSDL_Ext::blit8bppAlphaTo24bpp(
			flag->ourImages[flagAnim].bitmap,
			NULL,
			screen,
			&genRect(
				flag->ourImages[flagAnim].bitmap->h,
				flag->ourImages[flagAnim].bitmap->w,
				71 + pos.x,
				39 + pos.y
			)
		);
	}
	++flagAnimCount;
	if(flagAnimCount%4==0)
	{
		++flagAnim;
		flagAnim %= flag->ourImages.size();
	}
	//animation of hero
	int tick=-1;
	for(int i=0; i<dh->ourImages.size(); ++i)
	{
		if(dh->ourImages[i].groupNumber==phase)
			++tick;
		if(tick==image)
		{
			SDL_Rect posb = pos;
			CSDL_Ext::blit8bppAlphaTo24bpp(dh->ourImages[i].bitmap, NULL, to, &posb);
			if(phase != 4 || nextPhase != -1 || image < 4)
			{
				if(flagAnimCount%2==0)
				{
					++image;
				}
				if(dh->ourImages[(i+1)%dh->ourImages.size()].groupNumber!=phase) //back to appropriate frame
				{
					image = 0;
				}
			}
			if(phase == 4 && nextPhase != -1 && image == 7)
			{
				phase = nextPhase;
				nextPhase = -1;
				image = 0;
			}
			break;
		}
	}
}

void CBattleHero::activate()
{
	activateLClick();
}
void CBattleHero::deactivate()
{
	deactivateLClick();
}

void CBattleHero::setPhase(int newPhase)
{
	if(phase != 4)
	{
		phase = newPhase;
		image = 0;
	}
	else
	{
		nextPhase = newPhase;
	}
}

void CBattleHero::clickLeft(tribool down, bool previousState)
{
	if(myOwner->spellDestSelectMode) //we are casting a spell
		return;

	if(!down && myHero && LOCPLINT->cb->battleCanCastSpell()) //check conditions
	{
		for(int it=0; it<BFIELD_SIZE; ++it) //do nothing when any hex is hovered - hero's animation overlaps battlefield
		{
			if(myOwner->bfield[it].hovered && myOwner->bfield[it].strictHovered)
				return;
		}
		CGI->curh->changeGraphic(0,0);

		CSpellWindow * spellWindow = new CSpellWindow(genRect(595, 620, (conf.cc.resx - 620)/2, (conf.cc.resy - 595)/2), myHero);
		GH.pushInt(spellWindow);
	}
}

CBattleHero::CBattleHero(const std::string & defName, int phaseG, int imageG, bool flipG, unsigned char player, const CGHeroInstance * hero, const CBattleInterface * owner): flip(flipG), myHero(hero), myOwner(owner), phase(phaseG), nextPhase(-1), image(imageG), flagAnim(0), flagAnimCount(0)
{
	dh = CDefHandler::giveDef( defName );
	for(int i=0; i<dh->ourImages.size(); ++i) //transforming images
	{
		if(flip)
		{
			SDL_Surface * hlp = CSDL_Ext::rotate01(dh->ourImages[i].bitmap);
			SDL_FreeSurface(dh->ourImages[i].bitmap);
			dh->ourImages[i].bitmap = hlp;
		}
		CSDL_Ext::alphaTransform(dh->ourImages[i].bitmap);
	}
	dh->alphaTransformed = true;

	if(flip)
		flag = CDefHandler::giveDef("CMFLAGR.DEF");
	else
		flag = CDefHandler::giveDef("CMFLAGL.DEF");

	//coloring flag and adding transparency
	for(int i=0; i<flag->ourImages.size(); ++i)
	{
		CSDL_Ext::alphaTransform(flag->ourImages[i].bitmap);
		graphics->blueToPlayersAdv(flag->ourImages[i].bitmap, player);
	}
}

CBattleHero::~CBattleHero()
{
	delete dh;
	delete flag;
}

Point CBattleHex::getXYUnitAnim(const int & hexNum, const bool & attacker, const CStack * stack, const CBattleInterface * cbi)
{
	Point ret(-500, -500); //returned value
	if(stack->position < 0) //creatures in turrets
	{
		const CCreature & turretCreature = CGI->creh->creatures[ CGI->creh->factionToTurretCreature[cbi->siegeH->town->town->typeID] ];
		int xShift = turretCreature.isDoubleWide() ? 44 : 0;

		switch(stack->position)
		{
		case -2: //keep
			ret = Point(505 + xShift, -66);
			break;
		case -3: //lower turret
			ret = Point(368 + xShift, 304);
			break;
		case -4: //upper turret
			ret = Point(339 + xShift, -192);
			break;	
		}
	}
	else
	{
		ret.y = -139 + 42 * (hexNum/BFIELD_WIDTH); //counting y
		//counting x
		if(attacker)
		{
			ret.x = -160 + 22 * ( ((hexNum/BFIELD_WIDTH) + 1)%2 ) + 44 * (hexNum % BFIELD_WIDTH);
		}
		else
		{
			ret.x = -219 + 22 * ( ((hexNum/BFIELD_WIDTH) + 1)%2 ) + 44 * (hexNum % BFIELD_WIDTH);
		}
		//shifting position for double - hex creatures
		if(stack && stack->hasFeatureOfType(StackFeature::DOUBLE_WIDE))
		{
			if(attacker)
			{
				ret.x -= 42;
			}
			else
			{
				ret.x += 42;
			}
		}
	}
	//returning
	return ret + LOCPLINT->battleInt->pos;
}
void CBattleHex::activate()
{
	activateHover();
	activateMouseMove();
	activateLClick();
	activateRClick();
}

void CBattleHex::deactivate()
{
	deactivateHover();
	deactivateMouseMove();
	deactivateLClick();
	deactivateRClick();
}

void CBattleHex::hover(bool on)
{
	hovered = on;
	//Hoverable::hover(on);
	if(!on && setAlterText)
	{
		myInterface->console->alterTxt = std::string();
		setAlterText = false;
	}
}

CBattleHex::CBattleHex() : setAlterText(false), myNumber(-1), accesible(true), hovered(false), strictHovered(false), myInterface(NULL)
{
}

void CBattleHex::mouseMoved(const SDL_MouseMotionEvent &sEvent)
{
	if(myInterface->cellShade)
	{
		if(CSDL_Ext::SDL_GetPixel(myInterface->cellShade, sEvent.x-pos.x, sEvent.y-pos.y) == 0) //hovered pixel is outside hex
		{
			strictHovered = false;
		}
		else //hovered pixel is inside hex
		{
			strictHovered = true;
		}
	}

	if(hovered && strictHovered) //print attacked creature to console
	{
		if(myInterface->console->alterTxt.size() == 0 && LOCPLINT->cb->battleGetStack(myNumber) != -1 &&
			LOCPLINT->cb->battleGetStackByPos(myNumber)->owner != LOCPLINT->playerID &&
			LOCPLINT->cb->battleGetStackByPos(myNumber)->alive())
		{
			char tabh[160];
			CStack attackedStack = *LOCPLINT->cb->battleGetStackByPos(myNumber);
			const std::string & attackedName = attackedStack.amount == 1 ? attackedStack.creature->nameSing : attackedStack.creature->namePl;
			sprintf(tabh, CGI->generaltexth->allTexts[220].c_str(), attackedName.c_str());
			myInterface->console->alterTxt = std::string(tabh);
			setAlterText = true;
		}
	}
	else if(setAlterText)
	{
		myInterface->console->alterTxt = std::string();
		setAlterText = false;
	}
}

void CBattleHex::clickLeft(tribool down, bool previousState)
{
	if(!down && hovered && strictHovered) //we've been really clicked!
	{
		myInterface->hexLclicked(myNumber);
	}
}

void CBattleHex::clickRight(tribool down, bool previousState)
{
	int stID = LOCPLINT->cb->battleGetStack(myNumber); //id of stack being on this tile
	if(hovered && strictHovered && stID!=-1)
	{
		const CStack & myst = *LOCPLINT->cb->battleGetStackByID(stID); //stack info
		if(!myst.alive()) return;
		StackState *pom = NULL;
		if(down)
		{
			pom = new StackState();
			const CGHeroInstance *h = myst.owner == myInterface->attackingHeroInstance->tempOwner ? myInterface->attackingHeroInstance : myInterface->defendingHeroInstance;

			pom->attackBonus = myst.Attack() - myst.creature->attack;
			pom->defenseBonus = myst.Defense() - myst.creature->defence;
			pom->luck = myst.Luck();
			pom->morale = myst.Morale();
			pom->speedBonus = myst.Speed() - myst.creature->speed;
			pom->healthBonus = myst.MaxHealth() - myst.creature->hitPoints;
			if(myst.hasFeatureOfType(StackFeature::SIEGE_WEAPON))
				pom->dmgMultiplier = h->getPrimSkillLevel(0) + 1;
			else
				pom->dmgMultiplier = 1;

			pom->shotsLeft = myst.shots;
			for(int vb=0; vb<myst.effects.size(); ++vb)
			{
				pom->effects.insert(myst.effects[vb].id);
			}
			pom->currentHealth = myst.firstHPleft;
			GH.pushInt(new CCreInfoWindow(myst.creature->idNumber, 0, myst.amount, pom, 0, 0, NULL));
		}
		delete pom;
	}
}

CBattleConsole::CBattleConsole() : lastShown(-1), alterTxt(""), whoSetAlter(0)
{
}

CBattleConsole::~CBattleConsole()
{
	texts.clear();
}

void CBattleConsole::show(SDL_Surface * to)
{
	if(ingcAlter.size())
	{
		CSDL_Ext::printAtMiddleWB(ingcAlter, pos.x + pos.w/2, pos.y + 10, GEOR13, 80, zwykly, to);
	}
	else if(alterTxt.size())
	{
		CSDL_Ext::printAtMiddleWB(alterTxt, pos.x + pos.w/2, pos.y + 10, GEOR13, 80, zwykly, to);
	}
	else if(texts.size())
	{
		if(texts.size()==1)
		{
			CSDL_Ext::printAtMiddleWB(texts[0], pos.x + pos.w/2, pos.y + 10, GEOR13, 80, zwykly, to);
		}
		else
		{
			CSDL_Ext::printAtMiddleWB(texts[lastShown-1], pos.x + pos.w/2, pos.y + 10, GEOR13, 80, zwykly, to);
			CSDL_Ext::printAtMiddleWB(texts[lastShown], pos.x + pos.w/2, pos.y + 26, GEOR13, 80, zwykly, to);
		}
	}
}

bool CBattleConsole::addText(const std::string & text)
{
	if(text.size()>70)
		return false; //text too long!
	int firstInToken = 0;
	for(int i=0; i<text.size(); ++i) //tokenize
	{
		if(text[i] == 10)
		{
			texts.push_back( text.substr(firstInToken, i-firstInToken) );
			firstInToken = i+1;
		}
	}

	texts.push_back( text.substr(firstInToken, text.size()) );
	lastShown = texts.size()-1;
	return true;
}

void CBattleConsole::eraseText(unsigned int pos)
{
	if(pos < texts.size())
	{
		texts.erase(texts.begin() + pos);
		if(lastShown == texts.size())
			--lastShown;
	}
}

void CBattleConsole::changeTextAt(const std::string & text, unsigned int pos)
{
	if(pos >= texts.size()) //no such pos
		return;
	texts[pos] = text;
}

void CBattleConsole::scrollUp(unsigned int by)
{
	if(lastShown > by)
		lastShown -= by;
}

void CBattleConsole::scrollDown(unsigned int by)
{
	if(lastShown + by < texts.size())
		lastShown += by;
}

CBattleResultWindow::CBattleResultWindow(const BattleResult &br, const SDL_Rect & pos, const CBattleInterface * owner)
{
	this->pos = pos;
	background = BitmapHandler::loadBitmap("CPRESULT.BMP", true);
	graphics->blueToPlayersAdv(background, LOCPLINT->playerID);
	SDL_Surface * pom = SDL_ConvertSurface(background, screen->format, screen->flags);
	SDL_FreeSurface(background);
	background = pom;
	exit = new AdventureMapButton (std::string(), std::string(), boost::bind(&CBattleResultWindow::bExitf,this), 384 + pos.x, 505 + pos.y, "iok6432.def", SDLK_RETURN);

	if(br.winner==0) //attacker won
	{
		CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[410], 60, 122, GEOR13, zwykly, background);
		CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[411], 410, 122, GEOR13, zwykly, background);
	}
	else //if(br.winner==1)
	{
		CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[411], 60, 122, GEOR13, zwykly, background);
		CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[410], 410, 122, GEOR13, zwykly, background);
	}

	
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[407], 235, 299, GEOR16, tytulowy, background);
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[408], 235, 329, GEOR16, zwykly, background);
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[409], 235, 426, GEOR16, zwykly, background);

	std::string attackerName, defenderName;

	if(owner->attackingHeroInstance) //a hero attacked
	{
		SDL_BlitSurface(graphics->portraitLarge[owner->attackingHeroInstance->portrait], NULL, background, &genRect(64, 58, 21, 38));
		//setting attackerName
		attackerName = owner->attackingHeroInstance->name;
	}
	else //a monster attacked
	{
		int bestMonsterID = -1;
		int bestPower = 0;
		for(std::map<si32,std::pair<ui32,si32> >::const_iterator it = owner->army1->slots.begin(); it!=owner->army1->slots.end(); ++it)
		{
			if( CGI->creh->creatures[it->first].AIValue > bestPower)
			{
				bestPower = CGI->creh->creatures[it->first].AIValue;
				bestMonsterID = it->first;
			}
		}
		SDL_BlitSurface(graphics->bigImgs[bestMonsterID], NULL, background, &genRect(64, 58, 21, 38));
		//setting attackerName
		attackerName =  CGI->creh->creatures[bestMonsterID].namePl;
	}
	if(owner->defendingHeroInstance) //a hero defended
	{
		SDL_BlitSurface(graphics->portraitLarge[owner->defendingHeroInstance->portrait], NULL, background, &genRect(64, 58, 391, 38));
		//setting defenderName
		defenderName = owner->defendingHeroInstance->name;
	}
	else //a monster defended
	{
		int bestMonsterID = -1;
		int bestPower = 0;
		for(std::map<si32,std::pair<ui32,si32> >::const_iterator it = owner->army2->slots.begin(); it!=owner->army2->slots.end(); ++it)
		{
			if( CGI->creh->creatures[it->second.first].AIValue > bestPower)
			{
				bestPower = CGI->creh->creatures[it->second.first].AIValue;
				bestMonsterID = it->second.first;
			}
		}
		SDL_BlitSurface(graphics->bigImgs[bestMonsterID], NULL, background, &genRect(64, 58, 391, 38));
		//setting defenderName
		defenderName =  CGI->creh->creatures[bestMonsterID].namePl;
	}

	//printing attacker and defender's names
	CSDL_Ext::printAtMiddle(attackerName, 156, 44, GEOR16, zwykly, background);
	CSDL_Ext::printAtMiddle(defenderName, 314, 44, GEOR16, zwykly, background);
	//printing casualities
	for(int step = 0; step < 2; ++step)
	{
		if(br.casualties[step].size()==0)
		{
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[523], 235, 360 + 97*step, GEOR16, zwykly, background);
		}
		else
		{
			int xPos = 235 - (br.casualties[step].size()*32 + (br.casualties[step].size() - 1)*10)/2; //increment by 42 with each picture
			int yPos = 344 + step*97;
			for(std::set<std::pair<ui32,si32> >::const_iterator it=br.casualties[step].begin(); it!=br.casualties[step].end(); ++it)
			{
				blitAt(graphics->smallImgs[it->first], xPos, yPos, background);
				std::ostringstream amount;
				amount<<it->second;
				CSDL_Ext::printAtMiddle(amount.str(), xPos+16, yPos + 42, GEOR13, zwykly, background);
				xPos += 42;
			}
		}
	}
	//printing result description
	bool weAreAttacker = (LOCPLINT->playerID == owner->attackingHeroInstance->tempOwner);
	switch(br.result)
	{
	case 0: //normal victory
		if((br.winner == 0 && weAreAttacker) || (br.winner == 1 && !weAreAttacker)) //we've won
		{
			CGI->musich->playMusic(musicBase::winBattle);
#ifdef _WIN32
			CGI->videoh->open(VIDEO_WIN);
#else
			CGI->videoh->open(VIDEO_WIN, true);
#endif
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[304], 235, 235, GEOR13, zwykly, background);
		}
		else
		{
			CGI->musich->playMusic(musicBase::loseCombat);
			CGI->videoh->open(VIDEO_LOSE_BATTLE_START);
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[311], 235, 235, GEOR13, zwykly, background);
		}
		break;
	case 1: //flee
		if((br.winner == 0 && weAreAttacker) || (br.winner == 1 && !weAreAttacker)) //we've won
		{
			CGI->musich->playMusic(musicBase::winBattle);
#ifdef _WIN32
			CGI->videoh->open(VIDEO_WIN);
#else
			CGI->videoh->open(VIDEO_WIN, true);
#endif
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[303], 235, 235, GEOR13, zwykly, background);
		}
		else
		{
			CGI->musich->playMusic(musicBase::retreatBattle);
			CGI->videoh->open(VIDEO_RETREAT_START);
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[310], 235, 235, GEOR13, zwykly, background);
		}
		break;
	case 2: //surrender
		if((br.winner == 0 && weAreAttacker) || (br.winner == 1 && !weAreAttacker)) //we've won
		{
			CGI->musich->playMusic(musicBase::winBattle);
#ifdef _WIN32
			CGI->videoh->open(VIDEO_WIN);
#else
			CGI->videoh->open(VIDEO_WIN, true);
#endif
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[302], 235, 235, GEOR13, zwykly, background);
		}
		else
		{
			CGI->musich->playMusic(musicBase::surrenderBattle);
			CGI->videoh->open(VIDEO_SURRENDER);
			CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[309], 235, 235, GEOR13, zwykly, background);
		}
		break;
	}
}

CBattleResultWindow::~CBattleResultWindow()
{
	SDL_FreeSurface(background);
}

void CBattleResultWindow::activate()
{
	LOCPLINT->showingDialog->set(true);
	exit->activate();
}

void CBattleResultWindow::deactivate()
{
	exit->deactivate();
}

void CBattleResultWindow::show(SDL_Surface *to)
{
	//evaluating to
	if(!to)
		to = screen;

	CGI->videoh->update(107, 70, background, false, true);

	SDL_BlitSurface(background, NULL, to, &pos);
	exit->show(to);
}

void CBattleResultWindow::bExitf()
{
	GH.popInts(2); //first - we; second - battle interface
	LOCPLINT->showingDialog->setn(false);
	CGI->videoh->close();
}

CBattleOptionsWindow::CBattleOptionsWindow(const SDL_Rect & position, CBattleInterface *owner): myInt(owner)
{
	pos = position;
	background = BitmapHandler::loadBitmap("comopbck.bmp", true);
	graphics->blueToPlayersAdv(background, LOCPLINT->playerID);

	viewGrid = new CHighlightableButton(boost::bind(&CBattleInterface::setPrintCellBorders, owner, true), boost::bind(&CBattleInterface::setPrintCellBorders, owner, false), boost::assign::map_list_of(0,CGI->generaltexth->zelp[427].first)(3,CGI->generaltexth->zelp[427].first), CGI->generaltexth->zelp[427].second, false, "sysopchk.def", NULL, 185, 140, false);
	viewGrid->select(owner->settings.printCellBorders);
	movementShadow = new CHighlightableButton(boost::bind(&CBattleInterface::setPrintStackRange, owner, true), boost::bind(&CBattleInterface::setPrintStackRange, owner, false), boost::assign::map_list_of(0,CGI->generaltexth->zelp[428].first)(3,CGI->generaltexth->zelp[428].first), CGI->generaltexth->zelp[428].second, false, "sysopchk.def", NULL, 185, 173, false);
	movementShadow->select(owner->settings.printStackRange);
	mouseShadow = new CHighlightableButton(boost::bind(&CBattleInterface::setPrintMouseShadow, owner, true), boost::bind(&CBattleInterface::setPrintMouseShadow, owner, false), boost::assign::map_list_of(0,CGI->generaltexth->zelp[429].first)(3,CGI->generaltexth->zelp[429].first), CGI->generaltexth->zelp[429].second, false, "sysopchk.def", NULL, 185, 207, false);
	mouseShadow->select(owner->settings.printMouseShadow);

	animSpeeds = new CHighlightableButtonsGroup(0);
	animSpeeds->addButton(boost::assign::map_list_of(0,CGI->generaltexth->zelp[422].first),CGI->generaltexth->zelp[422].second, "sysopb9.def",188, 309, 1);
	animSpeeds->addButton(boost::assign::map_list_of(0,CGI->generaltexth->zelp[423].first),CGI->generaltexth->zelp[423].second, "sysob10.def",252, 309, 2);
	animSpeeds->addButton(boost::assign::map_list_of(0,CGI->generaltexth->zelp[424].first),CGI->generaltexth->zelp[424].second, "sysob11.def",315, 309, 4);
	animSpeeds->select(owner->getAnimSpeed(), 1);
	animSpeeds->onChange = boost::bind(&CBattleInterface::setAnimSpeed, owner, _1);

	setToDefault = new AdventureMapButton (CGI->generaltexth->zelp[392].first, CGI->generaltexth->zelp[392].second, boost::bind(&CBattleOptionsWindow::bDefaultf,this), 405, 443, "codefaul.def");
	std::swap(setToDefault->imgs[0][0], setToDefault->imgs[0][1]);
	exit = new AdventureMapButton (CGI->generaltexth->zelp[393].first, CGI->generaltexth->zelp[393].second, boost::bind(&CBattleOptionsWindow::bExitf,this), 516, 443, "soretrn.def",SDLK_RETURN);
	std::swap(exit->imgs[0][0], exit->imgs[0][1]);

	//printing texts to background
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[392], 240, 32, GEOR16, tytulowy, background); //window title
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[393], 122, 211, GEOR16, tytulowy, background); //animation speed
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[394], 122, 292, GEOR16, tytulowy, background); //music volume
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[395], 122, 358, GEOR16, tytulowy, background); //effects' volume
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[396], 353, 64, GEOR16, tytulowy, background); //auto - combat options
	CSDL_Ext::printAtMiddle(CGI->generaltexth->allTexts[397], 353, 264, GEOR16, tytulowy, background); //creature info

		//auto - combat options
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[398], 283, 87, GEOR16, zwykly, background); //creatures
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[399], 283, 117, GEOR16, zwykly, background); //spells
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[400], 283, 147, GEOR16, zwykly, background); //catapult
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[151], 283, 177, GEOR16, zwykly, background); //ballista
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[401], 283, 207, GEOR16, zwykly, background); //first aid tent

		//creature info
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[402], 283, 286, GEOR16, zwykly, background); //all stats
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[403], 283, 316, GEOR16, zwykly, background); //spells only
	
		//general options
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[404], 61, 58, GEOR16, zwykly, background); //hex grid
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[405], 61, 91, GEOR16, zwykly, background); //movement shadow
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[406], 61, 124, GEOR16, zwykly, background); //cursor shadow
	CSDL_Ext::printAt(CGI->generaltexth->allTexts[577], 61, 157, GEOR16, zwykly, background); //spellbook animation
	//texts printed
}

CBattleOptionsWindow::~CBattleOptionsWindow()
{
	SDL_FreeSurface(background);

	delete setToDefault;
	delete exit;

	delete viewGrid;
	delete movementShadow;
	delete animSpeeds;
	delete mouseShadow;
}

void CBattleOptionsWindow::activate()
{
	setToDefault->activate();
	exit->activate();
	viewGrid->activate();
	movementShadow->activate();
	animSpeeds->activate();
	mouseShadow->activate();
}

void CBattleOptionsWindow::deactivate()
{
	setToDefault->deactivate();
	exit->deactivate();
	viewGrid->deactivate();
	movementShadow->deactivate();
	animSpeeds->deactivate();
	mouseShadow->deactivate();
}

void CBattleOptionsWindow::show(SDL_Surface *to)
{
	if(!to) //"evaluating" to
		to = screen;

	SDL_BlitSurface(background, NULL, to, &pos);

	setToDefault->show(to);
	exit->show(to);
	viewGrid->show(to);
	movementShadow->show(to);
	animSpeeds->show(to);
	mouseShadow->show(to);
}

void CBattleOptionsWindow::bDefaultf()
{
}

void CBattleOptionsWindow::bExitf()
{
	GH.popIntTotally(this);
}

std::string CBattleInterface::SiegeHelper::townTypeInfixes[F_NUMBER] = {"CS", "RM", "TW", "IN", "NC", "DN", "ST", "FR", "EL"};

CBattleInterface::SiegeHelper::SiegeHelper(const CGTownInstance *siegeTown, const CBattleInterface * _owner)
: town(siegeTown), owner(_owner)
{
	for(int g=0; g<ARRAY_COUNT(walls); ++g)
	{
		walls[g] = BitmapHandler::loadBitmap( getSiegeName(g) );
	}
}

CBattleInterface::SiegeHelper::~SiegeHelper()
{
	for(int g=0; g<ARRAY_COUNT(walls); ++g)
	{
		SDL_FreeSurface(walls[g]);
	}
}

std::string CBattleInterface::SiegeHelper::getSiegeName(ui16 what, ui16 additInfo) const
{
	if(what == 2 || what == 3 || what == 8)
	{
		if(additInfo == 3) additInfo = 2;
	}
	char buf[100];
	SDL_itoa(additInfo, buf, 10);
	std::string addit(buf);
	switch(what)
	{
	case 0: //background
		return "SG" + townTypeInfixes[town->town->typeID] + "BACK.BMP";
	case 1: //background wall
		{
			switch(town->town->typeID)
			{
			case 5: case 4: case 1: case 6:
				return "SG" + townTypeInfixes[town->town->typeID] + "TPW1.BMP";
			case 0: case 2: case 3: case 7: case 8:
				return "SG" + townTypeInfixes[town->town->typeID] + "TPWL.BMP";
			default:
				return "";
			}
		}
	case 2: //keep
		return "SG" + townTypeInfixes[town->town->typeID] + "MAN" + addit + ".BMP";
	case 3: //bottom tower
		return "SG" + townTypeInfixes[town->town->typeID] + "TW1" + addit + ".BMP";
	case 4: //bottom wall
		return "SG" + townTypeInfixes[town->town->typeID] + "WA1" + addit + ".BMP";
	case 5: //below gate
		return "SG" + townTypeInfixes[town->town->typeID] + "WA3" + addit + ".BMP";
	case 6: //over gate
		return "SG" + townTypeInfixes[town->town->typeID] + "WA4" + addit + ".BMP";
	case 7: //upper wall
		return "SG" + townTypeInfixes[town->town->typeID] + "WA6" + addit + ".BMP";
	case 8: //upper tower
		return "SG" + townTypeInfixes[town->town->typeID] + "TW2" + addit + ".BMP";
	case 9: //gate
		return "SG" + townTypeInfixes[town->town->typeID] + "DRW" + addit + ".BMP";
	case 10: //gate arch
		return "SG" + townTypeInfixes[town->town->typeID] + "ARCH.BMP";
	case 11: //bottom static wall
		return "SG" + townTypeInfixes[town->town->typeID] + "WA2.BMP";
	case 12: //upper static wall
		return "SG" + townTypeInfixes[town->town->typeID] + "WA5.BMP";
	case 13: //moat
		return "SG" + townTypeInfixes[town->town->typeID] + "MOAT.BMP";
	case 14: //mlip
		return "SG" + townTypeInfixes[town->town->typeID] + "MLIP.BMP";
	case 15: //keep creature cover
		return "SG" + townTypeInfixes[town->town->typeID] + "MANC.BMP";
	case 16: //bottom turret creature cover
		return "SG" + townTypeInfixes[town->town->typeID] + "TW1C.BMP";
	case 17: //upper turret creature cover
		return "SG" + townTypeInfixes[town->town->typeID] + "TW2C.BMP";
	default:
		return "";
	}
}

void CBattleInterface::SiegeHelper::printPartOfWall(SDL_Surface * to, int what)
{
	Point pos = Point(-1, -1);
	switch(what)
	{
	case 1: //background wall
		pos = Point(owner->pos.w + owner->pos.x - walls[1]->w, 55 + owner->pos.y);
		break;
	case 2: //keep
		pos = Point(owner->pos.w + owner->pos.x - walls[2]->w, 154 + owner->pos.y);
		break;
	case 3: //bottom tower
	case 4: //bottom wall
	case 5: //below gate
	case 6: //over gate
	case 7: //upper wall
	case 8: //upper tower
	case 9: //gate
	case 10: //gate arch
	case 11: //bottom static wall
	case 12: //upper static wall
		pos.x = CGI->heroh->wallPositions[town->town->typeID][what - 3].first + owner->pos.x;
		pos.y = CGI->heroh->wallPositions[town->town->typeID][what - 3].second + owner->pos.y;
		break;
	case 15: //keep creature cover
		pos = Point(owner->pos.w + owner->pos.x - walls[2]->w, 154 + owner->pos.y);
		break;
	case 16: //bottom turret creature cover
		pos.x = CGI->heroh->wallPositions[town->town->typeID][0].first + owner->pos.x;
		pos.y = CGI->heroh->wallPositions[town->town->typeID][0].second + owner->pos.y;
		break;
	case 17: //upper turret creature cover
		pos.x = CGI->heroh->wallPositions[town->town->typeID][5].first + owner->pos.x;
		pos.y = CGI->heroh->wallPositions[town->town->typeID][5].second + owner->pos.y;
		break;
	};

	if(pos.x != -1)
	{
		blitAt(walls[what], pos.x, pos.y, to);
	}
}

void CStackQueue::update()
{
	stacksSorted.clear();
	LOCPLINT->cb->getStackQueue(stacksSorted, QUEUE_SIZE);
	for (int i = 0; i < QUEUE_SIZE ; i++)
	{
		stackBoxes[i]->setStack(stacksSorted[i]);
	}
}

CStackQueue::CStackQueue(bool Embedded)
:embedded(Embedded)
{
	OBJ_CONSTRUCTION_CAPTURING_ALL;
	if(embedded)
	{
		box = NULL;
		bg = NULL;
		pos.w = QUEUE_SIZE * 37;
		pos.h = 32; //height of small creature img
		pos.x = screen->w/2 - pos.w/2;
		pos.y = (screen->h - 600)/2 + 10;
	}
	else
	{
		box = BitmapHandler::loadBitmap("CHRROP.pcx");
		bg = BitmapHandler::loadBitmap("DIBOXPI.pcx");
		pos.w = 600;
		pos.h = bg->h;
	}

	stackBoxes.resize(QUEUE_SIZE);
	for (int i = 0; i < QUEUE_SIZE; i++)
	{
		stackBoxes[i] = new StackBox(box);
		stackBoxes[i]->pos.x += 6 + (embedded ? 37 : 79)*i;
	}
}

CStackQueue::~CStackQueue()
{
	SDL_FreeSurface(box);
}

void CStackQueue::showAll( SDL_Surface *to )
{
	blitBg(to);

	CIntObject::showAll(to);
}

void CStackQueue::blitBg( SDL_Surface * to )
{
	if(bg)
	{
		for (int w = 0; w < pos.w; w += bg->w)
		{
			blitAtLoc(bg, w, 0, to);		
		}
	}
}

void CStackQueue::StackBox::showAll( SDL_Surface *to )
{
	assert(my);
	if(bg)
	{
		graphics->blueToPlayersAdv(bg, my->owner);
		SDL_UpdateRect(bg, 0, 0, 0, 0);
		blitAt(bg, pos, to);
		blitAt(graphics->bigImgs[my->creature->idNumber], pos.x +9, pos.y + 1, to);
		printAtMiddleLoc(makeNumberShort(my->amount), pos.w/2, pos.h - 12, FONT_MEDIUM, zwykly, to);
	}
	else
	{
		blitAt(graphics->smallImgs[-2], pos, to);
		blitAt(graphics->smallImgs[my->creature->idNumber], pos, to);
		const SDL_Color &ownerColor = (my->owner == 255 ? *graphics->neutralColor : graphics->playerColors[my->owner]);
		CSDL_Ext::drawBorder(to, pos, int3(ownerColor.r, ownerColor.g, ownerColor.b));
		printAtMiddleLoc(makeNumberShort(my->amount), pos.w/2, pos.h - 8, FONT_TINY, zwykly, to);
	}
}

void CStackQueue::StackBox::setStack( const CStack *nStack )
{
	my = nStack;
}

CStackQueue::StackBox::StackBox(SDL_Surface *BG)
:bg(BG), my(NULL)
{
	if(bg)
	{
		pos.w = bg->w;
		pos.h = bg->h;
	}
	else
	{
		pos.w = pos.h = 32;
	}

	pos.y += 2;
}

CStackQueue::StackBox::~StackBox()
{
}

void CStackQueue::StackBox::hover( bool on )
{

}

BattleSettings::BattleSettings()
{
	printCellBorders = true;
	printStackRange = true;
	animSpeed = 2;
	printMouseShadow = true;
	showQueue = true;
}