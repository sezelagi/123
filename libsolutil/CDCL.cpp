/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <libsolutil/CDCL.h>

#include <liblangutil/Exceptions.h>

// TODO remove before merge
#include <iostream>
#include <libsolutil/StringUtils.h>

using namespace std;
using namespace solidity;
using namespace solidity::util;


CDCL::CDCL(
	vector<string> _variables,
	vector<Clause> const& _clauses,
	std::function<std::optional<Clause>(size_t, std::map<size_t, bool> const&)> _theorySolver,
	std::function<void(size_t)> _backtrackNotify,
	std::function<std::optional<bool>(size_t)> _constraintIndication
):
	m_theorySolver(_theorySolver),
	m_backtrackNotify(_backtrackNotify),
	m_constraintIndication(_constraintIndication),
	m_variables(move(_variables)),
	m_order(VarOrderLt(m_activity))
{
	for (Clause const& clause: _clauses)
		addClause(clause);

	// TODO some sanity checks like no empty clauses, no duplicate literals?
}

optional<CDCL::Model> CDCL::solve()
{
	CDCL::Model model;
	int solution;
	uint32_t max_conflicts = 100;
	bool solved = false;
	while(!solved) {
		solution = 3;
		solved = solve_loop(max_conflicts, model, solution);
		max_conflicts = uint32_t((double)max_conflicts * 1.2);
	}
	assert(solution != 3);
	if (solution) return model;
	else return nullopt;
}

bool CDCL::solve_loop(const uint32_t max_conflicts, CDCL::Model& model, int& solution)
{
	assert (max_conflicts > 0);
	uint32_t conflicts = 0;
	while (conflicts < max_conflicts)
	{
		optional<Clause> conflictClause = propagate();
		if (!conflictClause && m_theorySolver)
		{
			size_t lastTrailSizeCall = m_assignemntTrailSizesWeCalledSolverFor.empty() ? 0 : m_assignemntTrailSizesWeCalledSolverFor.back();

			std::map<size_t, bool> newAssignments;
			for (size_t i = lastTrailSizeCall; i < m_assignmentTrail.size(); ++i)
				newAssignments[m_assignmentTrail[i].variable] = m_assignmentTrail[i].positive;
			conflictClause = m_theorySolver(m_assignmentTrail.size(), newAssignments);
			m_assignemntTrailSizesWeCalledSolverFor.emplace_back(m_assignmentTrail.size());
//			if (conflictClause)
//				cout << "Theory gave us conflict: " << toString(*conflictClause) << endl;
		}
		if (conflictClause)
		{
			conflicts++;
#ifdef DEBUG
			m_sumConflicts++;
			if (m_sumConflicts % 1000 == 999) {
				cerr << "c confl: " << m_sumConflicts << std::endl;
			}
#endif
			if (currentDecisionLevel() == 0)
			{
//				cout << "Unsatisfiable" << endl;
				solution = 0;
				return true;
			}
			auto&& [learntClause, backtrackLevel] = analyze(move(*conflictClause));
			cancelUntil(backtrackLevel);
			while (!m_assignemntTrailSizesWeCalledSolverFor.empty() && m_assignemntTrailSizesWeCalledSolverFor.back() > m_assignmentTrail.size())
				m_assignemntTrailSizesWeCalledSolverFor.pop_back();
			if (m_backtrackNotify)
				m_backtrackNotify(m_assignemntTrailSizesWeCalledSolverFor.empty() ? 0 : m_assignemntTrailSizesWeCalledSolverFor.back());
			solAssert(!learntClause.empty());
			solAssert(!isAssigned(learntClause.front()));
//			for (size_t i = 1; i < learntClause.size(); i++)
//				solAssert(value(learntClause[i]) == TriState{false});

			addClause(move(learntClause));
			enqueue(m_clauses.back()->front(), &(*m_clauses.back()));
		}
		else
		{
			if (auto variable = nextDecisionVariable())
			{
				cerr << "c Level " << currentDecisionLevel() << " - ";
				cerr << ((m_assignments.size() * 100) / m_variables.size()) << "% of variables assigned." << endl;
				m_decisionPoints.emplace_back(m_assignmentTrail.size());
				cerr << "Deciding on " << m_variables.at(*variable) << " @" << currentDecisionLevel() << endl;

				optional<bool> guess;
				if (m_constraintIndication)
					guess = m_constraintIndication(*variable);

				if (!guess)
				{
					// Polarity caching below
					bool positive = false;
					auto const& found = m_assignments_cache.find(*variable);
					if (found != m_assignments_cache.end()) positive = found->second;
					guess = positive;
				}
				enqueue(Literal{*guess, *variable}, nullptr);
			}
			else
			{
				//cout << "satisfiable." << endl;
				//for (auto&& [i, value]: m_assignments | ranges::view::enumerate())
				//	cout << " " << m_variables.at(i) << ": " << value.toString() << endl;
				solution = 1;
				model = m_assignments;
				return true;
			}
		}
	}
	return false;
}

