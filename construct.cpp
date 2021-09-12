#include "optseqimpl.h"
#define DEBUG 0

bool
OptSeqImpl::makeModesFeasible(Solution &solution, Id fixed){
	csp->resetFixedVariables();
	if (fixed < activities.size()) { csp->setFixedVariable(fixed); }
	return csp->solve(solution.getModeLocalIds()) < csp->getIterationLimit();
}

void 
OptSeqImpl::calculatePenalty(Solution &solution){
	// calculate objective value and evaluation value
	Int objective = 0;
	double evaluation = 0;
	// --- Feasible case
	if (solution.isFeasible()){ 
		solution.clearViolations();
		// due dates
		for (Id i = 0; i < getNumActivities(); i++) {
			Activity* activity = activities[i];
			Int penalty = 0;
			Int duedate;
			//   for start time
			if ((duedate = activity->getDueDate(Activity::DueDateType::START)) < Inf) {
				Int lateness = solution.getExecutions(i).begin()->from - duedate;
				Int tardiness = getActivityBackward(i) ? max<Int>(0, -lateness) : max<Int>(0, lateness);
				if (tardiness > 0) {
					auto tp = activity->getTardinessPenalty(Activity::DueDateType::START);
					switch (tp.second) {
					case Activity::PenaltyType::LINEAR: penalty += tp.first * tardiness; break;
					case Activity::PenaltyType::QUADRATIC: penalty += tp.first * tardiness * tardiness; break;
					}
				}
			}
			//   for completion time
			if ((duedate = activity->getDueDate(Activity::DueDateType::COMPLETION)) < Inf) {
				Int lateness = solution.getExecutions(i).rbegin()->to - duedate;
				Int tardiness = getActivityBackward(i) ? max<Int>(0, -lateness) : max<Int>(0, lateness);
				if (tardiness > 0) {
					auto tp = activity->getTardinessPenalty(Activity::DueDateType::COMPLETION);
					switch (tp.second) {
					case Activity::PenaltyType::LINEAR: penalty += tp.first * tardiness; break;
					case Activity::PenaltyType::QUADRATIC: penalty += tp.first * tardiness * tardiness; break;
					}
				}
			}
			if (penalty > 0) {
				objective += penalty;
				evaluation += penalty;
				solution.addViolation(Violation::Type::DUE, i, penalty);
			}
		}
		// soft nrr constraints
		UInt kk = 0;
		for (auto ite = softNrrConstraintIds.begin(); ite != softNrrConstraintIds.end(); ++ite, ++kk) {
			Int penalty = nrrConstraints[*ite]->calculatePenalty(solution);
			if (penalty > 0) {
				double weighted_penalty = penalty * penaltyWeightSoftNrr[kk];
				evaluation += weighted_penalty;
				objective += penalty * nrrConstraints[*ite]->getWeight();
				solution.addViolation(Violation::Type::NRR, kk,
					rand(1, static_cast<Int>(min<double>(weighted_penalty * 100, static_cast<double>(Inf)))));
			}
		}

		if (objective > bigM) {
			stringstream ss;
			ss << "Objective value (" << objective << ") exceeds the limit (" << bigM << ")";
			string message(ss.str());
			throw runtime_error(message);
		}
	}
	// --- Infeasible case
	else{
		objective = bigM + getNumActivities() - solution.getNumActivitiesAssigned();
		evaluation = static_cast<double>(objective) * maxWeightMultiplier;
	}
	solution.setObjective(objective);
	solution.setEvaluation(evaluation);
}

