#include "optseqimpl.h"
#define DEBUG 0
#define DEPTH 3

const Int OptSeqImpl::bigM = 1000000000;
const double OptSeqImpl::maxWeightMultiplier = 1000;

void OptSeqImpl::updateCurrentSolution() {
	if (incumbent != current) {
		swap(current, bestNeighbor);
	}
	else {
		UInt kk;
		for (kk = 0; kk < 4; kk++) {
			if (current != kk && neighbor != kk && bestNeighbor != kk) { break; }
		}
		current = bestNeighbor; bestNeighbor = kk;
	}
}

bool
OptSeqImpl::search() {
	ltm.resize(getNumActivities());

	changeModeActivityFlag.resize(getNumActivities(), 0);
	changeModeFlag.resize(getNumActivities());
	shiftForwardFlag.resize(getNumActivities());
	for (Id i = 0; i < getNumActivities(); ++i) {
		changeModeFlag[i].resize(getActivityNumModes(i), 0);
		shiftForwardFlag[i].resize(getNumActivities(), 0);
	}
	shiftForwardSccFlag.resize(numSccs);
	for (UInt scc = 0; scc < numSccs; ++scc) {
		shiftForwardSccFlag[scc].resize(numSccs, 0);
	}
	while (1) {
		//			ratio = 1;
		if (solutions[current].getObjective() < getSolutionObjective()) {
			incumbent = current;
			cout << "objective value = " << getSolutionObjective();
			cout << " (cpu time = " << getSolutionCpuTime() << "(s), ";
			cout << "iteration = " << getSolutionIteration() << ")";
			cout << endl;
			if (getSolutionObjective() == 0) { return true; }
		}
		if (reportInterval == 0 || !(iteration % reportInterval)) {
			cout << iteration << ": ";
			cout << cputime() << "(s): ";
			cout << solutions[current].getObjective() << "/";
			cout << getSolutionObjective();
			cout << flush;
		}
		if (cputime() >= timeLimit || iteration >= iterationLimit) { return true; }

		++iteration;

		// adjust penalty weights
		if (weightControl && (iteration - solutions[incumbent].getIteration()) % weightControl == 0) {
			updatePenaltyWeights(solutions[incumbent]);
			updatePenaltyWeights(solutions[current]);
			calculatePenalty(solutions[current]);
		}

		auto printMove = [this](Move* move) {
			switch (move->type) {
			case Move::Type::ChangeMode:
				cout << getActivityName(move->id1) << ": -> " << getModeName(getActivityModeId(move->id1, move->id2));
				break;
			case Move::Type::ShiftForward:
				cout << getActivityName(move->id2) << "<-" << getActivityName(move->id1);
				break;
			}
		};

		// neighborhood search
		UInt numNeighbors = 0;
		UInt bestMoveIndex = 0;

		for (UInt repeat = 0; ; ++repeat) {
			if (reportInterval == 0) {
				cout << ": " << solutions[current].getEvaluation()
					<< "(" << iteration - solutions[incumbent].getIteration() << ")" << endl;
			}

			solutions[current].setMakeCriticalGraph(true);
			schedule(solutions[current]);
			solutions[current].setMakeCriticalGraph(false);

			solutions[current].sortViolations();
			auto& violations = solutions[current].getViolations();
			if (reportInterval == 0) {
				for (auto violation = violations.rbegin(); violation != violations.rend(); ++violation) {
					TempConstraint* temp;
					Activity* activity;
					NrrConstraint* nrr;
					switch (violation->type) {
					case Violation::Type::TMP:
						temp = tempConstraints[violation->id];
						cout << "!" << temp->getId() << ": " //<< temp->getName() << ": "
							<< violation->penalty
							<< endl;
						break;
					case Violation::Type::DUE:
						activity = activities[violation->id];
						cout << "!" << activity->getName() << " ";
						if (!solutions[current].getExecutions(violation->id).empty()) {
							cout << "\tstart: "
								<< max<Int>(0, solutions[current].getExecutions(violation->id).begin()->from - activity->getDueDate(Activity::DueDateType::START))
								<< " * " << activity->getTardinessPenalty(Activity::DueDateType::START).first
								<< "\tfinish: "
								<< max<Int>(0, solutions[current].getExecutions(violation->id).rbegin()->to - activity->getDueDate(Activity::DueDateType::COMPLETION))
								<< " * " << activity->getTardinessPenalty(Activity::DueDateType::COMPLETION).first;
						}
						cout << endl;
						break;
					case Violation::Type::NRR:
						nrr = nrrConstraints[softNrrConstraintIds[violation->id]];
						cout << "!" << nrr->getId() << " \t" << violation->penalty << ": "
							<< nrr->calculatePenalty(solutions[current]) << " * " << nrr->getWeight() << ":" << penaltyWeightSoftNrr[violation->id] << endl;
					}
				}
			}

			// evaluate neighbors, collecting moves
			moves.clear();
			solutions[bestNeighbor].setEvaluation(DInf);
			numNeighbors = 0;
			auto violation = violations.rbegin();

			//UInt nS = neighborhoodSize * static_cast<int>(pow(rand(1, 10), 3) / 100 + 1);
			//for (UInt k = 0; numNeighbors < neighborhoodSize; ++k) {
			UInt nS = neighborhoodSize;
			for (UInt k = 0; numNeighbors < nS; ++k) {
				while (k == moves.size()) {
					if (violation == violations.rend()) { break; }
					// collect moves
					switch (violation->type) {
					case Violation::Type::TMP:
						if (violation->penalty > 0) {
							collectMovesTemporal(violation->id);
						}
						break;
					case Violation::Type::DUE:
						collectMovesDueDate(violation->id, 0);
						break;
					case Violation::Type::NRR:
						collectMovesSoftNrr(softNrrConstraintIds[violation->id]);
						break;
					}
					++violation;
				}
				if (k == moves.size()) { break; }

				swap(moves[k], moves[rand(k, static_cast<Int>(moves.size()) - 1)]);
				Move* move = &moves[k];
				if (tabu(move->reverse_attribute, 0, -Inf)) { continue; }
				solutions[neighbor].initialize(solutions[current]);

				switch (move->type) {
					// change mode
				case Move::Type::ChangeMode:
					solutions[neighbor].setModeLocalId(move->id1, move->id2);
#if DEBUG
					cout << "ChangeMode: " << move->id1 << " " << getActivityName(move->id1)
						<< " " << move->id2 << " " << getModeName(getActivityModeId(move->id1, move->id2)) << endl;
#endif

					if (!makeModesFeasible(solutions[neighbor], move->id1)) {
						if (reportInterval == 0) { cout << "x" << flush; }
						continue;
					}
					break;
					// shift forward
				case Move::Type::ShiftForward:
					if (move->id1 == move->id2) { continue; }
					shiftForward(solutions[neighbor], move->id1, move->id2);
					break;
				}
				schedule(solutions[neighbor]);

				if (tabu(move->reverse_attribute, solutions[neighbor].getObjective(),
					solutions[neighbor].getObjective() - solutions[current].getObjective())
					&& solutions[neighbor].getObjective() >= solutions[incumbent].getObjective()) {
					continue;
				}
				if (reportInterval == 0) {
					cout << "\t";
					printMove(move);
					if (!solutions[neighbor].isFeasible()) {
						cout << "\tinfeasible";
						//cout << "+" << flush; 
					}
					else {
						cout << "\t" << solutions[neighbor].getObjective();
						//cout << "." << flush; 
						//if (move->type == Move::Type::ChangeMode) { cout << "m" << flush; }
						//else { cout << "s" << flush; }
					}
					if (activities[move->id1]->getAutoSelect()) {
						cout << "!";
					}
					cout << endl;
				}
				++numNeighbors;
				bool update = false;
				if (solutions[neighbor].getEvaluation() < solutions[bestNeighbor].getEvaluation()) {
					update = true;
				}
				else if (solutions[neighbor].getEvaluation() == solutions[bestNeighbor].getEvaluation()) {
					update = tabu.get_ltm(move->attribute) < tabu.get_ltm(moves[bestMoveIndex].attribute);
				}

				if (update) {
					bestMoveIndex = k;
					swap(neighbor, bestNeighbor);
					//if (solutions[bestNeighbor].getObjective() < solutions[incumbent].getObjective()) break;
					//if (solutions[bestNeighbor].getObjective() < solutions[current].getObjective()) break;
				}
			} // end: neighborhood search
			if (solutions[bestNeighbor].getEvaluation() == DInf) {
				if (reportInterval <= 1) { cout << ": no neighbor" << endl; }
				if (repeat >= 1) {
					if (reportInterval <= 1) { cout << "Restart " << cputime() << "(s)." << endl; }
					else if (!((iteration - 1) % reportInterval)) { cout << endl; }
					return false;
				}
				schedule(solutions[current]);
				tabu.clear(1);
				continue;
			}
			break;
		}
		Move* bestMove = &moves[bestMoveIndex];
		if (reportInterval <= 1) {
			cout.width(4);
			cout << ": # = " << numNeighbors << ": ";
			printMove(bestMove);
			cout << " (" << tabu.get_ltm(bestMove->attribute) << ") ";
		}

		tabu.update(bestMove->attribute, bestMove->reverse_attribute, solutions[bestNeighbor].getObjective(),
			solutions[bestNeighbor].getObjective() - solutions[current].getObjective(), rand(0, 3), reportInterval <= 1);
		if (reportInterval == 0 || !((iteration - 1) % reportInterval)) { cout << endl; }

		updateCurrentSolution();
	}
	return false;
}