void CDCL::setupWatches(Clause& _clause)
{
	for (size_t i = 0; i < min<size_t>(2, _clause.size()); i++)
		m_watches[_clause.at(i)].push_back(&_clause);
}

optional<Clause> CDCL::propagate()
{
	//cout << "Propagating." << endl;
	for (; m_assignmentQueuePointer < m_assignmentTrail.size(); m_assignmentQueuePointer++)
	{
		Literal toPropagate = m_assignmentTrail.at(m_assignmentQueuePointer);
		Literal falseLiteral = ~toPropagate;
		//cout << "Propagating " << toString(toPropagate) << endl;
		// Go through all watched clauses where this assignment makes the literal false.
		vector<Clause*> watchReplacement;
		auto it = m_watches[falseLiteral].begin();
		auto end = m_watches[falseLiteral].end();
		for (; it != end; ++it)
		{
			Clause& clause = **it;
			//cout << " watch clause: " << toString(clause) << endl;

			solAssert(!clause.empty());
			if (clause.front() != falseLiteral)
				swap(clause[0], clause[1]);
			solAssert(clause.front() == falseLiteral);
			if (clause.size() >= 2 && isAssignedTrue(clause[1]))
			{
				// Clause is already satisfied, keezp the watch.
				//cout << " -> already satisfied by " << toString(clause[1]) << endl;
				watchReplacement.emplace_back(&clause);
				continue;
			}

			// find a new watch to swap
			for (size_t i = 2; i < clause.size(); i++)
				if (isUnknownOrAssignedTrue(clause[i]))
				{
					//cout << " -> swapping " << toString(clause.front()) << " with " << toString(clause[i]) << endl;
					swap(clause.front(), clause[i]);
					m_watches[clause.front()].emplace_back(&clause);
					break;
				}
			if (clause.front() != falseLiteral)
				continue; // we found a new watch

			// We did not find a new watch, i.e. all literals starting from index 2
			// are false, thus clause[1] has to be true (if it exists)
			if (clause.size() == 1 || isAssignedFalse(clause[1]))
			{
//				if (clause.size() >= 2)
//					cout << " - Propagate resulted in conflict because " << toString(clause[1]) << " is also false." << endl;
//				else
//					cout << " - Propagate resulted in conflict since clause is single-literal." << endl;
				// Copy over the remaining watches and replace.
				while (it != end) watchReplacement.emplace_back(move(*it++));
				m_watches[falseLiteral] = move(watchReplacement);
				// Mark the queue as finished.
				m_assignmentQueuePointer = m_assignmentTrail.size();
				return clause;
			}
			else
			{
//				cout << " - resulted in new assignment: " << toString(clause[1]) << endl;
				watchReplacement.emplace_back(&clause);
				enqueue(clause[1], &clause);
			}
		}
		m_watches[falseLiteral] = move(watchReplacement);
	}
	return nullopt;
}


std::pair<Clause, size_t> CDCL::analyze(Clause _conflictClause)
{
	solAssert(!_conflictClause.empty());
	//cout << "Analyzing conflict." << endl;
	Clause learntClause;
	size_t backtrackLevel = 0;

	set<size_t> seenVariables;

	int pathCount = 0;
	size_t trailIndex = m_assignmentTrail.size() - 1;
	optional<Literal> resolvingLiteral;
	do
	{
		//cout << " conflict clause: " << toString(_conflictClause) << endl;
		for (Literal literal: _conflictClause)
			if ((!resolvingLiteral || literal != *resolvingLiteral) && !seenVariables.count(literal.variable))
			{
				seenVariables.insert(literal.variable);
				size_t variableLevel = m_levelForVariable.at(literal.variable);
				if (variableLevel == currentDecisionLevel())
				{
					//cout << "    ignoring " << toString(literal) << " at current decision level." << endl;
					// ignore variable, we will apply resolution with its reason.
					pathCount++;
				}
				else
				{
					//cout << "    adding " << toString(literal) << " @" << variableLevel << " to learnt clause." << endl;
					vsidsBumpVarAct((uint32_t)literal.variable);
					learntClause.push_back(literal);
					backtrackLevel = max(backtrackLevel, variableLevel);
				}
			}/*
			else
				cout << "    already seen " << toString(literal) << endl;*/

		solAssert(pathCount > 0);
		pathCount--;
		while (!seenVariables.count(m_assignmentTrail[trailIndex--].variable));
		resolvingLiteral = m_assignmentTrail[trailIndex + 1];
		//cout << "  resolving literal: " << toString(*resolvingLiteral) << endl;
		seenVariables.erase(resolvingLiteral->variable);
		// TODO Is there always a reason? Not if it's a decision variable.
		if (pathCount > 0)
		{
			_conflictClause = *m_reason.at(*resolvingLiteral);
			//cout << "  reason: " << toString(_conflictClause) << endl;
		}
	}
	while (pathCount > 0);
	solAssert(resolvingLiteral);
	learntClause.push_back(~(*resolvingLiteral));
	// Move to front so we can directly propagate.
	swap(learntClause.front(), learntClause.back());

	//cout << "-> learnt clause: " << toString(learntClause) << " backtrack to " << backtrackLevel << endl;


	return {move(learntClause), backtrackLevel};
}

