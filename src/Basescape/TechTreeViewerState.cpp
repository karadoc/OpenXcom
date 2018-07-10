/*
 * Copyright 2010-2015 OpenXcom Developers.
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
#include "TechTreeViewerState.h"
#include "TechTreeSelectState.h"
#include "../Engine/Action.h"
#include "../Engine/Game.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleBaseFacility.h"
#include "../Mod/RuleInterface.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleManufacture.h"
#include "../Mod/RuleResearch.h"
#include "../Engine/LocalizedText.h"
#include "../Engine/Options.h"
#include "../Interface/Window.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
#include "../Interface/TextList.h"
#include "../Savegame/SavedGame.h"
#include <algorithm>
#include <unordered_set>

namespace OpenXcom
{

/**
 * Initializes all the elements on the UI.
 */
TechTreeViewerState::TechTreeViewerState(const RuleResearch *r, const RuleManufacture *m, const RuleBaseFacility *f)
{
	if (r != 0)
	{
		_selectedTopic = r->getName();
		_selectedFlag = TTV_RESEARCH;
	}
	else if (m != 0)
	{
		_selectedTopic = m->getName();
		_selectedFlag = TTV_MANUFACTURING;
	}
	else if (f != 0)
	{
		_selectedTopic = f->getType();
		_selectedFlag = TTV_FACILITIES;
	}

	// Create objects
	_window = new Window(this, 320, 200, 0, 0);
	_txtTitle = new Text(304, 17, 8, 7);
	_txtSelectedTopic = new Text(204, 9, 8, 24);
	_txtProgress = new Text(100, 9, 212, 24);
	_txtCostIndicator = new Text(100, 9, 16, 32); // experimental cost indicator
	_lstLeft = new TextList(132, 128, 8, 40);
	_lstRight = new TextList(132, 128, 164, 40);
	_btnNew = new TextButton(148, 16, 8, 176);
	_btnOk = new TextButton(148, 16, 164, 176);

	// Set palette
	setInterface("techTreeViewer");

	_purple = _game->getMod()->getInterface("techTreeViewer")->getElement("list")->color;
	_pink = _game->getMod()->getInterface("techTreeViewer")->getElement("list")->color2;
	_blue = _game->getMod()->getInterface("techTreeViewer")->getElement("list")->border;
	_white = _game->getMod()->getInterface("techTreeViewer")->getElement("listExtended")->color;
	_gold = _game->getMod()->getInterface("techTreeViewer")->getElement("listExtended")->color2;

	add(_window, "window", "techTreeViewer");
	add(_txtTitle, "text", "techTreeViewer");
	add(_txtSelectedTopic, "text", "techTreeViewer");
	add(_txtProgress, "text", "techTreeViewer");
	add(_txtCostIndicator, "text", "techTreeViewer");
	add(_lstLeft, "list", "techTreeViewer");
	add(_lstRight, "list", "techTreeViewer");
	add(_btnNew, "button", "techTreeViewer");
	add(_btnOk, "button", "techTreeViewer");

	centerAllSurfaces();

	// Set up objects
	_window->setBackground(_game->getMod()->getSurface("BACK05.SCR"));

	_txtTitle->setBig();
	_txtTitle->setAlign(ALIGN_CENTER);
	_txtTitle->setText(tr("STR_TECH_TREE_VIEWER"));

	_txtSelectedTopic->setText(tr("STR_TOPIC").arg(L""));

	_lstLeft->setColumns(1, 132);
	_lstLeft->setSelectable(true);
	_lstLeft->setBackground(_window);
	_lstLeft->setWordWrap(true);
	_lstLeft->onMouseClick((ActionHandler)&TechTreeViewerState::onSelectLeftTopic);

	_lstRight->setColumns(1, 132);
	_lstRight->setSelectable(true);
	_lstRight->setBackground(_window);
	_lstRight->setWordWrap(true);
	_lstRight->onMouseClick((ActionHandler)&TechTreeViewerState::onSelectRightTopic);

	_btnNew->setText(tr("STR_SELECT_TOPIC"));
	_btnNew->onMouseClick((ActionHandler)&TechTreeViewerState::btnNewClick);
	_btnNew->onKeyboardPress((ActionHandler)&TechTreeViewerState::btnNewClick, Options::keyToggleQuickSearch);

	_btnOk->setText(tr("STR_OK"));
	_btnOk->onMouseClick((ActionHandler)&TechTreeViewerState::btnOkClick);
	_btnOk->onKeyboardPress((ActionHandler)&TechTreeViewerState::btnOkClick, Options::keyCancel);

	int discoveredSum = 0;
	// pre-calculate globally
	const std::vector<const RuleResearch *> &discoveredResearch = _game->getSavedGame()->getDiscoveredResearch();
	for (std::vector<const RuleResearch *>::const_iterator j = discoveredResearch.begin(); j != discoveredResearch.end(); ++j)
	{
		_alreadyAvailableResearch.insert((*j)->getName());
		discoveredSum += (*j)->getCost();
	}

	int totalSum = 0;
	const std::vector<std::string> &allResearch = _game->getMod()->getResearchList();
	RuleResearch *resRule = 0;
	for (std::vector<std::string>::const_iterator j = allResearch.begin(); j != allResearch.end(); ++j)
	{
		resRule = _game->getMod()->getResearch((*j));
		if (resRule != 0)
		{
			totalSum += resRule->getCost();
		}
	}

	const std::vector<std::string> &allManufacturing = _game->getMod()->getManufactureList();
	RuleManufacture *manRule = 0;
	for (std::vector<std::string>::const_iterator iter = allManufacturing.begin(); iter != allManufacturing.end(); ++iter)
	{
		manRule = _game->getMod()->getManufacture(*iter);
		if (_game->getSavedGame()->isResearched(manRule->getRequirements()))
		{
			_alreadyAvailableManufacture.insert(manRule->getName());
		}
	}

	const std::vector<std::string> &facilities = _game->getMod()->getBaseFacilitiesList();
	RuleBaseFacility *facRule = 0;
	for (std::vector<std::string>::const_iterator iter = facilities.begin(); iter != facilities.end(); ++iter)
	{
		facRule = _game->getMod()->getBaseFacility(*iter);
		if (_game->getSavedGame()->isResearched(facRule->getRequirements()))
		{
			_alreadyAvailableFacilities.insert(facRule->getType());
		}
	}

	const std::vector<std::string> &allItems = _game->getMod()->getItemsList();
	RuleItem *itemRule = 0;
	for (std::vector<std::string>::const_iterator iter = allItems.begin(); iter != allItems.end(); ++iter)
	{
		itemRule = _game->getMod()->getItem(*iter);
		if (!itemRule->getRequirements().empty() || !itemRule->getBuyRequirements().empty())
		{
			_protectedItems.insert(itemRule->getType());
			if (_game->getSavedGame()->isResearched(itemRule->getRequirements()) && _game->getSavedGame()->isResearched(itemRule->getBuyRequirements()))
			{
				_alreadyAvailableItems.insert(itemRule->getType());
			}
		}
	}

	_txtProgress->setAlign(ALIGN_RIGHT);
	_txtProgress->setText(tr("STR_RESEARCH_PROGRESS").arg(discoveredSum * 100 / totalSum));
}