void
OptSeqImpl::updatePenaltyWeights(OptSeqImpl::Solution& solution) {
	if (solution.getExecutions(SinkId).empty()) return;
	for (auto ite = solution.getViolations().begin(); ite != solution.getViolations().end(); ++ite) {
		Id id = ite->id;
		switch (ite->type) {
		case Violation::Type::DUE:
			if (solution.getExecutions(id).begin()->from > getActivityDueDate(id, 0)) {
				penaltyWeightStart[id] = min(penaltyWeightStart[id] * 1.2, maxWeightMultiplier * getActivityDueDateWeight(id, 0));
			}
			if (solution.getExecutions(id).begin()->from > getActivityDueDate(id, 1)) {
				penaltyWeightFinish[id] = min(penaltyWeightFinish[id] * 1.2, maxWeightMultiplier * getActivityDueDateWeight(id, 1));
			}
			break;
		case Violation::Type::NRR:
			penaltyWeightSoftNrr[id] = min(penaltyWeightSoftNrr[id] * 1.2,
				maxWeightMultiplier * getNrrConstraintWeight(softNrrConstraintIds[id]));
		}
	}
}


void
OptSeqImpl::shiftForward(Solution &solution, Id id1, Id id2){
	// move id1 to the position just before id2, under the priority condition
	Id scc1 = getActivityScc(id1);
	Id scc2 = getActivityScc(id2);
	
	Id target = id1, sentinel = solution.getPrevActivityId(id2), ite = solution.getPrevActivityId(id1);
	if (scc1 != scc2){
		for (; getActivityScc(solution.getNextActivityId(target)) == scc1; target = solution.getNextActivityId(target));
		for (; getActivityScc(sentinel) == scc2; sentinel = solution.getPrevActivityId(sentinel));
		for (; getActivityScc(ite) == scc1; ite = solution.getPrevActivityId(ite));
	}
	for (Id prev = 0; ite != sentinel; ite = prev){
		prev = solution.getPrevActivityId(ite);
		if (!precede(ite, target)) { solution.shiftAfter(ite, target); }
	}
}