void CDCL::addClause(Clause _clause)
{
	uint64_t max_var = (uint32_t)m_activity.size();
	uint64_t new_max_var = 0;
	for(auto const& l: _clause) {
		new_max_var = std::max<uint64_t>(l.variable+1, max_var);
	}
	int64_t to_add = (int64_t)new_max_var - (int64_t)max_var;
	if (to_add > 0) {
		m_activity.insert(m_activity.end(), (uint64_t)to_add, 0.0);
	}
	for(auto const& l: _clause) {
		if (!m_order.inHeap((int)l.variable)) m_order.insert((int)l.variable);
	}

	m_clauses.push_back(make_unique<Clause>(move(_clause)));
	setupWatches(*m_clauses.back());
}

void CDCL::enqueue(Literal const& _literal, Clause const* _reason)
{
	/*
	cout << "Enqueueing " << toString(_literal) << " @" << currentDecisionLevel() << endl;
	if (_reason)
		cout << "  because of " << toString(*_reason) << endl;
*/
	// TODO assert that assignmnets was unknown
	m_assignments[_literal.variable] = _literal.positive;
	m_levelForVariable[_literal.variable] = currentDecisionLevel();
	if (_reason) {
		m_reason[_literal] = _reason;
	}
	m_assignmentTrail.push_back(_literal);
}

void CDCL::cancelUntil(size_t _backtrackLevel)
{
	// TODO what if we backtrack to zero?
	//cout << "Canceling until " << _backtrackLevel << endl;
	solAssert(m_assignmentQueuePointer == m_assignmentTrail.size());
	size_t assignmentsToUndo = m_assignmentTrail.size() - m_decisionPoints.at(_backtrackLevel);
	if (m_assignmentTrail.size() > m_longest_trail) {
		m_assignments_cache= m_assignments;
		m_longest_trail = m_assignmentTrail.size();
	}

	for (size_t i = 0; i < assignmentsToUndo; i++)
	{
		Literal l = m_assignmentTrail.back();
		//cout << "  undoing " << toString(l) << endl;
		m_assignmentTrail.pop_back();
		m_assignments.erase(l.variable);
		m_reason.erase(l);
		// TODO maybe could do without.
		m_levelForVariable.erase(l.variable);
		if (!m_order.inHeap((int)l.variable)) {
			m_order.insert((int)l.variable);
		}
	}
	m_decisionPoints.resize(_backtrackLevel);
	m_assignmentQueuePointer = m_assignmentTrail.size();

	solAssert(currentDecisionLevel() == _backtrackLevel);
}

optional<size_t> CDCL::nextDecisionVariable()
{
	while(true) {
		if (m_order.empty()) return nullopt;
		size_t i = (size_t)m_order.removeMin();
		if (!m_assignments.count(i)) return i;
	}
	return nullopt;
}

bool CDCL::isAssigned(Literal const& _literal) const
{
	return m_assignments.count(_literal.variable);
}

bool CDCL::isAssignedTrue(Literal const& _literal) const
{
	return (
		m_assignments.count(_literal.variable) &&
		m_assignments.at(_literal.variable) == _literal.positive
	);
}

bool CDCL::isAssignedFalse(Literal const& _literal) const
{
	return (
		m_assignments.count(_literal.variable) &&
		!m_assignments.at(_literal.variable) == _literal.positive
	);
}

bool CDCL::isUnknownOrAssignedTrue(Literal const& _literal) const
{
	return (
		!m_assignments.count(_literal.variable) ||
		m_assignments.at(_literal.variable) == _literal.positive
	);
}

string CDCL::toString(Literal const& _literal) const
{
	return (_literal.positive ? "" : "~") + m_variables.at(_literal.variable);
}

string CDCL::toString(Clause const& _clause) const
{
	vector<string> literals;
	for (Literal const& l: _clause)
		literals.emplace_back(toString(l));
	return "(" + joinHumanReadable(literals) + ")";
}
