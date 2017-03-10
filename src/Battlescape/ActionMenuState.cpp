/*
 * Copyright 2010-2016 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "ActionMenuState.h"
#include "../Engine/Game.h"
#include "../Engine/Options.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Action.h"
#include "../Engine/Sound.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "ActionMenuItem.h"
#include "PrimeGrenadeState.h"
#include "MedikitState.h"
#include "ScannerState.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "Pathfinding.h"
#include "TileEngine.h"
#include "../Interface/Text.h"

namespace OpenXcom
{

/**
 * Initializes all the elements in the Action Menu window.
 * @param game Pointer to the core game.
 * @param action Pointer to the action.
 * @param x Position on the x-axis.
 * @param y position on the y-axis.
 */
ActionMenuState::ActionMenuState(BattleAction *action, int x, int y) : _action(action)
{
	_screen = false;

	// Set palette
	_game->getSavedGame()->getSavedBattle()->setPaletteByDepth(this);

	for (int i = 0; i < 6; ++i)
	{
		_actionMenu[i] = new ActionMenuItem(i, _game, x, y);
		add(_actionMenu[i]);
		_actionMenu[i]->setVisible(false);
		_actionMenu[i]->onMouseClick((ActionHandler)&ActionMenuState::btnActionMenuItemClick);
	}

	// Build up the popup menu
	int id = 0;
	const RuleItem *weapon = _action->weapon->getRules();

	// throwing (if not a fixed weapon)
	if (!weapon->isFixed() && weapon->getCostThrow().Time > 0)
	{
		addItem(BA_THROW, "STR_THROW", &id, Options::keyBattleActionItem5);
	}

	if (!Options::showGunMeleeOnTop && weapon->getCostMelee().Time > 0)
	{
		// stun rod
		if (weapon->getBattleType() == BT_MELEE && weapon->getDamageType()->ResistType == DT_STUN)
		{
			addItem(BA_HIT, "STR_STUN", &id, Options::keyBattleActionItem4);
		}
		else
			// melee weapon
		{
			addItem(BA_HIT, "STR_HIT_MELEE", &id, Options::keyBattleActionItem4);
		}
	}

	// execute / break neck / cut throat / coup de grace
	if (Options::executeUnconsciousEnemies && (_action->weapon->getUnit() && _action->weapon->getUnit()->getStatus() == STATUS_UNCONSCIOUS))
	{
		BattleItem *otherWeapon = 0;

		// check left hand for secondary weapon
		BattleItem *leftHandWeapon = _game->getSavedGame()->getSavedBattle()->getSelectedUnit()->getItem("STR_LEFT_HAND");
		if (leftHandWeapon)
		{
			if (leftHandWeapon->getRules()->getCostMelee().Time > 0)
			{
				// melee weapons with melee attack (i.e. excl. stun weapons and others)
				if (leftHandWeapon->getRules()->getBattleType() == BT_MELEE && leftHandWeapon->getRules()->getDamageType()->ResistType == DT_MELEE)
				{
					otherWeapon = leftHandWeapon;
				}
			}
		}

		// check right hand for secondary weapon
		BattleItem *rightHandWeapon = _game->getSavedGame()->getSavedBattle()->getSelectedUnit()->getItem("STR_RIGHT_HAND");
		if (rightHandWeapon)
		{
			if (rightHandWeapon->getRules()->getCostMelee().Time > 0)
			{
				// melee weapons with melee attack (i.e. excl. stun weapons and others)
				if (rightHandWeapon->getRules()->getBattleType() == BT_MELEE && rightHandWeapon->getRules()->getDamageType()->ResistType == DT_MELEE)
				{
					otherWeapon = rightHandWeapon;
				}
			}
		}

		if (otherWeapon != 0)
		{
			addItem(BA_EXECUTE, "STR_CUT_THROAT", &id, Options::keyBattleActionItem1, otherWeapon);
			return; // hotkey safety!
		}
	}

	if (weapon->isPsiRequired() && _action->actor->getBaseStats()->psiSkill <= 0)
	{
		return;
	}

	// priming
	if (weapon->getFuseTimerDefault() >= 0 )
	{
		if (_action->weapon->getFuseTimer() == -1)
		{
			if (weapon->getCostPrime().Time > 0)
			{
				addItem(BA_PRIME, weapon->getPrimeActionName(), &id, Options::keyBattleActionItem1); // FIXME: hotkey safety?!
			}
		}
		else
		{
			if (weapon->getCostUnprime().Time > 0 && !weapon->getUnprimeActionName().empty())
			{
				addItem(BA_UNPRIME, weapon->getUnprimeActionName(), &id, Options::keyBattleActionItem2); // FIXME: hotkey safety?!
			}
		}
	}

	if (weapon->getBattleType() == BT_FIREARM)
	{
		if (weapon->getWaypoints() != 0 || (_action->weapon->getAmmoItem() && _action->weapon->getAmmoItem()->getRules()->getWaypoints() != 0))
		{
			addItem(BA_LAUNCH, "STR_LAUNCH_MISSILE", &id, Options::keyBattleActionItem1);
		}
		else
		{
			if (weapon->getCostAuto().Time > 0)
			{
				addItem(BA_AUTOSHOT, "STR_AUTO_SHOT", &id, Options::keyBattleActionItem3);
			}
			if (weapon->getCostSnap().Time > 0)
			{
				addItem(BA_SNAPSHOT, "STR_SNAP_SHOT", &id, Options::keyBattleActionItem2);
			}
			if (weapon->getCostAimed().Time > 0)
			{
				addItem(BA_AIMEDSHOT, "STR_AIMED_SHOT", &id, Options::keyBattleActionItem1);
			}
		}
	}

	if (Options::showGunMeleeOnTop && weapon->getCostMelee().Time > 0)
	{
		// stun rod
		if (weapon->getBattleType() == BT_MELEE && weapon->getDamageType()->ResistType == DT_STUN)
		{
			addItem(BA_HIT, "STR_STUN", &id, Options::keyBattleActionItem4);
		}
		else
			// melee weapon
		{
			addItem(BA_HIT, "STR_HIT_MELEE", &id, Options::keyBattleActionItem4);
		}
	}

	// special items
	if (weapon->getBattleType() == BT_MEDIKIT)
	{
		addItem(BA_USE, "STR_USE_MEDI_KIT", &id, Options::keyBattleActionItem1);
	}
	else if (weapon->getBattleType() == BT_SCANNER)
	{
		addItem(BA_USE, "STR_USE_SCANNER", &id, Options::keyBattleActionItem1);
	}
	else if (weapon->getBattleType() == BT_PSIAMP)
	{
		if (weapon->getCostMind().Time > 0)
		{
			addItem(BA_MINDCONTROL, "STR_MIND_CONTROL", &id, Options::keyBattleActionItem3);
		}
		if (weapon->getCostPanic().Time > 0)
		{
			addItem(BA_PANIC, "STR_PANIC_UNIT", &id, Options::keyBattleActionItem2);
		}
		if (weapon->getCostUse().Time > 0)
		{
			addItem(BA_USE, weapon->getPsiAttackName(), &id, Options::keyBattleActionItem1);
		}
	}
	else if (weapon->getBattleType() == BT_MINDPROBE)
	{
		addItem(BA_USE, "STR_USE_MIND_PROBE", &id, Options::keyBattleActionItem1);
	}

}