void
OptSeqImpl::collectMovesChangeMode(Id id) {
	if (changeModeActivityFlag[id] == iteration || activities[id]->getNumModes() == 1) { return; }
	changeModeActivityFlag[id] = iteration;

	// autoselect
	if (activities[id]->getAutoSelect() > 0) { return; }

	// independent mode
	if (activities[id]->getNumDependences() == 0) {
#if 0
		LocalId lid = (solutions[current].getModeLocalId(id) + rand(1, activities[id]->getNumModes() - 1))
			% activities[id]->getNumModes();
		addMove(Move::Type::ChangeMode, id, lid);
#else
		for (LocalId lid = 0; lid < activities[id]->getNumModes(); ++lid) {
			if (solutions[current].getModeLocalId(id) == lid) { continue; }
			addMove(Move::Type::ChangeMode, id, lid);
		}
#endif
	}
	// dependent mode
	else {
		for (LocalId lid = 0; lid < activities[id]->getNumDependences(); ++lid) {
			Id aid = activities[id]->getDependence(lid);
			if (changeModeActivityFlag[aid] == iteration || activities[aid]->getNumModes() == 1) { continue; }
			for (LocalId lid = 0; lid < activities[aid]->getNumModes(); ++lid) {
				if (solutions[current].getModeLocalId(aid) == lid) { continue; }
				addMove(Move::Type::ChangeMode, aid, lid);
			}
		}
	}
}