/**
 *
 */
TechTreeViewerState::~TechTreeViewerState()
{
}

/**
* Initializes the screen (fills the lists).
*/
void TechTreeViewerState::init()
{
	State::init();
	initLists();
}

/**
* Returns to the previous screen.
* @param action Pointer to an action.
*/
void TechTreeViewerState::btnOkClick(Action *)
{
	_game->popState();
}

/**
* Opens the Select Topic screen.
* @param action Pointer to an action.
*/
void TechTreeViewerState::btnNewClick(Action *)
{
	_game->pushState(new TechTreeSelectState(this));
}

/**
 * Shows the filtered topics.
 */
void TechTreeViewerState::initLists()
{
	// Set topic name
	{
		std::wostringstream ss;
		ss << tr(_selectedTopic);
		if (_selectedFlag == TTV_MANUFACTURING)
		{
			ss << tr("STR_M_FLAG");
			_txtCostIndicator->setText(L"");
		}
		else if (_selectedFlag == TTV_FACILITIES)
		{
			ss << tr("STR_F_FLAG");
			_txtCostIndicator->setText(L"");
		}
		else if (_selectedFlag == TTV_ITEMS)
		{
			ss << tr("STR_I_FLAG");
			_txtCostIndicator->setText(L"");
		}
		_txtSelectedTopic->setText(tr("STR_TOPIC").arg(ss.str()));
	}

	// reset
	_leftTopics.clear();
	_rightTopics.clear();
	_leftFlags.clear();
	_rightFlags.clear();
	_lstLeft->clearList();
	_lstRight->clearList();

	if (_selectedFlag == TTV_NONE)
	{
		return;
	}
	else if (_selectedFlag == TTV_RESEARCH)
	{
		int row = 0;
		RuleResearch *rule = _game->getMod()->getResearch(_selectedTopic);
		if (rule == 0)
			return;

		// Cost indicator
		{
			std::wostringstream ss;
			int cost = rule->getCost();
			std::vector<std::pair<int, std::wstring>> symbol_values
					({{100, L"#"}, {20, L"="}, {5, L"-"}});

			for (auto& sym : symbol_values)
			{
				while (cost >= std::get<0>(sym))
				{
					cost -= std::get<0>(sym);
					ss << std::get<1>(sym);
				}
			}
			_txtCostIndicator->setText(ss.str());
		}
		//

		const std::vector<std::string> &researchList = _game->getMod()->getResearchList();
		const std::vector<std::string> &manufactureList = _game->getMod()->getManufactureList();

		// 0. common pre-calc
		const std::vector<std::string> reqs = rule->getRequirements();
		const std::vector<std::string> deps = rule->getDependencies();
		std::vector<std::string> unlockedBy;
		std::vector<std::string> disabledBy;
		std::vector<std::string> getForFreeFrom;
		std::vector<std::string> requiredByResearch;
		std::vector<std::string> requiredByManufacture;
		std::vector<std::string> requiredByFacilities;
		std::vector<std::string> requiredByItems;
		std::vector<std::string> leadsTo;
		const std::vector<std::string> unlocks = rule->getUnlocked();
		const std::vector<std::string> disables = rule->getDisabled();
		const std::vector<std::string> free = rule->getGetOneFree();
		const std::map<std::string, std::vector<std::string> > freeProtected = rule->getGetOneFreeProtected();

		for (std::vector<std::string>::const_iterator j = manufactureList.begin(); j != manufactureList.end(); ++j)
		{
			RuleManufacture *temp = _game->getMod()->getManufacture(*j);
			for (std::vector<std::string>::const_iterator i = temp->getRequirements().begin(); i != temp->getRequirements().end(); ++i)
			{
				if (*i == rule->getName())
				{
					requiredByManufacture.push_back(*j);
				}
			}
		}

		for (auto &f : _game->getMod()->getBaseFacilitiesList())
		{
			RuleBaseFacility *temp = _game->getMod()->getBaseFacility(f);
			for (auto &i : temp->getRequirements())
			{
				if (i == rule->getName())
				{
					requiredByFacilities.push_back(f);
				}
			}
		}

		for (auto &item : _game->getMod()->getItemsList())
		{
			RuleItem *temp = _game->getMod()->getItem(item);
			for (auto &i : temp->getRequirements())
			{
				if (i == rule->getName())
				{
					requiredByItems.push_back(item);
				}
			}
			for (auto &i : temp->getBuyRequirements())
			{
				if (i == rule->getName())
				{
					requiredByItems.push_back(item);
				}
			}
		}

		for (std::vector<std::string>::const_iterator j = researchList.begin(); j != researchList.end(); ++j)
		{
			RuleResearch *temp = _game->getMod()->getResearch(*j);
			for (std::vector<std::string>::const_iterator i = temp->getUnlocked().begin(); i != temp->getUnlocked().end(); ++i)
			{
				if (*i == rule->getName())
				{
					unlockedBy.push_back(*j);
				}
			}
			for (std::vector<std::string>::const_iterator i = temp->getDisabled().begin(); i != temp->getDisabled().end(); ++i)
			{
				if (*i == rule->getName())
				{
					disabledBy.push_back(*j);
				}
			}
			for (std::vector<std::string>::const_iterator i = temp->getGetOneFree().begin(); i != temp->getGetOneFree().end(); ++i)
			{
				if (*i == rule->getName())
				{
					getForFreeFrom.push_back(*j);
				}
			}
			for (std::map<std::string, std::vector<std::string> >::const_iterator itMap = temp->getGetOneFreeProtected().begin(); itMap != temp->getGetOneFreeProtected().end(); ++itMap)
			{
				for (std::vector<std::string>::const_iterator i = itMap->second.begin(); i != itMap->second.end(); ++i)
				{
					if (*i == rule->getName())
					{
						getForFreeFrom.push_back(*j);
					}
				}
			}
			for (std::vector<std::string>::const_iterator i = temp->getRequirements().begin(); i != temp->getRequirements().end(); ++i)
			{
				if (*i == rule->getName())
				{
					requiredByResearch.push_back(*j);
				}
			}
			for (std::vector<std::string>::const_iterator i = temp->getDependencies().begin(); i != temp->getDependencies().end(); ++i)
			{
				if (*i == rule->getName())
				{
					leadsTo.push_back(*j);
				}
			}
		}

		// 1. item required
		if (rule->needItem())
		{
			if (rule->destroyItem())
			{
				_lstLeft->addRow(1, tr("STR_ITEM_DESTROYED").c_str());
			}
			else
			{
				_lstLeft->addRow(1, tr("STR_ITEM_REQUIRED").c_str());
			}
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			std::wstring itemName = tr(_selectedTopic);
			itemName.insert(0, L"  ");
			_lstLeft->addRow(1, itemName.c_str());
			if (!isDiscoveredResearch(_selectedTopic))
			{
				_lstLeft->setRowColor(row, _pink);
			}
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
		}

		// 1b. requires buildings
		const std::vector<std::string> reqFacilities = rule->getRequireBaseFunc();
		if (reqFacilities.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_FACILITIES_REQUIRED").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqFacilities.begin(); i != reqFacilities.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				_lstLeft->setRowColor(row, _gold);
				_leftTopics.push_back("-");
				_leftFlags.push_back(TTV_NONE);
				++row;
			}
		}

		// 2. requires
		if (reqs.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_REQUIRES").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqs.begin(); i != reqs.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 3. depends on
		if (deps.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_DEPENDS_ON").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = deps.begin(); i != deps.end(); ++i)
			{
				if (std::find(unlockedBy.begin(), unlockedBy.end(), *i) != unlockedBy.end())
				{
					// if the same item is also in the "Unlocked by" section, skip it
					continue;
				}
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 4a. unlocked by
		if (unlockedBy.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_UNLOCKED_BY").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = unlockedBy.begin(); i != unlockedBy.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 4b. disabled by
		if (disabledBy.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_DISABLED_BY").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = disabledBy.begin(); i != disabledBy.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 5. get for free from
		if (getForFreeFrom.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_GET_FOR_FREE_FROM").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = getForFreeFrom.begin(); i != getForFreeFrom.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		row = 0;

		// 6. required by
		if (requiredByResearch.size() > 0 || requiredByManufacture.size() > 0 || requiredByFacilities.size() > 0 || requiredByItems.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_REQUIRED_BY").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
		}

		// 6a. required by research
		if (requiredByResearch.size() > 0)
		{
			for (std::vector<std::string>::const_iterator i = requiredByResearch.begin(); i != requiredByResearch.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 6b. required by manufacture
		if (requiredByManufacture.size() > 0)
		{
			for (std::vector<std::string>::const_iterator i = requiredByManufacture.begin(); i != requiredByManufacture.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				name.append(tr("STR_M_FLAG"));
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredManufacture((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_MANUFACTURING);
				++row;
			}
		}

		// 6c. required by facilities
		if (requiredByFacilities.size() > 0)
		{
			for (std::vector<std::string>::const_iterator i = requiredByFacilities.begin(); i != requiredByFacilities.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				name.append(tr("STR_F_FLAG"));
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredFacility((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_FACILITIES);
				++row;
			}
		}

		// 6d. required by items
		if (requiredByItems.size() > 0)
		{
			for (std::vector<std::string>::const_iterator i = requiredByItems.begin(); i != requiredByItems.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				name.append(tr("STR_I_FLAG"));
				_lstRight->addRow(1, name.c_str());
				if (!isProtectedAndDiscoveredItem((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_ITEMS);
				++row;
			}
		}

		// 7. leads to
		if (leadsTo.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_LEADS_TO").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = leadsTo.begin(); i != leadsTo.end(); ++i)
			{
				if (std::find(unlocks.begin(), unlocks.end(), *i) != unlocks.end())
				{
					// if the same topic is also in the "Unlocks" section, skip it
					continue;
				}
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 8a. unlocks
		if (unlocks.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_UNLOCKS").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = unlocks.begin(); i != unlocks.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 8b. disables
		if (disables.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_DISABLES").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = disables.begin(); i != disables.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 9. gives one for free
		if (free.size() > 0)
		{
			if (rule->sequentialGetOneFree())
			{
				_lstRight->addRow(1, tr("STR_GIVES_ONE_FOR_FREE_SEQ").c_str());
			}
			else
			{
				_lstRight->addRow(1, tr("STR_GIVES_ONE_FOR_FREE").c_str());
			}
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = free.begin(); i != free.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstRight->setRowColor(row, _pink);
				}
				_rightTopics.push_back((*i));
				_rightFlags.push_back(TTV_RESEARCH);
				++row;
			}
			for (std::map<std::string, std::vector<std::string> >::const_iterator itMap = freeProtected.begin(); itMap != freeProtected.end(); ++itMap)
			{
				std::wstring name2 = tr(itMap->first);
				name2.insert(0, L" ");
				name2.append(L":");
				_lstRight->addRow(1, name2.c_str());
				if (isDiscoveredResearch(itMap->first))
				{
					_lstRight->setRowColor(row, _white);
				}
				else
				{
					_lstRight->setRowColor(row, _gold);
				}
				_rightTopics.push_back(itMap->first);
				_rightFlags.push_back(TTV_RESEARCH);
				++row;
				for (std::vector<std::string>::const_iterator i = itMap->second.begin(); i != itMap->second.end(); ++i)
				{
					std::wstring name = tr((*i));
					name.insert(0, L"  ");
					_lstRight->addRow(1, name.c_str());
					if (!isDiscoveredResearch((*i)))
					{
						_lstRight->setRowColor(row, _pink);
					}
					_rightTopics.push_back((*i));
					_rightFlags.push_back(TTV_RESEARCH);
					++row;
				}
			}
		}
	}
	else if (_selectedFlag == TTV_MANUFACTURING)
	{
		int row = 0;
		RuleManufacture *rule = _game->getMod()->getManufacture(_selectedTopic);
		if (rule == 0)
			return;

		// 1. requires
		const std::vector<std::string> reqs = rule->getRequirements();
		if (reqs.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_RESEARCH_REQUIRED").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqs.begin(); i != reqs.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 2. requires buildings
		const std::vector<std::string> reqFacilities = rule->getRequireBaseFunc();
		if (reqFacilities.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_FACILITIES_REQUIRED").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqFacilities.begin(); i != reqFacilities.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				_lstLeft->setRowColor(row, _gold);
				_leftTopics.push_back("-");
				_leftFlags.push_back(TTV_NONE);
				++row;
			}
		}

		// 3. inputs
		const std::map<std::string, int> inputs = rule->getRequiredItems();
		if (inputs.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_MATERIALS_REQUIRED").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::map<std::string, int>::const_iterator i = inputs.begin(); i != inputs.end(); ++i)
			{
				std::wostringstream name;
				name << L"  ";
				name << tr((*i).first);
				name << L": ";
				name << (*i).second;
				_lstLeft->addRow(1, name.str().c_str());
				_lstLeft->setRowColor(row, _white);
				_leftTopics.push_back("-");
				_leftFlags.push_back(TTV_NONE);
				++row;
			}
		}

		row = 0;

		// 4. outputs
		const std::map<std::string, int> outputs = rule->getProducedItems();
		if (outputs.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_ITEMS_PRODUCED").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::map<std::string, int>::const_iterator i = outputs.begin(); i != outputs.end(); ++i)
			{
				std::wostringstream name;
				name << L"  ";
				name << tr((*i).first);
				name << L": ";
				name << (*i).second;
				_lstRight->addRow(1, name.str().c_str());
				_lstRight->setRowColor(row, _white);
				_rightTopics.push_back("-");
				_rightFlags.push_back(TTV_NONE);
				++row;
			}
		}

		// 5. person joining
		if (rule->getSpawnedPersonType() != "")
		{
			_lstRight->addRow(1, tr("STR_PERSON_RECRUITED").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;

			// person joining
			std::wostringstream name;
			name << L"  ";
			name << tr(rule->getSpawnedPersonName() != "" ? rule->getSpawnedPersonName() : rule->getSpawnedPersonType());
			_lstRight->addRow(1, name.str().c_str());
			_lstRight->setRowColor(row, _white);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
		}
	}
	else if (_selectedFlag == TTV_FACILITIES)
	{
		int row = 0;
		RuleBaseFacility *rule = _game->getMod()->getBaseFacility(_selectedTopic);
		if (rule == 0)
			return;

		// 1. requires
		const std::vector<std::string> reqs = rule->getRequirements();
		if (reqs.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_RESEARCH_REQUIRED").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqs.begin(); i != reqs.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 2. requires facilities
		const std::vector<std::string> reqFacilities = rule->getRequireBaseFunc();
		if (reqFacilities.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_FACILITIES_REQUIRED").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqFacilities.begin(); i != reqFacilities.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				_lstLeft->setRowColor(row, _gold);
				_leftTopics.push_back("-");
				_leftFlags.push_back(TTV_NONE);
				++row;
			}
		}

		row = 0;

		// 3. provides facilities
		const std::vector<std::string> providedFacilities = rule->getProvidedBaseFunc();
		if (providedFacilities.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_FACILITIES_PROVIDED").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = providedFacilities.begin(); i != providedFacilities.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				_lstRight->setRowColor(row, _gold);
				_rightTopics.push_back("-");
				_rightFlags.push_back(TTV_NONE);
				++row;
			}
		}

		// 4. forbids facilities
		const std::vector<std::string> forbFacilities = rule->getForbiddenBaseFunc();
		if (forbFacilities.size() > 0)
		{
			_lstRight->addRow(1, tr("STR_FACILITIES_FORBIDDEN").c_str());
			_lstRight->setRowColor(row, _blue);
			_rightTopics.push_back("-");
			_rightFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = forbFacilities.begin(); i != forbFacilities.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstRight->addRow(1, name.c_str());
				_lstRight->setRowColor(row, _white);
				_rightTopics.push_back("-");
				_rightFlags.push_back(TTV_NONE);
				++row;
			}
		}
	}
	else if (_selectedFlag == TTV_ITEMS)
	{
		int row = 0;
		RuleItem *rule = _game->getMod()->getItem(_selectedTopic);
		if (rule == 0)
			return;

		// 1. requires to use/equip
		const std::vector<std::string> reqs = rule->getRequirements();
		if (reqs.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_RESEARCH_REQUIRED_USE").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqs.begin(); i != reqs.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}

		// 2. requires to buy
		const std::vector<std::string> reqsBuy = rule->getBuyRequirements();
		if (reqsBuy.size() > 0)
		{
			_lstLeft->addRow(1, tr("STR_RESEARCH_REQUIRED_BUY").c_str());
			_lstLeft->setRowColor(row, _blue);
			_leftTopics.push_back("-");
			_leftFlags.push_back(TTV_NONE);
			++row;
			for (std::vector<std::string>::const_iterator i = reqsBuy.begin(); i != reqsBuy.end(); ++i)
			{
				std::wstring name = tr((*i));
				name.insert(0, L"  ");
				_lstLeft->addRow(1, name.c_str());
				if (!isDiscoveredResearch((*i)))
				{
					_lstLeft->setRowColor(row, _pink);
				}
				_leftTopics.push_back((*i));
				_leftFlags.push_back(TTV_RESEARCH);
				++row;
			}
		}
	}
}

/**
* Selects the topic.
* @param action Pointer to an action.
*/
void TechTreeViewerState::onSelectLeftTopic(Action *)
{
	int index = _lstLeft->getSelectedRow();
	if (_leftFlags[index] > TTV_NONE)
	{
		if (Options::techTreeViewerSpoilerProtection)
		{
			if (_leftFlags[index] == TTV_RESEARCH && !isDiscoveredResearch(_leftTopics[index]))
			{
				return;
			}
			else if (_leftFlags[index] == TTV_MANUFACTURING && !isDiscoveredManufacture(_leftTopics[index]))
			{
				return;
			}
			else if (_leftFlags[index] == TTV_FACILITIES && !isDiscoveredFacility(_leftTopics[index]))
			{
				return;
			}
			else if (_leftFlags[index] == TTV_ITEMS && !isProtectedAndDiscoveredItem(_leftTopics[index]))
			{
				return;
			}
		}
		_selectedFlag = _leftFlags[index];
		_selectedTopic = _leftTopics[index];
		initLists();
	}
}

/**
* Selects the topic.
* @param action Pointer to an action.
*/
void TechTreeViewerState::onSelectRightTopic(Action *)
{
	int index = _lstRight->getSelectedRow();
	if (_rightFlags[index] > TTV_NONE)
	{
		if (Options::techTreeViewerSpoilerProtection)
		{
			if (_rightFlags[index] == TTV_RESEARCH && !isDiscoveredResearch(_rightTopics[index]))
			{
				return;
			}
			else if (_rightFlags[index] == TTV_MANUFACTURING && !isDiscoveredManufacture(_rightTopics[index]))
			{
				return;
			}
			else if (_rightFlags[index] == TTV_FACILITIES && !isDiscoveredFacility(_rightTopics[index]))
			{
				return;
			}
			else if (_rightFlags[index] == TTV_ITEMS && !isProtectedAndDiscoveredItem(_rightTopics[index]))
			{
				return;
			}
		}
		_selectedFlag = _rightFlags[index];
		_selectedTopic = _rightTopics[index];
		initLists();
	}
}

/**
* Changes the selected topic.
*/
void TechTreeViewerState::setSelectedTopic(const std::string &selectedTopic, TTVMode topicType)
{
	_selectedTopic = selectedTopic;
	_selectedFlag = topicType;
}

/**
* Is given research topic discovered/available?
*/
bool TechTreeViewerState::isDiscoveredResearch(const std::string &topic) const
{
	if (_alreadyAvailableResearch.find(topic) == _alreadyAvailableResearch.end())
	{
		return false;
	}
	return true;
}

/**
* Is given manufacture topic discovered/available?
*/
bool TechTreeViewerState::isDiscoveredManufacture(const std::string &topic) const
{
	if (_alreadyAvailableManufacture.find(topic) == _alreadyAvailableManufacture.end())
	{
		return false;
	}
	return true;
}

/**
* Is given base facility discovered/available?
*/
bool TechTreeViewerState::isDiscoveredFacility(const std::string &topic) const
{
	if (_alreadyAvailableFacilities.find(topic) == _alreadyAvailableFacilities.end())
	{
		return false;
	}
	return true;
}

/**
* Is given item protected by any research?
*/
bool TechTreeViewerState::isProtectedItem(const std::string &topic) const
{
	if (_protectedItems.find(topic) == _protectedItems.end())
	{
		return false;
	}
	return true;
}

/**
* Is given protected item discovered/available for both purchase and usage/equipment?
*/
bool TechTreeViewerState::isProtectedAndDiscoveredItem(const std::string &topic) const
{
	if (_alreadyAvailableItems.find(topic) == _alreadyAvailableItems.end())
	{
		return false;
	}
	return true;
}

}