/**
 * Deletes the ActionMenuState.
 */
ActionMenuState::~ActionMenuState()
{

}

/**
 * Init function.
 */
void ActionMenuState::init()
{
	if (!_actionMenu[0]->getVisible())
	{
		// Item don't have any actions, close popup.
		_game->popState();
	}
}

/**
 * Adds a new menu item for an action.
 * @param ba Action type.
 * @param name Action description.
 * @param id Pointer to the new item ID.
 */
void ActionMenuState::addItem(BattleActionType ba, const std::string &name, int *id, SDLKey key, BattleItem *secondaryWeapon)
{
	std::wstring s1, s2;
	int acc = _action->actor->getFiringAccuracy(ba, _action->weapon, _game->getMod());
	if (secondaryWeapon != 0)
	{
		// for display only, this action will never miss anyway (alien is unconscious, how could you miss?)
		acc = 999;
		// backup the original "weapon" (i.e. unconscious alien) for later use (when we need to execute them)
		_action->origWeapon = _action->weapon;
		// this is actually important, so that we spend TUs (and other stats) correctly
		_action->weapon = secondaryWeapon;
	}
	int tu = _action->actor->getActionTUs(ba, (secondaryWeapon == 0) ? _action->weapon : secondaryWeapon).Time;

	if (ba == BA_THROW || ba == BA_AIMEDSHOT || ba == BA_SNAPSHOT || ba == BA_AUTOSHOT || ba == BA_LAUNCH || ba == BA_HIT || ba == BA_EXECUTE)
		s1 = tr("STR_ACCURACY_SHORT").arg(Text::formatPercentage(acc));
	s2 = tr("STR_TIME_UNITS_SHORT").arg(tu);
	_actionMenu[*id]->setAction(ba, tr(name), s1, s2, tu);
	_actionMenu[*id]->setVisible(true);
	_actionMenu[*id]->onKeyboardPress((ActionHandler)&ActionMenuState::btnActionMenuItemClick, key);
	(*id)++;
}

