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
#include "AlienRace.h"
#include "../Engine/RNG.h"

namespace OpenXcom
{

/**
 * Creates a blank alien race.
 * @param id String defining the id.
 */
AlienRace::AlienRace(const std::string &id) : _id(id), _retaliation(true), _retaliationAggression(0)
{
}

AlienRace::~AlienRace()
{
}

/**
 * Loads the alien race from a YAML file.
 * @param node YAML node.
 */
void AlienRace::load(const YAML::Node &node)
{
	if (const YAML::Node &parent = node["refNode"])
	{
		load(parent);
	}
	_id = node["id"].as<std::string>(_id);
	_baseCustomDeploy = node["baseCustomDeploy"].as<std::string>(_baseCustomDeploy);
	_baseCustomMission = node["baseCustomMission"].as<std::string>(_baseCustomMission);
	_retaliationMission = node["retaliationMission"].as<std::string>(_retaliationMission);
	_members = node["members"].as< std::vector<std::string> >(_members);
	_membersRandom = node["membersRandom"].as< std::vector <std::vector<std::string> > >(_membersRandom);
	_retaliation = node["retaliation"].as<bool>(_retaliation);
	_retaliationAggression = node["retaliationAggression"].as<int>(_retaliationAggression);
}

/**
 * Returns the language string that names
 * this alien race. Each race has a unique name.
 * @return Race name.
 */
const std::string &AlienRace::getId() const
{
	return _id;
}

/**
 * Returns optional weapon deploy for aliens in they base.
 * @return Alien deployment id.
 */
const std::string &AlienRace::getBaseCustomDeploy() const
{
	return _baseCustomDeploy;
}

/**
 * Returns custom alien base deploy.
 * @return Alien deployment id.
 */
const std::string &AlienRace::getBaseCustomMission() const
{
	return _baseCustomMission;
}
/**
 * Gets a certain member of this alien race family.
 * @param id The member's id.
 * @return The member's name.
 */
const std::string &AlienRace::getMember(int id) const
{
	if (!_membersRandom.empty())
	{
		int rng = RNG::generate(0, _membersRandom[id].size() - 1);
		return _membersRandom[id][rng];
	}
	return _members[id];
}

/**
 * Gets mission used for retaliation, can be empty. This is different than canRetaliate.
 * @return Mission ID or empty string.
 */
const std::string &AlienRace::getRetaliationMission() const
{
	return _retaliationMission;
}

/**
 * Gets how aggressive alien are
 * @return Mission ID or empty string.
 */
int AlienRace::getRetaliationAggression() const
{
	return _retaliationAggression;
}
/**
 * Returns if the race can participate in retaliation missions.
 * @return True if it can retaliate.
 */
bool AlienRace::canRetaliate() const
{
	return _retaliation;
}

}
