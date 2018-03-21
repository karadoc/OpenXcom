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

#include <sstream>
#include <algorithm>
#include "Ufopaedia.h"
#include "ArticleStateItem.h"
#include "../Mod/Mod.h"
#include "../Mod/ArticleDefinition.h"
#include "../Mod/RuleItem.h"
#include "../Engine/Game.h"
#include "../Engine/Palette.h"
#include "../Engine/Surface.h"
#include "../Engine/LocalizedText.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
#include "../Interface/TextList.h"
#include "../Mod/RuleInterface.h"

namespace OpenXcom
{

	ArticleStateItem::ArticleStateItem(ArticleDefinitionItem *defs) : ArticleState(defs->id)
	{
		RuleItem *item = _game->getMod()->getItem(defs->id, true);

		// add screen elements
		_txtTitle = new Text(148, 32, 5, 24);
		_txtWeight = new Text(88, 8, 104, 55);

		// Set palette
		setPalette("PAL_BATTLEPEDIA");

		_buttonColor = _game->getMod()->getInterface("articleItem")->getElement("button")->color;
		_textColor = _game->getMod()->getInterface("articleItem")->getElement("text")->color;
		_listColor1 = _game->getMod()->getInterface("articleItem")->getElement("list")->color;
		_listColor2 = _game->getMod()->getInterface("articleItem")->getElement("list")->color2;
		_ammoColor = _game->getMod()->getInterface("articleItem")->getElement("ammoColor")->color;

		ArticleState::initLayout();

		// add other elements
		add(_txtTitle);
		add(_txtWeight);

		// Set up objects
		_game->getMod()->getSurface("BACK08.SCR")->blit(_bg);
		_btnOk->setColor(_buttonColor);
		_btnPrev->setColor(_buttonColor);
		_btnNext->setColor(_buttonColor);
		_btnInfo->setColor(_buttonColor);
		_btnInfo->setVisible(true);

		_txtTitle->setColor(_textColor);
		_txtTitle->setBig();
		_txtTitle->setWordWrap(true);
		_txtTitle->setText(tr(defs->title));

		_txtWeight->setColor(_textColor);
		_txtWeight->setAlign(ALIGN_RIGHT);

		// IMAGE
		_image = new Surface(32, 48, 157, 5);
		add(_image);

		item->drawHandSprite(_game->getMod()->getSurfaceSet("BIGOBS.PCK"), _image);

		const std::vector<std::string> *ammo_data = item->getPrimaryCompatibleAmmo();

		int weight = item->getWeight();
		std::wstring weightLabel = tr("STR_WEIGHT_PEDIA1").arg(weight);
		if (!ammo_data->empty())
		{
			// Note: weight including primary ammo only!
			RuleItem *ammo_rule = _game->getMod()->getItem((*ammo_data)[0]);
			weightLabel = tr("STR_WEIGHT_PEDIA2").arg(weight).arg(weight + ammo_rule->getWeight());
		}
		_txtWeight->setText(weight > 0 ? weightLabel : L"");

		// SHOT STATS TABLE (for firearms and melee only)
		if (item->getBattleType() == BT_FIREARM || item->getBattleType() == BT_MELEE)
		{
			_txtShotType = new Text(100, 17, 8, 66);
			add(_txtShotType);
			_txtShotType->setColor(_textColor);
			_txtShotType->setWordWrap(true);
			_txtShotType->setText(tr("STR_SHOT_TYPE"));

			_txtAccuracy = new Text(50, 17, 104, 66);
			add(_txtAccuracy);
			_txtAccuracy->setColor(_textColor);
			_txtAccuracy->setWordWrap(true);
			_txtAccuracy->setText(tr("STR_ACCURACY_UC"));

			_txtTuCost = new Text(60, 17, 158, 66);
			add(_txtTuCost);
			_txtTuCost->setColor(_textColor);
			_txtTuCost->setWordWrap(true);
			_txtTuCost->setText(tr("STR_TIME_UNIT_COST"));

			_lstInfo = new TextList(204, 55, 8, 82);
			add(_lstInfo);

			_lstInfo->setColor(_listColor2); // color for %-data!
			_lstInfo->setColumns(3, 100, 52, 52);
			_lstInfo->setBig();
		}

		if (item->getBattleType() == BT_FIREARM)
		{
			int current_row = 0;
			if (item->getCostAuto().Time>0)
			{
				std::wstring tu = Text::formatPercentage(item->getCostAuto().Time);
				if (item->getFlatAuto().Time)
				{
					tu.erase(tu.end() - 1);
				}
				_lstInfo->addRow(3,
								 tr("STR_SHOT_TYPE_AUTO").arg(item->getConfigAuto()->shots).c_str(),
								 Text::formatPercentage(item->getAccuracyAuto()).c_str(),
								 tu.c_str());
				_lstInfo->setCellColor(current_row, 0, _listColor1);
				current_row++;
			}

			if (item->getCostSnap().Time>0)
			{
				std::wstring tu = Text::formatPercentage(item->getCostSnap().Time);
				if (item->getFlatSnap().Time)
				{
					tu.erase(tu.end() - 1);
				}
				_lstInfo->addRow(3,
								 tr("STR_SHOT_TYPE_SNAP").arg(item->getConfigSnap()->shots).c_str(),
								 Text::formatPercentage(item->getAccuracySnap()).c_str(),
								 tu.c_str());
				_lstInfo->setCellColor(current_row, 0, _listColor1);
				current_row++;
			}

			if (item->getCostAimed().Time>0)
			{
				std::wstring tu = Text::formatPercentage(item->getCostAimed().Time);
				if (item->getFlatAimed().Time)
				{
					tu.erase(tu.end() - 1);
				}
				_lstInfo->addRow(3,
								 tr("STR_SHOT_TYPE_AIMED").arg(item->getConfigAimed()->shots).c_str(),
								 Text::formatPercentage(item->getAccuracyAimed()).c_str(),
								 tu.c_str());
				_lstInfo->setCellColor(current_row, 0, _listColor1);
				current_row++;
			}

			// text_info is BELOW the info table (table can have 0-3 rows)
			int shift = (3 - current_row) * 16;
			if (ammo_data->size() == 2 && current_row <= 1)
			{
				shift -= (2 - current_row) * 16;
			}
			_txtInfo = new Text((ammo_data->size()<3 ? 300 : 180), 56 + shift, 8, 138 - shift);
		}
		else if (item->getBattleType() == BT_MELEE)
		{
			if (item->getCostMelee().Time > 0)
			{
				std::wstring tu = Text::formatPercentage(item->getCostMelee().Time);
				if (item->getFlatMelee().Time)
				{
					tu.erase(tu.end() - 1);
				}
				_lstInfo->addRow(3,
					tr("STR_SHOT_TYPE_MELEE").c_str(),
					Text::formatPercentage(item->getAccuracyMelee()).c_str(),
					tu.c_str());
				_lstInfo->setCellColor(0, 0, _listColor1);
			}

			// text_info is BELOW the info table (with 1 row only)
			_txtInfo = new Text(300, 88, 8, 106);
		}
		else
		{
			// text_info is larger and starts on top
			_txtInfo = new Text(300, 125, 8, 67);
		}

		add(_txtInfo);

		_txtInfo->setColor(_textColor);
		_txtInfo->setWordWrap(true);
		_txtInfo->setText(tr(defs->text));


		// AMMO column
		std::wostringstream ss;

		for (int i = 0; i<3; ++i)
		{
			_txtAmmoType[i] = new Text(82, 16, 194, 20 + i*49);
			add(_txtAmmoType[i]);
			_txtAmmoType[i]->setColor(_textColor);
			_txtAmmoType[i]->setAlign(ALIGN_CENTER);
			_txtAmmoType[i]->setVerticalAlign(ALIGN_MIDDLE);
			_txtAmmoType[i]->setWordWrap(true);

			_txtAmmoDamage[i] = new Text(82, 17, 194, 40 + i*49);
			add(_txtAmmoDamage[i]);
			_txtAmmoDamage[i]->setColor(_ammoColor);
			_txtAmmoDamage[i]->setAlign(ALIGN_CENTER);
			_txtAmmoDamage[i]->setBig();

			_imageAmmo[i] = new Surface(32, 48, 280, 16 + i*49);
			add(_imageAmmo[i]);
		}

		switch (item->getBattleType())
		{
			case BT_FIREARM:
				_txtDamage = new Text(82, 10, 194, 7);
				add(_txtDamage);
				_txtDamage->setColor(_textColor);
				_txtDamage->setAlign(ALIGN_CENTER);
				_txtDamage->setText(tr("STR_DAMAGE_UC"));

				_txtAmmo = new Text(50, 10, 268, 7);
				add(_txtAmmo);
				_txtAmmo->setColor(_textColor);
				_txtAmmo->setAlign(ALIGN_CENTER);
				_txtAmmo->setText(tr("STR_AMMO"));

				if (ammo_data->empty())
				{
					_txtAmmoType[0]->setText(tr(getDamageTypeText(item->getDamageType()->ResistType)));

					ss.str(L"");ss.clear();
					ss << item->getPower();
					if (item->getShotgunPellets())
					{
						ss << L"x" << item->getShotgunPellets();
					}
					_txtAmmoDamage[0]->setText(ss.str());
					_txtAmmoDamage[0]->setColor(getDamageTypeTextColor(item->getDamageType()->ResistType));
				}
				else
				{
					for (size_t i = 0; i < std::min(ammo_data->size(), (size_t)3); ++i)
					{
						ArticleDefinition *ammo_article = _game->getMod()->getUfopaediaArticle((*ammo_data)[i], true);
						if (Ufopaedia::isArticleAvailable(_game->getSavedGame(), ammo_article))
						{
							RuleItem *ammo_rule = _game->getMod()->getItem((*ammo_data)[i], true);
							_txtAmmoType[i]->setText(tr(getDamageTypeText(ammo_rule->getDamageType()->ResistType)));

							ss.str(L"");ss.clear();
							ss << ammo_rule->getPower();
							if (ammo_rule->getShotgunPellets())
							{
								ss << L"x" << ammo_rule->getShotgunPellets();
							}
							_txtAmmoDamage[i]->setText(ss.str());
							_txtAmmoDamage[i]->setColor(getDamageTypeTextColor(ammo_rule->getDamageType()->ResistType));

							ammo_rule->drawHandSprite(_game->getMod()->getSurfaceSet("BIGOBS.PCK"), _imageAmmo[i]);
						}
					}
				}
				break;
			case BT_AMMO:
			case BT_GRENADE:
			case BT_PROXIMITYGRENADE:
			case BT_MELEE:
				_txtDamage = new Text(82, 10, 194, 7);
				add(_txtDamage);
				_txtDamage->setColor(_textColor);
				_txtDamage->setAlign(ALIGN_CENTER);
				_txtDamage->setText(tr("STR_DAMAGE_UC"));

				_txtAmmoType[0]->setText(tr(getDamageTypeText(item->getDamageType()->ResistType)));

				ss.str(L"");ss.clear();
				ss << item->getPower();
				if (item->getShotgunPellets())
				{
					ss << L"x" << item->getShotgunPellets();
				}
				_txtAmmoDamage[0]->setText(ss.str());
				_txtAmmoDamage[0]->setColor(getDamageTypeTextColor(item->getDamageType()->ResistType));
				break;
			default: break;
		}

		centerAllSurfaces();
	}

