#include "optseqimpl.h"
#define DEBUG 0

Int
OptSeqImpl::scheduleReverse(Solution& solution, Id& activityId, bool test) {
#if DEBUG > 1
	cout << getActivityName(activityId) << " " << (getActivityNumModes(activityId) > 1 ?
		getModeName(getActivityModeId(activityId, solution.getModeLocalId(activityId))) : "---") << endl;
#endif

	// returns the completion time of the activity if it succeeds in assigning the activity and test = true; otherwise, returns 0 or inf 
	auto activity = activities[activityId];
	LocalId modeLocalId = solution.getModeLocalId(activityId);
	auto mode = modes[activity->getModeId(modeLocalId)];
	auto rmode = mode->getReverseMode();
	Int duration = mode->getDuration();
	// -- calculate UB
	activity->updateUbStart(activity->getDueDate(Activity::DueDateType::START));
	activity->updateUbFinish(activity->getDueDate(Activity::DueDateType::COMPLETION));
	criticalsS.clear();
	criticalsF.clear();
	auto num1 = activity->getNumOutTempConstraints();
	auto num2 = activity->getNumOutTempConstraintsForMode(modeLocalId);
	for (LocalId kk = 0; kk < num1 + num2; ++kk) {
		auto temporal = (kk < num1) ? activity->getOutTempConstraint(kk) : activity->getOutTempConstraintForMode(modeLocalId, kk - num1);
		Id succId = temporal->getSuccId();
		auto& succExecutions = solution.getExecutions(succId);
		if (succExecutions.empty()) { continue; } //  has not yet scheduleed
		if (kk >= num1 && temporal->getSuccModeLocalId() != solution.getModeLocalId(succId)) { continue; }
		Int delay = temporal->getDelay();
		if (test) {
			switch (temporal->getType()) {
			case TempConstraintType::SS: activity->updateUbStart(succExecutions.begin()->from - delay); break;
			case TempConstraintType::SC: activity->updateUbStart(succExecutions.rbegin()->to - delay); break;
			case TempConstraintType::CS: activity->updateUbFinish(succExecutions.begin()->from - delay); break;
			case TempConstraintType::CC: activity->updateUbFinish(succExecutions.rbegin()->to - delay); break;
			}
		}
		else {
			switch (temporal->getType()) {
			case TempConstraintType::SS: activity->updateUbStart(succExecutions.begin()->from - delay, criticalsS, succId); break;
			case TempConstraintType::SC: activity->updateUbStart(succExecutions.rbegin()->to - delay, criticalsS, succId); break;
			case TempConstraintType::CS: activity->updateUbFinish(succExecutions.begin()->from - delay, criticalsF, succId); break;
			case TempConstraintType::CC: activity->updateUbFinish(succExecutions.rbegin()->to - delay, criticalsF, succId); break;
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

	// -- find completion time(s)
	//  - upper bound for each sub-activitity
	auto& ubs = lbs;
	ubs.clear();
	Int ub = activity->getUbStart();
	ubs.emplace_back(duration, duration, ub, 1);
	for (auto bd = rmode->getMaxBreakDurations(duration); ; bd = bd->getPrev()) {
		Int from = max<Int>(bd->getTo() - (ub + activity->getUbFinish()) / (1 + bd->getKey()), bd->getFrom() - 1);
		ub += (bd->getTo() - from) * (1 + bd->getKey());
		if (from < bd->getTo()) { ubs.emplace_back(from, bd->getTo(), ub, 1 + bd->getKey()); 
		//cout << from << "-" << bd->getTo() << " " << ub << " grad = " << 1 + bd->getKey() << endl;
		}
		if (from != bd->getFrom() - 1 || bd->getFrom() <= 1) { break; }
	}
	if (ubs.rbegin()->from == -1) {
		ubs.rbegin()->from = 0;
		ubs.rbegin()->value -= ubs.rbegin()->grad;
	}
	if (ubs.rbegin()->from == 0) {
		activity->updateUbFinish(ubs.rbegin()->value + rmode->getMaxBreakDurations(0)->getKey());
	}
#if DEBUG > 1
	auto printUBs = [&]() {
		for (auto ite = ubs.rbegin(); ite != ubs.rend(); ++ite) {
			cout << "\t\t(" << ite->from << "," << ite->value << ")" <<
				"-" << "(" << ite->to << "," << ite->value - (ite->grad * (ite->to - ite->from)) << ")"
				<< " grad = " << ite->grad << endl;
		}
		cout << "\t\tUB finish = " << activity->getUbFinish() << endl;
	};
	printUBs();
#endif
	

	//	- first segment; start time
	auto& executions = solution.getExecutions(activityId);
	Int t = activity->getUbFinish();
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
			if (mode->getNumStates() > 0) { throw runtime_error("backward scheduling of an activity with state-variables is not supported"); }
		} // end of state-check

		if (t <= 0 && progress < duration) {
			executions.clear();
			if (!test) {
				//cout << "[S]";
				solution.addViolation(Violation::Type::DUE, activityId, Inf);
			}
			return Inf;
		}
		// check UB and update t if necessary
		if (!ubs.empty() && progress == ubs.rbegin()->from && ubs.rbegin()->value < t) { t = ubs.rbegin()->value; }
		//	remove redundant segment of UBs; so "ubs[].value < t", which implies that "ubs[].from < progress"
		while (!ubs.empty() && ubs.rbegin()->value >= t) {
			if (ubs.rbegin()->value - (ubs.rbegin()->to - ubs.rbegin()->from) * ubs.rbegin()->grad >= t) { ubs.pop_back(); }
			else {
				Int x = (ubs.rbegin()->value - t) / ubs.rbegin()->grad + ubs.rbegin()->from + 1;
				ubs.rbegin()->value -= (x - ubs.rbegin()->from) * ubs.rbegin()->grad;
				ubs.rbegin()->from = x;
				break;
			}
		}
	
#if DEBUG > 1
		cout << "#" << activityId << " " << getActivityName(activityId) << " t=" << t << " " << progress << endl;
		cout << executions << endl;
		printUBs();
#endif

		//  check if the next sub-activity can be assigned; if yes, nextProgress is calculatd, if no, retry & ub are calculated
		Int nextProgress = duration; // for success
		Int retry = Inf; // for failure
		// if breakpoint = -1, completion time must be moved up in case of failure
		auto breakDuration = rmode->getMaxBreakDurations(progress);
		Int breakPoint = (breakDuration->getKey() > 0) ? progress : breakDuration->getFrom() - 1;
		auto parallelInterval = rmode->getMaxNumsParallel(progress + 1); // numParallel->getFrom() <= progress+1 <= numParallel->getTo()
		Int parallel = min<Int>(parallelInterval->getKey(), duration - progress);
		if (progress < duration) { // except for the case that all of the sub-activities have already been assigned
			if (!ubs.empty()) { // note: ubs.rbegin()->from > progress
				parallel = min<Int>(parallel, ubs.rbegin()->from - progress); // note: ubs.rbegin()->value < t
				if (parallel == 1 && ubs.rbegin()->grad == 1 && t - ubs.rbegin()->from + progress <= ubs.rbegin()->value) {
					nextProgress = ubs.rbegin()->to + 1;
				}
				else { nextProgress = ubs.rbegin()->from; }
			}
#if DEBUG > 2
			cout << "\tnextProgress = " << nextProgress << ", parallel = " << parallel << endl;
#endif
			bool increaseTby1 = false;
			// check the resource constraints
			for (LocalId rr = 0; rr < rmode->getNumRequiredResources(); rr++) {
				auto resource = resources[rmode->getRequiredResourceId(rr)];
				bool maximum = (rmode->getRequirements(rr, false, true) != 0);
				auto requirement = rmode->getRequirements(rr, false, maximum, progress + 1);
				auto profile = resource->getProfile(t);
				if (requirement->getKey() > profile->getKey()) { // cannot assign
					parallel = 0;
					do {
						if (!test) { solution.setCritical(activityId, resource, profile->getFrom(), profile->getTo()); }
						profile = profile->getPrev();
					} while (profile && requirement->getKey() > profile->getKey());
					// the resource is not sufficient enough to process the portion corresponding to "requirement" until profile->getTo()
					Int bp = breakPoint * 2 + 1; // -1, 1, 3, ...
					if (breakPoint != -1) {
						auto breakRequirement = rmode->getRequirements(rr, true, maximum, breakPoint);
						if (breakRequirement->getKey() >= requirement->getKey()) { bp = breakRequirement->getFrom() * 2; } // lazy calculation; 0, 2, 4, ...
					}
					Int retry_temp = max<Int>(requirement->getFrom() * 2 - 1, bp); // >= 0
					if (retry_temp < retry) { retry = retry_temp; ub = Inf; }
					if (retry_temp == retry) { ub = min<Int>(ub, profile ? profile->getTo() : -1); }
#if DEBUG > 2
					cout << "\tresource: " << retry / 2 << " " << (retry % 2 ? " " : "-") << ub << endl;
#endif
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
						for (LocalId rr = 0; rr < rmode->getNumRequiredResources(); rr++) {
							Resource* resource = resources[rmode->getRequiredResourceId(rr)];
							bool maximum = (rmode->getRequirements(rr, false, true) != 0);
							auto requirement = rmode->getRequirements(rr, false, maximum); // the last arg. = progress+1 // redundant?
							auto profile = resource->getProfile(); // t // redundant?
							if (parallel == 1) {
								nextProgress = min<Int>(nextProgress, min<Int>(requirement->getTo(), progress + t - profile->getFrom() + 1));
							}
							else {
								nextProgress = min<Int>(nextProgress,
									progress + parallel * min<Int>((requirement->getTo() - progress) / parallel, t - profile->getFrom() + 1));
							}
						}
					}
				}
			}
		}
		if (retry == Inf) {
			// check break
			Int gap = executions.rbegin()->from - t;
			if (gap > 0) {
				//	- check break; max break duration 
				if (gap > breakDuration->getKey()) {
					if (progress == 0) { // delay the completion time
						retry = 0;
						ub = t + breakDuration->getKey();
					}
					else { //	push UB
						auto ite = executions.rbegin();
						Int right = max<Int>(breakDuration->getFrom() - 1, progress - (ite->to - ite->from) * ite->parallel);
						if (ite->parallel == 1) {
							if (breakDuration->getKey() > 0) { right = max<Int>(right, progress - (ite->from - t) / breakDuration->getKey()); }
						}
						else {
							if (breakDuration->getKey() <= ite->from - t) {
								right = max<Int>(right, progress - (ite->from - t) * ite->parallel / ((1 + breakDuration->getKey()) * ite->parallel - 1));
							}
							else { right = progress; }
						}
						if (right >= progress) { throw runtime_error("push LB"); }
						// insert new segment
						ub = t + (progress - right) * (1 + breakDuration->getKey());
						ubs.emplace_back(right, progress, ub, 1 + breakDuration->getKey());
						retry = right * 2 + 1;
					}
#if DEBUG > 2
					if (retry < Inf) {
						cout << "\tbreak: " << retry / 2 << " " << (retry % 2 ? " " : "-") << ub << endl;
					}
#endif				
				}
				//	- check break; resource availability
				else {
					for (LocalId rr = 0; rr < rmode->getNumRequiredResources(); rr++) {
						auto resource = resources[rmode->getRequiredResourceId(rr)];
						bool maximum = (rmode->getRequirements(rr, true, true) != 0);
						auto breakRequirement = rmode->getRequirements(rr, true, maximum, progress);
						// -- check whether resource is enough during the break; !!! there remain some redundant calculations !!!
						if (breakRequirement->getKey() == 0) { continue; }
						auto profile = resource->getProfile(t + 1);
						while (1) {
							if (breakRequirement->getKey() > profile->getKey()) {
								if (!test) solution.setCritical(activityId, resource, profile->getFrom(), profile->getTo());
								auto requirement = rmode->getRequirements(rr, false, maximum, progress + 1); // progress???
								Int retry_temp = progress * 2;
								if (requirement->getKey() > profile->getKey()) {
									retry_temp = min<Int>(retry_temp, requirement->getFrom() * 2 - 1);
								}
								if (retry_temp < retry) { retry = retry_temp; ub = Inf; }
								if (retry_temp == retry) { ub = min<Int>(ub, max<Int>(profile->getFrom() - 1, t)); }
								break;
							}
							if (profile->getTo() >= executions.rbegin()->from) { break; }
							profile = profile->getNext();
						}
					}
#if DEBUG > 2
					if (retry < Inf) {
						cout << "\tbreak resource: " << retry / 2 << " " << (retry % 2 ? " " : "-") << ub << endl;
					}
#endif
				} // end of checking resource availability for break
			} // end if (gap > 0)

			if (retry == Inf) {
				// consecutively processed				
				if (progress == duration) { // all done?
					executions.emplace_back(t, t, 0);
					reverse(executions.begin(), executions.end());
					break;
				}
				Int increment = (nextProgress - progress) / parallel;
				if (nextProgress != progress + parallel * increment) { throw runtime_error("consecutively processed."); }
				if (executions.rbegin()->from == t && executions.rbegin()->parallel == parallel) {
					executions.rbegin()->from -= increment;
				}
				else { executions.emplace_back(t- increment, t, parallel); }
				t -= increment;
				progress = nextProgress;

				//	- remove or update redundant segment of UBs; so ubs[].from <= progress
				while (!ubs.empty() && ubs.rbegin()->from < progress) {
					if (ubs.rbegin()->to < progress) { ubs.pop_back(); }
					else {
						ubs.rbegin()->value -= (progress - ubs.rbegin()->from) * ubs.rbegin()->grad;
						ubs.rbegin()->from = progress;
						break;
					}
				}
				continue; // next sub-activity to be assigned
			}
		}
		// retry
		if (ubs.empty() || ubs.rbegin()->from > progress) {
			ubs.emplace_back(progress, progress, t, 1 + breakDuration->getKey());
		}
		if (retry == 0) { // move up the completion time
			executions.clear();
			executions.emplace_back(ub, ub, 0);
		}
		else {
			if (retry % 2 == 0) {
				++ub;
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
					Int to = executions.rbegin()->to;
					Int profile = temp - progress + retry / 2;
					executions.pop_back();
					if (profile / parallel > 0) {
						executions.emplace_back(to - profile / parallel, to, parallel);
						to -= profile / parallel;
					}
					if (profile % parallel > 0) {
						if (executions.rbegin()->from == to && executions.rbegin()->parallel == profile % parallel) { // redundant?
							executions.rbegin()->from = to + 1;
						}
						else { executions.emplace_back(to - 1, to, profile % parallel); }
					}
					break;
				}
			}
		}
		progress = retry / 2;
		t = ub;
	} // end of main loop while

	// check lower bounds
	Id backtrack = UInf, minPosition = UInf;
	Id criticalTempConstraintId;
	num1 = activity->getNumInTempConstraints();
	num2 = activity->getNumInTempConstraintsForMode(modeLocalId);
	for (LocalId kk = 0; kk < num1 + num2; ++kk) {
		auto temporal = (kk < num1) ? activity->getInTempConstraint(kk) : activity->getInTempConstraintForMode(modeLocalId, kk - num1);
		Id predId = temporal->getPredId();
		if (solution.getExecutions(predId).empty()) { continue; } // successor has not yet scheduleed
		if (kk >= num1 && temporal->getPredModeLocalId() != solution.getModeLocalId(predId)) { continue; }
		Int delay = temporal->getDelay();
		bool violate = false;
		switch (temporal->getType()) {
		case TempConstraintType::SS:
			if ((violate = (executions.begin()->from < solution.getExecutions(predId).begin()->from+ delay)) && !test) {
				activities[predId]->updateUbStart(executions.begin()->from - delay);
			}
			break;
		case TempConstraintType::SC:
			if ((violate = (executions.rbegin()->to < solution.getExecutions(predId).begin()->from + delay)) && !test) {
				activities[predId]->updateUbStart(executions.rbegin()->to - delay);
			}
			break;
		case TempConstraintType::CS:
			if ((violate = (executions.begin()->from < solution.getExecutions(predId).rbegin()->to + delay)) && !test) {
				activities[predId]->updateUbFinish(executions.begin()->from - delay);
			}
			break;
		case TempConstraintType::CC:
			if ((violate = (executions.rbegin()->to < solution.getExecutions(predId).rbegin()->to + delay)) && !test) {
				activities[predId]->updateUbFinish(executions.rbegin()->to - delay);
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
				solution.setCritical(predId, activityId);
				if (solution.getPosition(predId) < minPosition) {
					minPosition = solution.getPosition(predId);
					backtrack = predId;
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
		cout << *activities[activityId] << " assigned (backward): " << executions << endl;
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
			if (++violationCount[criticalTempConstraintId] > maxViolationCount) {
				solution.addViolation(Violation::Type::TMP, criticalTempConstraintId, Inf);
				return Inf;
			}
			// backtrack
			activity->clearUbStart();
			activity->clearUbFinish();
			activityId = solution.getPrevActivityId(activityId);
			while (1) {
				if (activityId != backtrack) {
					if (getActivityBackward(activityId)) {
						if (solution.getExecutions(activityId).begin()->from <= activities[activityId]->getUbStart()) { activities[activityId]->clearUbStart(); }
						if (solution.getExecutions(activityId).rbegin()->to <= activities[activityId]->getUbFinish()) { activities[activityId]->clearUbFinish(); }
					}
					else {
						if (solution.getExecutions(activityId).begin()->from >= activities[activityId]->getLbStart()) { activities[activityId]->clearLbStart(); }
						if (solution.getExecutions(activityId).rbegin()->to >= activities[activityId]->getLbFinish()) { activities[activityId]->clearLbFinish(); }
					}
				}
				assign(activityId, modes[getActivityModeId(activityId, solution.getModeLocalId(activityId))], solution.getExecutions(activityId), -1);
				solution.getExecutions(activityId).clear();
				if (activityId == backtrack) { break; }
				activityId = solution.getPrevActivityId(activityId);
			}
			return 0;
		}
	}
}