/**
 * Closes the window on right-click.
 * @param action Pointer to an action.
 */
void ActionMenuState::handle(Action *action)
{
	State::handle(action);
	if (action->getDetails()->type == SDL_MOUSEBUTTONDOWN && action->getDetails()->button.button == SDL_BUTTON_RIGHT)
	{
		_game->popState();
	}
	else if (action->getDetails()->type == SDL_KEYDOWN &&
		(action->getDetails()->key.keysym.sym == Options::keyCancel ||
		action->getDetails()->key.keysym.sym == Options::keyBattleUseLeftHand ||
		action->getDetails()->key.keysym.sym == Options::keyBattleUseRightHand))
	{
		_game->popState();
	}
}

/**
 * Executes the action corresponding to this action menu item.
 * @param action Pointer to an action.
 */
void ActionMenuState::btnActionMenuItemClick(Action *action)
{
	_game->getSavedGame()->getSavedBattle()->getPathfinding()->removePreview();

	int btnID = -1;
	const RuleItem *weapon = _action->weapon->getRules();

	// got to find out which button was pressed
	for (size_t i = 0; i < sizeof(_actionMenu)/sizeof(_actionMenu[0]) && btnID == -1; ++i)
	{
		if (action->getSender() == _actionMenu[i])
		{
			btnID = i;
		}
	}

	if (btnID != -1)
	{
		bool newHitLog = false;

		_action->type = _actionMenu[btnID]->getAction();
		_action->updateTU();
		if (_action->type != BA_THROW &&
			_action->actor->getOriginalFaction() == FACTION_PLAYER &&
			!_game->getSavedGame()->isResearched(weapon->getRequirements()))
		{
			_action->result = "STR_UNABLE_TO_USE_ALIEN_ARTIFACT_UNTIL_RESEARCHED";
			_game->popState();
		}
		else if (weapon->isWaterOnly() &&
			_game->getSavedGame()->getSavedBattle()->getDepth() == 0 &&
			_action->type != BA_THROW)
		{
			_action->result = "STR_UNDERWATER_EQUIPMENT";
			_game->popState();
		}
		else if (_action->type != BA_THROW &&
			_action->actor->getFaction() == FACTION_PLAYER &&
			weapon->isBlockingBothHands() &&
			_action->actor->getLeftHandWeapon() != 0 &&
			_action->actor->getRightHandWeapon() != 0)
		{
			_action->result = "STR_MUST_USE_BOTH_HANDS";
			_game->popState();
		}
		else if (_action->type == BA_PRIME)
		{
			const BattleFuseType fuseType = weapon->getFuseTimerType();
			if (fuseType == BFT_SET)
			{
				_game->pushState(new PrimeGrenadeState(_action, false, 0));
			}
			else
			{
				_action->value = weapon->getFuseTimerDefault();
				_game->popState();
			}
		}
		else if (_action->type == BA_UNPRIME)
		{
			_game->popState();
		}
		else if (_action->type == BA_USE && weapon->getBattleType() == BT_MEDIKIT)
		{
			BattleUnit *targetUnit = 0;
			TileEngine *tileEngine = _game->getSavedGame()->getSavedBattle()->getTileEngine();
			const std::vector<BattleUnit*> *units = _game->getSavedGame()->getSavedBattle()->getUnits();
			for (std::vector<BattleUnit*>::const_iterator i = units->begin(); i != units->end() && !targetUnit; ++i)
			{
				// we can heal a unit that is at the same position, unconscious and healable(=woundable)
				if ((*i)->getPosition() == _action->actor->getPosition() && *i != _action->actor && (*i)->getStatus() == STATUS_UNCONSCIOUS && (*i)->isWoundable())
				{
					targetUnit = *i;
				}
			}
			if (!targetUnit)
			{
				if (tileEngine->validMeleeRange(
					_action->actor->getPosition(),
					_action->actor->getDirection(),
					_action->actor,
					0, &_action->target, false))
				{
					Tile *tile = _game->getSavedGame()->getSavedBattle()->getTile(_action->target);
					if (tile != 0 && tile->getUnit() && tile->getUnit()->isWoundable())
					{
						targetUnit = tile->getUnit();
					}
				}
			}
			if (!targetUnit && weapon->getAllowSelfHeal())
			{
				targetUnit = _action->actor;
			}
			if (targetUnit)
			{
				_game->popState();
				BattleMediKitType type = weapon->getMediKitType();
				if (type)
				{
					if ((type == BMT_HEAL && _action->weapon->getHealQuantity() > 0) ||
						(type == BMT_STIMULANT && _action->weapon->getStimulantQuantity() > 0) ||
						(type == BMT_PAINKILLER && _action->weapon->getPainKillerQuantity() > 0))
					{
						if (_action->spendTU(&_action->result))
						{
							switch (type)
							{
							case BMT_HEAL:
								if (targetUnit->getFatalWounds())
								{
									for (int i = 0; i < BODYPART_MAX; ++i)
									{
										if (targetUnit->getFatalWound(i))
										{
											tileEngine->medikitHeal(_action, targetUnit, i);
											tileEngine->medikitRemoveIfEmpty(_action);
											break;
										}
									}
								}
								else
								{
									tileEngine->medikitHeal(_action, targetUnit, BODYPART_TORSO);
									tileEngine->medikitRemoveIfEmpty(_action);
								}
								break;
							case BMT_STIMULANT:
								tileEngine->medikitStimulant(_action, targetUnit);
								tileEngine->medikitRemoveIfEmpty(_action);
								break;
							case BMT_PAINKILLER:
								tileEngine->medikitPainKiller(_action, targetUnit);
								tileEngine->medikitRemoveIfEmpty(_action);
								break;
							case BMT_NORMAL:
								break;
							}
						}
					}
					else
					{
						_action->result = "STR_NO_USES_LEFT";
					}
				}
				else
				{
					_game->pushState(new MedikitState(targetUnit, _action, tileEngine));
				}
			}
			else
			{
				_action->result = "STR_THERE_IS_NO_ONE_THERE";
				_game->popState();
			}
		}
		else if (_action->type == BA_USE && weapon->getBattleType() == BT_SCANNER)
		{
			// spend TUs first, then show the scanner
			if (_action->spendTU(&_action->result))
			{
				_game->popState();
				_game->pushState (new ScannerState(_action));
			}
			else
			{
				_game->popState();
			}
		}
		else if (_action->type == BA_LAUNCH)
		{
			// check beforehand if we have enough time units
			if (!_action->haveTU(&_action->result))
			{
				//nothing
			}
			else if (_action->weapon->getAmmoItem() ==0 || (_action->weapon->getAmmoItem() && _action->weapon->getAmmoItem()->getAmmoQuantity() == 0))
			{
				_action->result = "STR_NO_AMMUNITION_LOADED";
			}
			else
			{
				_action->targeting = true;
				newHitLog = true;
			}
			_game->popState();
		}
		else if (_action->type == BA_HIT)
		{
			// check beforehand if we have enough time units
			if (!_action->haveTU(&_action->result))
			{
				//nothing
			}
			else if (!_game->getSavedGame()->getSavedBattle()->getTileEngine()->validMeleeRange(
				_action->actor->getPosition(),
				_action->actor->getDirection(),
				_action->actor,
				0, &_action->target))
			{
				_action->result = "STR_THERE_IS_NO_ONE_THERE";
			}
			else
			{
				newHitLog = true;
			}
			_game->popState();
		}
		else if (_action->type == BA_EXECUTE)
		{
			if (_action->spendTU(&_action->result))
			{
				if (_action->origWeapon != 0)
				{
					// kill unit
					_action->origWeapon->getUnit()->instaKill();
					// convert inventory item to corpse
					RuleItem *corpseRules = _game->getMod()->getItem(_action->origWeapon->getUnit()->getArmor()->getCorpseBattlescape()[0]); // we're in an inventory, so we must be a 1x1 unit
					_action->origWeapon->convertToCorpse(corpseRules);
					// inform the player
					_action->result = "STR_TARGET_WAS_EXECUTED";
					// audio feedback
					if (_action->weapon->getRules()->getMeleeHitSound() > -1)
					{
						_game->getMod()->getSoundByDepth(_game->getSavedGame()->getSavedBattle()->getDepth(), _action->weapon->getRules()->getMeleeHitSound())->play();
					}
				}
			}
			_game->popState();
		}
		else
		{
			_action->targeting = true;
			newHitLog = true;
			_game->popState();
		}

		if (newHitLog)
		{
			// start new hit log
			_game->getSavedGame()->getSavedBattle()->hitLog.str(L"");
			_game->getSavedGame()->getSavedBattle()->hitLog.clear();
			// log weapon
			_game->getSavedGame()->getSavedBattle()->hitLog << "Weapon: " << tr(weapon->getType()) << "\n\n";
		}
	}
}

/**
 * Updates the scale.
 * @param dX delta of X;
 * @param dY delta of Y;
 */
void ActionMenuState::resize(int &dX, int &dY)
{
	State::recenter(dX, dY * 2);
}

}