void
OptSeqImpl::collectMovesSoftNrr(Id k){
	Solution &solution = solutions[current];
	const NrrConstraint *constraint = nrrConstraints[k];
	for (Id t = 0; t < constraint->getNumTerms(); ++t){
		Id i = constraint->getActivityId(t);
		if (changeModeActivityFlag[i] == iteration) continue;
		// autoselect
		if (activities[i]->getAutoSelect()) {
			const Mode* mode = modes[getActivityModeId(i, solution.getModeLocalId(i))];
			for (LocalId ss = 0; ss < mode->getNumStates(); ++ss) {
				State* state = states[mode->getStateId(ss)];
				const vector<Id>& acts = state->getActivities();
				for (auto ite = acts.begin(); ite != acts.end(); ++ite) {
					if (i == *ite) { continue; }
					if (solution.getPosition(i) < solution.getPosition(*ite)) { addMove(Move::Type::ShiftForward, *ite, i); }
					else {
						Id i2 = solution.getNextActivityId(*ite);
						if (getActivityScc(i) == getActivityScc(*ite)) { // shiftAfter(i, *ite)
							if (!precede(i2, i) && i != i2) { addMove(Move::Type::ShiftForward, i, i2); }
						}
						else {
							while (getActivityScc(i2) == getActivityScc(*ite)) i2 = solution.getNextActivityId(i2);
							if (!precede(i2, i) && i != i2) { addMove(Move::Type::ShiftForward, i, i2); }
						}
					}
				}
			}
		}
		collectMovesChangeMode(i);
	}
}

void OptSeqImpl::collectMovesTemporal(Id tmpConstraintId) {
	collectMovesDueDate(tempConstraints[tmpConstraintId]->getPredId(), 0);
	collectMovesChangeMode(tempConstraints[tmpConstraintId]->getSuccId());
}

void
OptSeqImpl::collectMovesDueDate(Id id, Int depth) {
#if DEBUG > 1
	cout << "collectMovesDueDate: " << id << " " << getActivityName(id) << endl;
#endif
	collectMovesChangeMode(id);

	Solution& solution = solutions[current];
	vector<Id>& critical = solution.getCritical(id);
	if (critical.empty()) return;
	while (!critical.empty()) {
		UInt k = rand(0, static_cast<Int>(critical.size()) - 1);
		UInt pid = critical[k];
		swap(critical[k], *critical.rbegin());
		critical.pop_back();

		Id id2 = pid, id1 = id;
		if (solution.getPosition(id1) < solution.getPosition(id2)) {
			swap(id2, id1);
		}
		//cout << getActivityName(id2) << "<-" << getActivityName(id1) << endl;

		if (!precede(id2, id1)) {
			addMove(Move::Type::ShiftForward, id1, id2);

		}

		collectMovesDueDate(pid, depth + 1);
		if (solution.getExecutions(SinkId).empty()) {
			if (depth > 0) break; // the current solution is infeasible
		}
		else {
			if (depth > DEPTH) break; // feasible
		}
	}
}

void 
OptSeqImpl::addMove(Move::Type type, Id id1, Id id2) {
	switch (type) {
	case Move::Type::ChangeMode:
		if (changeModeFlag[id1][id2] != iteration) {
			moves.emplace_back(type, id1, id2, id1, id1);
			changeModeFlag[id1][id2] = iteration;
		}
		break;

	case Move::Type::ShiftForward:
		if (getActivityScc(id1) == getActivityScc(id2)) {
			if (shiftForwardFlag[id1][id2] != iteration) {
				moves.emplace_back(type, id1, id2, getNumActivities() + id1, getNumActivities() + id2);
				shiftForwardFlag[id1][id2] = iteration;
			}
		}
		else {
			if (shiftForwardSccFlag[getActivityScc(id1)][getActivityScc(id2)] != iteration) {
				moves.emplace_back(type, id1, id2, getNumActivities() * 2 + getActivityScc(id1), getNumActivities() * 2 + getActivityScc(id2));
				shiftForwardSccFlag[getActivityScc(id1)][getActivityScc(id2)] = iteration;
			}
		}
		break;

	default:
		break;
	}
}