void 
OptSeqImpl::schedule(Solution& solution) {
	//cout <<  "#" << endl;
	//for (Id i = SourceId; i < getNumActivities(); i = solution.getNextActivityId(i)) {
	//	cout << getActivityName(i) << " " << (getActivityNumModes(i) > 1 ? getModeName(getActivityModeId(i, solution.getModeLocalId(i))) : "---") << endl;
	//}
	//cout << endl;

	// --- schedule
	// initialization
	for (Id r = 0; r < getNumResources(); r++) {
		resources[r]->clear();
		solution.clearResourceResiduals(r);
	}
	for (Id s = 0; s < getNumStates(); s++) { states[s]->clear(); }
	for (Id i = 0; i < getNumActivities(); i++) {
		solution.getExecutions(i).clear();
		solution.clearCritical(i);
		if (getActivityBackward(i)) {
			activities[i]->clearUbStart();
			activities[i]->clearUbFinish();
		}
		else {
			activities[i]->clearLbStart();
			activities[i]->clearLbFinish();
		}
		//activities[i]->memoryClear();
	}
	solution.resetNumActivitiesAssigned();
	solution.clearViolations();
	solution.calculatePositions();
	numBacktracks = 0;
	for (Id k = 0; k < getNumTempConstraints(); ++k) { violationCount[k] = 0; }

	// main loop
	Id activityId = SourceId, scc = getActivityScc(activityId);
	while (1) {
		// "activityId" is updated within "schedule"
		auto activity = activities[activityId];
		if (activity->getAutoSelect() && activity->getNumModes() > 1) {
//			LocalId mm = 0;
//			if (activity->getAutoSelect() == 2 && !activity->memoryNeedsUpdate()) { // autoselect slow
//#if DEBUG > 1
//				cout << "### next mode" << endl;
//#endif
//				mm = activity->getNextModeLocalId();
//			}
//			if (activity->getAutoSelect() != 2 || activity->memoryNeedsUpdate()) {
//				LocalId mm_init = solution.getModeLocalId(activityId);
//				UInt k;
//				for (k = 0; k < activity->getNumModes(); ++k) {
//					mm = (mm_init + k) % activity->getNumModes();
//					solution.setModeLocalId(activityId, mm);
//					activity->memoryLbs();
//					Int start = schedule(solution, activityId, true);
//					activity->memoryUpdate(mm, start);
//					activity->resumeLbs();
//					// first valid mode
//					if (activity->getAutoSelect() == 3 && start < Inf / 2) break; // autoselect fast
//				}
//				if (activity->getAutoSelect() != 3) mm = activity->memorySort();
//			}
//			solution.setModeLocalId(activityId, mm);

			LocalId mm_init = solution.getModeLocalId(activityId), best_mm = mm_init;
			Int min_completion = Inf;
			for (UInt k = 0; k < activity->getNumModes(); ++k) {
				LocalId mm = (mm_init + k) % activity->getNumModes();
				solution.setModeLocalId(activityId, mm);
				activity->memoryLbs();
				Int completion = schedule(solution, activityId, true);
				activity->resumeLbs();
				if (completion < min_completion) {
					min_completion = completion;
					best_mm = mm;
				}
				// first valid mode
				if (activity->getAutoSelect() == 3 && completion < Inf) { break; } // autoselect first
			}
			solution.setModeLocalId(activityId, best_mm);
		}
		if (activity->getBackward()) { if (scheduleReverse(solution, activityId)) { break; } }
		else { if (schedule(solution, activityId)) { break; } }
		if (activityId >= getNumActivities()) { break; }
		if (scc != getActivityScc(activityId)) {
			numBacktracks = 0;
			scc = getActivityScc(activityId);
		}
	}
	calculatePenalty(solution);
	solution.setIteration(iteration);
	solution.setCpuTime(cputime());
	//cout << numBacktracks << endl;
	// set resource residuals
	if (solution.isFeasible()) {
		Int makespan = solution.getExecutions(SinkId).begin()->from;
		for (Id r = 0; r < getNumResources(); r++) {
			solution.setResourceResiduals(r, resources[r]->getProfileBegin(), makespan);
		}
	}
	//exit(1);
}