	ArticleStateItem::~ArticleStateItem()
	{}

	int ArticleStateItem::getDamageTypeTextColor(ItemDamageType dt)
	{
		Element *interfaceElement = 0;
		int color = _ammoColor;

		switch (dt)
		{
			case DT_NONE:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTNone");
				break;

			case DT_AP:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTAP");
				break;

			case DT_IN:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTIN");
				break;

			case DT_HE:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTHE");
				break;

			case DT_LASER:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTLaser");
				break;

			case DT_PLASMA:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTPlasma");
				break;

			case DT_STUN:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTStun");
				break;

			case DT_MELEE:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTMelee");
				break;

			case DT_ACID:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTAcid");
				break;

			case DT_SMOKE:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDTSmoke");
				break;

			case DT_10:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT10");
				break;

			case DT_11:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT11");
				break;

			case DT_12:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT12");
				break;

			case DT_13:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT13");
				break;

			case DT_14:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT14");
				break;

			case DT_15:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT15");
				break;

			case DT_16:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT16");
				break;

			case DT_17:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT17");
				break;

			case DT_18:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT18");
				break;

			case DT_19:
				interfaceElement = _game->getMod()->getInterface("articleItem")->getElement("ammoColorDT19");
				break;

			default :
				break;
		}

		if (interfaceElement)
		{
			color = interfaceElement->color;
		}

		return color;
	}
}