Int
OptSeqImpl::schedule(Solution& solution, Id& activityId, bool test) {
#if DEBUG > 1
	cout << getActivityName(activityId) << " " << (getActivityNumModes(activityId) > 1 ?
		getModeName(getActivityModeId(activityId, solution.getModeLocalId(activityId))) : "---") << endl;
#endif

	// returns the completion time of the activity if it succeeds in assigning the activity and test = true; otherwise, returns 0 or inf 
	auto activity = activities[activityId];
	LocalId modeLocalId = solution.getModeLocalId(activityId);
	auto mode = modes[activity->getModeId(modeLocalId)];
	Int duration = mode->getDuration();
	// -- calculate LB	
	criticalsS.clear();
	criticalsF.clear();
	auto num1 = activity->getNumInTempConstraints();
	auto num2 = activity->getNumInTempConstraintsForMode(modeLocalId);
	for (LocalId kk = 0; kk < num1 + num2; ++kk) {
		auto temporal = (kk < num1) ? activity->getInTempConstraint(kk) : activity->getInTempConstraintForMode(modeLocalId, kk - num1);
		Id predId = temporal->getPredId();
		auto& predExecutions = solution.getExecutions(predId);
		if (predExecutions.empty()) { continue; } // predecessor has not yet scheduleed
		if (kk >= num1 && temporal->getPredModeLocalId() != solution.getModeLocalId(predId)) { continue; }
		Int delay = temporal->getDelay();
		if (test) {
			switch (temporal->getType()) {
			case TempConstraintType::SS: activity->updateLbStart(predExecutions.begin()->from + delay); break;
			case TempConstraintType::SC: activity->updateLbFinish(predExecutions.begin()->from + delay); break;
			case TempConstraintType::CS: activity->updateLbStart(predExecutions.rbegin()->to + delay); break;
			case TempConstraintType::CC: activity->updateLbFinish(predExecutions.rbegin()->to + delay); break;
			}
		}
		else {
			switch (temporal->getType()) {
			case TempConstraintType::SS: activity->updateLbStart(predExecutions.begin()->from + delay, criticalsS, predId); break;
			case TempConstraintType::SC: activity->updateLbFinish(predExecutions.begin()->from + delay, criticalsF, predId); break;
			case TempConstraintType::CS: activity->updateLbStart(predExecutions.rbegin()->to + delay, criticalsS, predId); break;
			case TempConstraintType::CC: activity->updateLbFinish(predExecutions.rbegin()->to + delay, criticalsF, predId); break;
			}
		}
	}
	//	 - add critical arc(s)
	if (!test) {
		for (auto id : criticalsS) {
			if (id != SourceId) { solution.setCritical(activityId, id); }
		}
		for (auto id : criticalsF) {
			if (id != SourceId) { solution.setCritical(activityId, id); }
		}
	}

	// -- find start time(s)
	//  - lower bound for each sub-activitity
	lbs.clear();
	Int lb = activity->getLbFinish();
	lbs.emplace_back(duration, duration, lb, 1);
	for (auto bd = mode->getMaxBreakDurations(duration); ; bd = bd->getPrev()) {
		Int from = max<Int>(bd->getTo() - (lb - activity->getLbStart()) / (1 + bd->getKey()), bd->getFrom() - 1);
		lb -= (bd->getTo() - from) * (1 + bd->getKey());
		if (from < bd->getTo()) { lbs.emplace_back(from, bd->getTo(), lb, 1 + bd->getKey()); }
		if (from != bd->getFrom() - 1 || bd->getFrom() <= 1) { break; }
	}
	if (lbs.rbegin()->from == -1) {
		lbs.rbegin()->from = 0;
		lbs.rbegin()->value += lbs.rbegin()->grad;
	}
	if (lbs.rbegin()->from == 0) {
		activity->updateLbStart(lbs.rbegin()->value - mode->getMaxBreakDurations(0)->getKey());
	}
#if DEBUG > 1
	auto printLBs = [this]() {
		for (auto ite = lbs.rbegin(); ite != lbs.rend(); ++ite) {
			cout << "\t\t(" << ite->from << "," << ite->value << ")" <<
				"-" << "(" << ite->to << "," << ite->value + (ite->grad * (ite->to - ite->from)) << ")"
				<< " grad = " << ite->grad << endl;
		}
	};
	//printLBs();
#endif

	//	- first segment; start time
	auto& executions = solution.getExecutions(activityId);
	Int t = activity->getLbStart();
	Int progress = 0;
	executions.emplace_back(t, t, 0);

#if DEBUG > 1
	cout << "#" << activityId << " " << getActivityName(activityId) << " t=" << t << endl;
#endif
	//	- main loop
	stateValues.resize(mode->getNumStates());
	while (1) {
		// check state variables; just after assigning the start time
		if (t == executions[0].from) { // progress == 0
			//if (progress != 0) { throw runtime_error("in construct; check state variables"); }
			while (t < Inf) {
				LocalId ss;
				for (ss = 0; ss < mode->getNumStates(); ++ss) {
					State* state = states[mode->getStateId(ss)];
					Int init_t = t;
					Int to = 0;
					for (const IntervalImpl* stateInterval = state->getInterval(t); t < Inf;
						stateInterval = stateInterval->getNext(), t = stateInterval->getFrom()) {
						// check "from"
						Id bid = state->getActivityAssigned(stateInterval); // before
						Int from = state->getValue(stateInterval);
						to = mode->getStateValue(ss, from);
						if (to < 0) { // invalid transition
							if (!test) {
								// set critical
								if (bid == UInf) { // no activity changes the value
									const vector<Id>& acts = state->getActivities();
									for (vector<Id>::const_iterator ite = acts.begin(); ite != acts.end(); ++ite) {
										if (activityId == *ite) continue;
										//				solution.setCritical(id, *ite);
										if (solution.getPosition(activityId) < solution.getPosition(*ite)) {
											solution.setCritical(activityId, *ite);
										}
										else {
											if (getActivityScc(activityId) == getActivityScc(*ite)) { // shiftAfter(id, *ite)
												solution.setCritical(activityId, solution.getNextActivityId(*ite));
											}
											else {
												Id id_ite = solution.getNextActivityId(*ite);
												while (getActivityScc(id_ite) == getActivityScc(*ite)) {
													id_ite = solution.getNextActivityId(id_ite);
												}
												solution.setCritical(activityId, id_ite);
											}
										}
									}
								}
								else { // some activity has changed the value
									solution.setCritical(activityId, bid);
								}
							} // end: set critical
							continue;
						}
						// check "to"
						Id aid = state->getActivityAssigned(stateInterval->getNext()); // after
						if (aid < UInf) {
							if (stateInterval->getNext()->getFrom() == t + 1 || from != to) {
								if (!test) { solution.setCritical(activityId, aid); }
								continue;
							}
						}
						break;
					}
					if (t != init_t) { break; }
					stateValues[ss] = to;
				}
				if (ss == mode->getNumStates()) { break; }
			}
			executions[0].from = executions[0].to = t; // update (delay) the start time 
		} // end of state-check
		if (t >= Inf) {
			executions.clear();
			if (!test) {
				//cout << "[S]";
				solution.addViolation(Violation::Type::DUE, activityId, Inf);
			}
			return Inf;
		}

		// check LB and update t if necessary
		if (!lbs.empty() && progress == lbs.rbegin()->from && lbs.rbegin()->value > t) { t = lbs.rbegin()->value; }
		//	remove redundant segment of LBs; so "lbs[].value > t", which implies that "lbs[].from > progress"
		while (!lbs.empty() && lbs.rbegin()->value <= t) {
			if (lbs.rbegin()->value + (lbs.rbegin()->to - lbs.rbegin()->from) * lbs.rbegin()->grad <= t) { lbs.pop_back(); }
			else {
				Int x = (t - lbs.rbegin()->value) / lbs.rbegin()->grad + lbs.rbegin()->from + 1;
				lbs.rbegin()->value += (x - lbs.rbegin()->from) * lbs.rbegin()->grad;
				lbs.rbegin()->from = x;
				break;
			}
		}
	

#if DEBUG > 1
		cout << "#" << activityId << " " << getActivityName(activityId) << " t=" << t << " " << progress << endl;
		printLBs();
#endif

		//  check if the next sub-activity can be assigned; if yes, nextProgress is calculatd, if no, retry & lb are calculated
		Int nextProgress = duration; // for success
		Int retry = Inf; // for failure
		// if breakpoint = -1, start time must be delayed in case of failure
		auto breakDuration = mode->getMaxBreakDurations(progress);
		Int breakPoint = (breakDuration->getKey() > 0) ? progress : breakDuration->getFrom() - 1;
		auto parallelInterval = mode->getMaxNumsParallel(progress + 1); // numParallel->getFrom() <= progress+1 <= numParallel->getTo()
		Int parallel = min<Int>(parallelInterval->getKey(), duration - progress);
		if (progress < duration) { // except for the case that all of the sub-activities have already been assigned
			if (!lbs.empty()) { // note: lbs.rbegin()->from > progress
				//if (lbs.rbegin()->from <= progress) { throw runtime_error("caluclate parallel"); }
				parallel = min<Int>(parallel, lbs.rbegin()->from - progress); // note: lbs.rbegin()->lb > t
				if (parallel == 1 && lbs.rbegin()->grad == 1 && t + lbs.rbegin()->from - progress >= lbs.rbegin()->value) {
					nextProgress = lbs.rbegin()->to + 1; // modified
				}
				else { nextProgress = lbs.rbegin()->from; }
			}
			//cout << "\tnextProgress = " << nextProgress << ", parallel = " << parallel << endl;
			bool increaseTby1 = false;
			// check the resource constraints
			for (LocalId rr = 0; rr < mode->getNumRequiredResources(); rr++) {
				auto resource = resources[mode->getRequiredResourceId(rr)];
				bool maximum = (mode->getRequirements(rr, false, true) != 0);
				auto requirement = mode->getRequirements(rr, false, maximum, progress + 1);
				auto profile = resource->getProfile(t + 1);
				if (requirement->getKey() > profile->getKey()) { // cannot assign
					parallel = 0;
					do {
						if (!test) { solution.setCritical(activityId, resource, profile->getFrom(), profile->getTo()); }
						profile = profile->getNext();
					} while (requirement->getKey() > profile->getKey() && profile->getTo() <= Inf);
					// the resource is not sufficient enough to process the portion corresponding to "requirement" until profile->getFrom()-1
					Int bp = breakPoint * 2 + 1; // -1, 1, 3, ...
					if (breakPoint != -1) {
						auto breakRequirement = mode->getRequirements(rr, true, maximum, breakPoint);
						if (breakRequirement->getKey() >= requirement->getKey()) { bp = breakRequirement->getFrom() * 2; } // lazy calculation; 0, 2, 4, ...
					}
					Int retry_temp = max<Int>(requirement->getFrom() * 2 - 1, bp); // >= 0 // (getFrom()-1)*2+1
					if (retry_temp < retry) { retry = retry_temp; lb = 0; }
					if (retry_temp == retry) { lb = max<Int>(lb, profile->getFrom() - 1); }
				}
				// check if parallel should be reduced
				else if (parallel > 1) {
					Int p = (maximum || requirement->getKey() == 0) ?
						parallel : min<Int>(parallel, profile->getKey() / requirement->getKey());
					if (requirement->getTo() <= progress + p) { // different types of units may be embedded
						p = requirement->getTo() - progress;
						Int req = (maximum) ? requirement->getKey() : requirement->getKey() * p;
						for (Int delta = 0, add = 0; p < parallel; req += add, p += delta) {
							requirement = requirement->getNext();
							delta = requirement->getTo() - requirement->getFrom() + 1;
							if (maximum) { add = (requirement->getKey() > req) ? requirement->getKey() - req : 0; }
							else { add = requirement->getKey() * delta; }
							if (req + add > profile->getKey()) {
								if (!maximum) { p += (profile->getKey() - req) / requirement->getKey(); }
								break;
							}
						}
						increaseTby1 = true;
					}
					parallel = min(parallel, p);
				}
			} // end of for rr
			if (retry == Inf) {
				// calculate nextProgress
				if (increaseTby1) { nextProgress = progress + parallel; }
				else { // in this case, the next segment consists of identical unit(s)
					nextProgress -= (nextProgress - progress) % parallel;
					if (nextProgress > progress + parallel) {
						auto ptr = parallelInterval;
						//while (ptr->getTo() < duration && ptr->getNext()->getKey() >= parallel) { ptr = ptr->getNext(); }
						//nextProgress = min(nextProgress, progress + parallel * ((ptr->getTo() + ptr->getKey() - 1 - progress) / parallel));
						nextProgress = min<Int>(nextProgress, progress + parallel * ((min<Int>(ptr->getTo() + ptr->getKey() - 1, duration) - progress) / parallel));
						for (LocalId rr = 0; rr < mode->getNumRequiredResources(); rr++) {
							Resource* resource = resources[mode->getRequiredResourceId(rr)];
							bool maximum = (mode->getRequirements(rr, false, true) != 0);
							auto requirement = mode->getRequirements(rr, false, maximum); // the last arg. = progress+1// redundant?
							auto profile = resource->getProfile(); // t+1
							if (parallel == 1) {
								nextProgress = min<Int>(nextProgress, min<Int>(requirement->getTo(), progress + profile->getTo() - t));
							}
							else {
								nextProgress = min<Int>(nextProgress,
									progress + parallel * min<Int>((requirement->getTo() - progress) / parallel, profile->getTo() - t));
							}
						}
					}
				}
			}
		}
		if (retry == Inf) {
			// check break
			Int gap = t - executions.rbegin()->to;
			if (gap > 0) {
				//	- check break; max break duration 
				if (gap > breakDuration->getKey()) {
					if (progress == 0) { // delay the start time
						retry = 0;
						lb = t - breakDuration->getKey();
					}
					else { //	push LB
						auto ite = executions.rbegin();
						Int left = max<Int>(breakDuration->getFrom() - 1, progress - (ite->to - ite->from) * ite->parallel);
						if (ite->parallel == 1) {
							if (breakDuration->getKey() > 0) { left = max<Int>(left, progress - (t - ite->to) / breakDuration->getKey()); }
						}
						else {
							if (breakDuration->getKey() <= t - ite->to) {
								left = max(left, progress - (t - ite->to) * ite->parallel / ((1 + breakDuration->getKey()) * ite->parallel - 1));
							}
							else { left = progress; }
						}
						if (left >= progress) { throw runtime_error("push LB"); }
						// insert new segment
						lb = t - (progress - left) * (1 + breakDuration->getKey());
						lbs.emplace_back(left, progress, lb, 1 + breakDuration->getKey());
						retry = left * 2 + 1;
					}
				}
				//	- check break; resource availability
				else {
					for (LocalId rr = 0; rr < mode->getNumRequiredResources(); rr++) {
						auto resource = resources[mode->getRequiredResourceId(rr)];
						bool maximum = (mode->getRequirements(rr, true, true) != 0);
						auto breakRequirement = mode->getRequirements(rr, true, maximum, progress);
						// -- check whether resource is enough during the break; !!! there remain some redundant calculations !!!
						if (breakRequirement->getKey() == 0) { continue; }
						auto profile = resource->getProfile(t);
						while (1) {
							if (breakRequirement->getKey() > profile->getKey()) {
								if (!test) { solution.setCritical(activityId, resource, profile->getFrom(), profile->getTo()); }
								auto requirement = mode->getRequirements(rr, false, maximum, progress + 1); // ??? progress??
								Int retry_temp = progress * 2;
								if (requirement->getKey() > profile->getKey()) {
									retry_temp = min(retry_temp, requirement->getFrom() * 2 - 1);
								}
								if (retry_temp < retry) { retry = retry_temp; lb = 0; }
								if (retry_temp == retry) { lb = max(lb, min(profile->getTo(), t)); }
								break;
							}
							if (profile->getFrom() <= executions.rbegin()->to + 1) { break; }
							profile = profile->getPrev();
						}
					}
				} // end of checking resource availability for break
			} // end if (gap > 0)

			if (retry == Inf) {
				// consecutively processed				
				if (progress == duration) { // all done?
					executions.emplace_back(t, t, 0);
					break;
				}
				Int increment = (nextProgress - progress) / parallel;
				if (nextProgress != progress + parallel * increment) { throw runtime_error("consecutively processed."); }
				if (executions.rbegin()->to == t && executions.rbegin()->parallel == parallel) {
					executions.rbegin()->to += increment;
				}
				else { executions.emplace_back(t, t + increment, parallel); }
				t += increment;
				progress = nextProgress;

				//	- remove or update redundant segment of LBs; so lbs[].from >= progress
				while (!lbs.empty() && lbs.rbegin()->from < progress) {
					if (lbs.rbegin()->to < progress) { lbs.pop_back(); }
					else {
						lbs.rbegin()->value += (progress - lbs.rbegin()->from) * lbs.rbegin()->grad;
						lbs.rbegin()->from = progress;
						break;
					}
				}
				continue; // next sub-activity to be assigned
			}
		}
		// retry
		if (lbs.empty() || lbs.rbegin()->from > progress) {
			lbs.emplace_back(progress, progress, t, 1 + breakDuration->getKey());
		}
		if (retry == 0) { // delay the start time
			executions.clear();
			executions.emplace_back(lb, lb, 0);
		}
		else {
			if (retry % 2 == 0) {
				--lb;
				--retry;
			}
			// pop executions
			while (progress > retry / 2) {
				Int parallel = executions.rbegin()->parallel;
				Int temp = (executions.rbegin()->to - executions.rbegin()->from) * parallel;
				if (temp <= progress - retry / 2) {
					executions.pop_back();
					progress -= temp;
				}
				else {
					Int from = executions.rbegin()->from;
					Int profile = temp - progress + retry / 2;
					executions.pop_back();
					if (profile / parallel > 0) {
						executions.emplace_back(from, from + profile / parallel, parallel);
						from += profile / parallel;
					}
					if (profile % parallel > 0) {
						if (executions.rbegin()->to == from && executions.rbegin()->parallel == profile % parallel) { // redundant?
							executions.rbegin()->to = from + 1;
						}
						else { executions.emplace_back(from, from + 1, profile % parallel); }
					}
					break;
				}
			}
		}
		progress = retry / 2;
		t = lb;
	} // end of main loop while

	// check upper bounds
	Id backtrack = UInf, minPosition = UInf;
	Id criticalTempConstraintId;
	num1 = activity->getNumOutTempConstraints();
	num2 = activity->getNumOutTempConstraintsForMode(modeLocalId);
	for (LocalId kk = 0; kk < num1 + num2; ++kk) {
		auto temporal = (kk < num1) ? activity->getOutTempConstraint(kk) : activity->getOutTempConstraintForMode(modeLocalId, kk - num1);
		Id succId = temporal->getSuccId();
		if (solution.getExecutions(succId).empty()) { continue; } // successor has not yet scheduleed
		if (kk >= num1 && temporal->getSuccModeLocalId() != solution.getModeLocalId(succId)) { continue; }
		Int delay = temporal->getDelay();
		bool violate = false;
		switch (temporal->getType()) {
		case TempConstraintType::SS:
			if ((violate = (executions.begin()->from + delay > solution.getExecutions(succId).begin()->from)) && !test) {
				activities[succId]->updateLbStart(executions.begin()->from + delay);
			}
			break;
		case TempConstraintType::SC:
			if ((violate = (executions.begin()->from + delay > solution.getExecutions(succId).rbegin()->to)) && !test) {
				activities[succId]->updateLbFinish(executions.begin()->from + delay);
			}
			break;
		case TempConstraintType::CS:
			if ((violate = (executions.rbegin()->to + delay > solution.getExecutions(succId).begin()->from)) && !test) {
				activities[succId]->updateLbStart(executions.rbegin()->to + delay);
			}
			break;
		case TempConstraintType::CC:
			if ((violate = (executions.rbegin()->to + delay > solution.getExecutions(succId).rbegin()->to)) && !test) {
				activities[succId]->updateLbFinish(executions.rbegin()->to + delay);
			}
			break;
		}
		if (violate) {
			if (test) {
#if DEBUG > 1
				cout << "## Backtrack" << endl;
#endif
				executions.clear();
				return Inf;
			}
			else {
				solution.setCritical(succId, activityId);
				if (solution.getPosition(succId) < minPosition) {
					minPosition = solution.getPosition(succId);
					backtrack = succId;
					criticalTempConstraintId = temporal->getId();
#if DEBUG
					cout << "[B] " << minPosition << " " << endl;
#endif
				}
			}
		}
	}
	if (minPosition == UInf) { // backtrack is not necessary
#if DEBUG
		cout << *activities[activityId] << " assigned: " << executions << endl;
#endif
		if (test) { 
			Int ret = executions.begin()->to;
			executions.clear();
			return ret;
		}
		else {
			assign(activityId, mode, executions, +1);
			//solution.setExecution(activityId, executions);
			solution.updateNumActivitiesAssigned(solution.getPosition(activityId) + 1);
			activityId = solution.getNextActivityId(activityId);
			return 0;
		}
	}
	else {  // backtrack is required
		executions.clear();
		if (backtrack == SourceId || numBacktracks++ == maxNumBacktracks) {
		//if (backtrack == SourceId) {
			//cout << "[B]";
			solution.addViolation(Violation::Type::TMP, criticalTempConstraintId, Inf);
			return 1;
		}
		else {
#if DEBUG
			cout << "critical " << tempConstraints[criticalTempConstraintId]->getId() << endl;
#endif
			Id shiftForwardId = UInf;
			if (++violationCount[criticalTempConstraintId] > maxViolationCount) {
				solution.addViolation(Violation::Type::TMP, criticalTempConstraintId, Inf);
				return Inf;
			}
#if 1
			else if (violationCount[criticalTempConstraintId] == 1 && !precede(backtrack, activityId)) {
				shiftForwardId = activityId;
				if (scc[backtrack] != scc[shiftForwardId]) {
					while (scc[backtrack] == scc[solution.getPrevActivityId(backtrack)]) {
						backtrack = solution.getPrevActivityId(backtrack);
					}
				}
			}
#endif
			// backtrack
			activity->clearLbStart();
			activity->clearLbFinish();
			activityId = solution.getPrevActivityId(activityId);
			while (1) {
				if (activityId != backtrack) {
					if (shiftForwardId < UInf) {
						activities[activityId]->clearLbStart();
						activities[activityId]->clearLbFinish();
					}
					else {
						if (getActivityBackward(activityId)) {
							if (solution.getExecutions(activityId).begin()->from <= activities[activityId]->getUbStart()) { activities[activityId]->clearUbStart(); }
							if (solution.getExecutions(activityId).rbegin()->to <= activities[activityId]->getUbFinish()) { activities[activityId]->clearUbFinish(); }
						}
						else {
							if (solution.getExecutions(activityId).begin()->from >= activities[activityId]->getLbStart()) { activities[activityId]->clearLbStart(); }
							if (solution.getExecutions(activityId).rbegin()->to >= activities[activityId]->getLbFinish()) { activities[activityId]->clearLbFinish(); }
						}
					}
				}
				assign(activityId, modes[getActivityModeId(activityId, solution.getModeLocalId(activityId))], solution.getExecutions(activityId), -1);
				solution.getExecutions(activityId).clear();
				if (activityId == backtrack) { break; }
				activityId = solution.getPrevActivityId(activityId);
			}
			// modify the activity list
			if (shiftForwardId < UInf){
#if DEBUG
				cout << "# shift forward: " << activities[backtrack]->getName() << "<-" << activities[shiftForwardId]->getName() << endl;
#endif
				activityId = solution.getPrevActivityId(activityId);
				shiftForward(solution, shiftForwardId, backtrack);
				solution.calculatePositions();
				activityId = solution.getNextActivityId(activityId);
			}
			return 0;
		}
	}
}

void
OptSeqImpl::assign(Id aid, const Mode *mode, const vector<Solution::Execution> &executions, Int sign){
	for (LocalId ss = 0; ss < mode->getNumStates(); ++ss){
		State *state = states[mode->getStateId(ss)];
		if (sign > 0) { state->change(executions.begin()->from + 1, aid, stateValues[ss]); }
		else { state->undo(executions.begin()->from + 1); }
	}

	// assign or cancel the activity
	for (LocalId rr = 0; rr < mode->getNumRequiredResources(); rr++){
		Resource *resource = resources[mode->getRequiredResourceId(rr)];
		bool maximum = (mode->getRequirements(rr, false, true) != 0);
		const IntervalImpl *requirement = mode->getRequirements(rr, false, maximum, 1);
		//cout << resource->getName() << endl;
		Int progress = 0;
		for (UInt k = 1; k < executions.size(); k++){
			if (executions[k-1].to < executions[k].from){
				Int req = mode->getRequirements(rr, true, maximum, progress)->getKey();
				if (req > 0){
					//cout << executions[k-1].to+1 << "-" << executions[k].from << " " << req << endl;
					resource->consume(executions[k-1].to+1, executions[k].from, req * sign, aid);
				}
			}
			Int t = executions[k].from, fraction = 0, parallel = executions[k].parallel;
			Int maximum_req = 0;
			while (t < executions[k].to) {
				if (fraction > 0) {
					Int nextFraction = min(parallel, fraction + requirement->getTo() - progress);
					if (maximum) {
						if (requirement->getKey() > maximum_req) {
							resource->consume(t + 1, t + 1, (requirement->getKey() - maximum_req) * sign, aid);
							maximum_req = requirement->getKey();
						}
					}
					else {
						//cout << t+1 << "-" << t+1 << " " << requirement->getKey() << "*" << nextFraction - fraction << endl;
						resource->consume(t + 1, t + 1, requirement->getKey() * (nextFraction - fraction) * sign, aid);
					}
					progress += nextFraction - fraction;
					if ((fraction = nextFraction % parallel) == 0) ++t;
				}
				else { // the case of fraction == 0
					Int increment = min((requirement->getTo() - progress) / parallel, executions[k].to - t);
					if (increment > 0) {
						Int num = (maximum) ? 1 : parallel;
						//cout << t+1 << "-" << t+increment << " " << requirement->getKey() << "*" << num << endl;
						resource->consume(t + 1, t + increment, requirement->getKey() * num * sign, aid);
						t += increment;
						progress += increment * parallel;
					}
					if (t < executions[k].to && (fraction = (requirement->getTo() - progress) % parallel) > 0) {
						Int num;
						if (maximum) { num = 1; maximum_req = requirement->getKey(); }
						else num = fraction;
						//cout << t+1 << "-" << t+1 << " " << requirement->getKey() << "*" << num << endl;
						resource->consume(t + 1, t + 1, requirement->getKey() * num * sign, aid);
						progress += fraction;
					}
				}
				if (progress == requirement->getTo()) { requirement = requirement->getNext(); }
			}
		}
	} // end for 
}


