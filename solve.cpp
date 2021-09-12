#include "optseqimpl.h"
#include "network.h"
#include <fstream>

//static constexpr auto CONSECUTIVELY_ORDER = 1;
//static constexpr auto SS_PRECEDENCE = 1;
//static constexpr auto EDT = 1;
//static constexpr auto MTS = 0;


void
OptSeqImpl::solve() {
	// start clocking
	cputime.init();

	// preprocess
		//	 --- additional temporal constraints
	for (Id i = 2; i < getNumActivities(); i++) {
		if (activities[i]->getNumModes() == 0) {
			throw runtime_error(activities[i]->getName() + ": no mode defined.");
		}
		newTempConstraint(SourceId, i, TempConstraintType::CS);
		newTempConstraint(i, SinkId, TempConstraintType::CS);
	}
	//   - only for the case where there is no activity (but source and sink) defined
	if (getNumActivities() == 2) {
		newTempConstraint(SourceId, SinkId, TempConstraintType::CS);
	}

	//	--- state
	vector<Id> stateflag(getNumStates(), 0);
	for (Id i = 2; i < getNumActivities(); ++i) {
		for (LocalId mm = 0; mm < getActivityNumModes(i); ++mm) {
			Mode* mode = modes[getActivityModeId(i, mm)];
			for (LocalId ss = 0; ss < mode->getNumStates(); ++ss) {
				Id s = mode->getStateId(ss);
				if (stateflag[s] < i) {
					stateflag[s] = i;
					states[s]->addActivity(i);
				}
			}
		}
	}

	//	--- calculate the mininum & maximum duration of each mode & each activity, respectively
	for (Id m = 0; m < getNumModes(); m++) { modes[m]->calcMinMaxDurations(); }
	for (Id i = 0; i < getNumActivities(); i++) { activities[i]->calcMinMaxDurations(); }

	//	 --- calculate strongly connected components & make precedence table
	//		 - prepare the activity-on-node project network
	Network<Int, UInt> network(Inf);
	//Network network;
	network.resize(getNumActivities() * 2);
	for (Id i = 0; i < getNumActivities(); i++) {
		network.add_arc(i * 2, i * 2 + 1, activities[i]->getMinDuration());
		network.add_arc(i * 2 + 1, i * 2, -activities[i]->getMaxDuration());
	}
	vector<Id> tempConstraintIds[4];
	for (auto t = 0; t < 4; ++t) { tempConstraintIds[t].reserve(tempConstraints.size()); }
	for (Id k = 0; k < tempConstraints.size(); ++k) {
		//if (tempConstraints[k]->getSuccId() == SourceId) { continue; }
		tempConstraintIds[toUType(tempConstraints[k]->getType())].push_back(k);
	}
	vector<tuple<Id, Id, Int>> ignore;
	auto addArc = [&](Id from, Id to, Int length) {
		if (to / 2 == SourceId || to / 2 == SinkId) { ignore.emplace_back(from, to, length); }
		else { network.add_arc(from, to, length); }
	};
	for (auto t = 0; t < 4; ++t) {
		sort(tempConstraintIds[t].begin(), tempConstraintIds[t].end(),
			[this](auto const& k1, auto const& k2) {
				auto t1 = tempConstraints[k1];
				auto t2 = tempConstraints[k2];
				return make_tuple(t1->getPredId(), t1->getSuccId(), t1->getPredModeLocalId(), t1->getSuccModeLocalId())
					< make_tuple(t2->getPredId(), t2->getSuccId(), t2->getPredModeLocalId(), t2->getSuccModeLocalId());
			});
		Int delayInf = -Inf / static_cast<Int>(getNumActivities());
		for (Id k = 0; k < tempConstraintIds[t].size(); ++k) {
			auto temporal = tempConstraints[tempConstraintIds[t][k]];
			Id predId = temporal->getPredId();
			Id succId = temporal->getSuccId();
			UInt numPredModes = getActivityNumModes(predId);
			UInt numSuccModes = getActivityNumModes(succId);
			vector<vector<Int>> maxDelay(numPredModes);
			for (Id mm = 0; mm < numPredModes; ++mm) {
				maxDelay[mm].resize(numSuccModes, delayInf);
			}
			for (; k < tempConstraintIds[t].size(); ++k) {
				Int delay = temporal->getDelay();
				Id pmm2 = min<Id>(temporal->getPredModeLocalId(), numPredModes - 1), pmm1 = (pmm2 == numPredModes - 1) ? 0 : pmm2;
				Id smm2 = min<Id>(temporal->getSuccModeLocalId(), numSuccModes - 1), smm1 = (smm2 == numSuccModes - 1) ? 0 : smm2;
				for (Id pmm = pmm1; pmm <= pmm2; ++pmm) {
					for (Id smm = smm1; smm <= smm2; ++smm) {
						maxDelay[pmm][smm] = max<Int>(delay, maxDelay[pmm][smm]);
					}
				}
				if (k + 1 == tempConstraintIds[t].size()) { break; }
				auto temporal2 = tempConstraints[tempConstraintIds[t][k + 1]];
				if (predId != temporal2->getPredId() || succId != temporal2->getSuccId()) { break; }
			}
			Int minDelay = Inf;
			for (Id pmm = 0; pmm < numPredModes; ++pmm) {
				for (Id smm = 0; smm < numSuccModes; ++smm) {
					minDelay = min<Int>(minDelay, maxDelay[pmm][smm]);
				}
			}
			switch (t) {
			case toUType(TempConstraintType::SS): addArc(predId * 2, succId * 2, minDelay); break;
			case toUType(TempConstraintType::SC): addArc(predId * 2, succId * 2 + 1, minDelay); break;
			case toUType(TempConstraintType::CC): addArc(predId * 2 + 1, succId * 2 + 1, minDelay); break;
			case toUType(TempConstraintType::CS): addArc(predId * 2 + 1, succId * 2, minDelay); break;
			}
		}
	}
	//		 - calculate strongly connected components
	numSccs = network.strongly_connected_components();
	scc.resize(getNumActivities());
	vector<vector<Id>> sccActivities(numSccs);
	for (Id i = 0; i < getNumActivities(); ++i) {
		scc[i] = network.get_strongly_connected_component(i * 2);
		sccActivities[getActivityScc(i)].push_back(i);
	}
	//		 - calculate longest paths
	cout << "# computing all-pairs longest paths and strongly connected components ..." << flush;
	if (!network.all_pairs_longest_path()) {
		throw runtime_error("positive cycle detected in the temporal scheduling network --- no feasible schedule.\n");
	}
	//		 - calculate precedence relation on activities (w.r.t. activity list)
	precedenceTable.resize(getNumActivities());
	precedenceTable[SourceId].resize(getNumActivities(), true);
	precedenceTable[SourceId][SourceId] = false;
	precedenceTable[SinkId].resize(getNumActivities(), false);
	for (Id i = 2; i < getNumActivities(); ++i) {
		precedenceTable[i].resize(getNumActivities(), false);
		precedenceTable[i][SinkId] = true;
	}

	// add ignored arcs
	for (auto ite = ignore.begin(); ite != ignore.end(); ++ite) {
		network.add_arc(get<0>(*ite), get<1>(*ite), get<2>(*ite));
	}
	// identify backward activities
	vector<Id> backward;
	for (Id i = 0; i < getNumActivities(); ++i) {
		if (getActivityBackward(i)) { backward.push_back(i * 2); }
	}
	network.closure(backward, true);
	for (Id i : backward) {
		if ((i % 2) == 0 && (i / 2) != SourceId && (i / 2) != SinkId) { setActivityBackward(i / 2, true); }
	}
	vector<Id> nodesReachableToSource;
	nodesReachableToSource.push_back(SourceId*2);
	network.closure(nodesReachableToSource, false);
	vector<bool> reachableToSource(getNumActivities(), false);
	for (Id i : nodesReachableToSource) {
		if (i % 2 == 0) { reachableToSource[i/2] = true; }
	}

	auto setPrecedence = [this](Id i, Id j) {
		if (getActivityBackward(i) && getActivityBackward(j)) { precedenceTable[j][i] = true; }
		else { precedenceTable[i][j] = true; }
	};
	for (Id i = 2; i < getNumActivities(); ++i) {
		for (Id j = 2; j < getNumActivities(); ++j) {
			if (network.get_dist(i * 2, j * 2) >= 0 && network.get_dist(j * 2, i * 2) < 0) {
				setPrecedence(i, j);
			}
		}
	}
	//     - calculate precedence relation on sccs
	for (Id c1 = 0; c1 < numSccs; ++c1) {
		for (Id c2 = c1 + 1; c2 < numSccs; ++c2) {
			bool flag = false;
			if (reachableToSource[sccActivities[c1][0]] && !reachableToSource[sccActivities[c2][0]]) {
				flag = true;
			}
			for (Id ii1 = 0; !flag && ii1 < sccActivities[c1].size(); ++ii1) {
				for (Id ii2 = 0; !flag && ii2 < sccActivities[c2].size(); ++ii2) {
					if (network.get_dist(sccActivities[c1][ii1] * 2, sccActivities[c2][ii2] * 2) > -Inf) { flag = true; }
				}
			}
			if (flag) { // in this case, scc[c1] -> scc[c2]
				for (Id ii1 = 0; ii1 < sccActivities[c1].size(); ++ii1) {
					for (Id ii2 = 0; ii2 < sccActivities[c2].size(); ++ii2) {
						setPrecedence(sccActivities[c1][ii1], sccActivities[c2][ii2]);
					}
				}
			}
		}
	}
	cout << " done" << endl;
	cout << "#scc " << numSccs << endl;

#if 0
	for (Id i = 0; i < getNumActivities(); ++i) {
		cout << i << " " << getActivityName(i) << " " << scc[i] << endl;
	}
	cout << endl;
	for (Id i = 0; i < getNumActivities(); ++i) {
		for (Id j = 0; j < getNumActivities(); ++j) {
			if (precede(i, j)) {
				cout << i << (getActivityBackward(i) ? " *" : " ")  << getActivityName(i) << " -> " 
					<< j << (getActivityBackward(j) ? " *" : " ") << getActivityName(j) << endl;
			}
		}
	}
#endif

	// csp
	vector<UInt> numModes(getNumActivities());
	UInt totalNumModes = 0;
	for (Id i = 0; i < getNumActivities(); ++i) {
		numModes[i] = activities[i]->getNumModes();
		totalNumModes += numModes[i];
	}
	csp = unique_ptr<CSP>(CSP::makeCSP(numModes));
	for (Id k = 0; k < getNumNrrConstraints(); ++k) {
		if (nrrConstraints[k]->getWeight() < Inf) continue;
		CSP::Linear* linear = new CSP::Linear(csp.get());
		linear->setRhs(getNrrConstraintRhs(k));
		for (Id l = 0; l < getNrrConstraintNumTerms(k); ++l) {
			linear->addTerm(nrrConstraints[k]->getCoefficient(l), nrrConstraints[k]->getActivityId(l),
				nrrConstraints[k]->getModeLocalId(l));
		}
	}
	csp->preprocess();
	csp->setIterationLimit(cspIterationLimit = totalNumModes);

	// allocate memory
	for (UInt s = 0; s < 4; s++) { solutions[s].initialize(*this); }
	executionLists.resize(getNumActivities(), 0);
	resourceResidualLists.resize(getNumResources(), 0);
	violationCount.resize(getNumTempConstraints(), 0);
	tabu.init(getNumActivities() * 2 + numSccs, tabuTenure);

	//for (i = 0; i < getNumActivities(); ++i) { activities[i]->memoryInitialize(); }

	penaltyWeightStart.resize(getNumActivities());
	penaltyWeightFinish.resize(getNumActivities());
	penaltyWeightSoftNrr.resize(softNrrConstraintIds.size());

	// reconstruct the network
	network.resize(getNumActivities());
	for (Id i = 0; i < getNumActivities(); i++) {
		for (Id j = 0; j < getNumActivities(); j++) {
			if (precede(i, j)) { network.add_arc(i, j, 0); }
		}
	}
	vector<Int> scores(getNumActivities(), 0);

	for (UInt round = 0; ; ++round) {
		// initialize penalty weights
		for (Id i = 0; i < getNumActivities(); ++i) {
			penaltyWeightStart[i] = static_cast<double>(getActivityDueDateWeight(i, false));
			penaltyWeightFinish[i] = static_cast<double>(getActivityDueDateWeight(i, true));
		}
		for (UInt k = 0; k < softNrrConstraintIds.size(); ++k) {
			penaltyWeightSoftNrr[k] = static_cast<double>(getNrrConstraintWeight(softNrrConstraintIds[k]));
		}
		// generate an initial solution
		solutions[bestNeighbor].setObjective(Inf);
		UInt num_initsol;
		for (num_initsol = 1; num_initsol <= 5; ++num_initsol) {
			bool quit = false;
			if (round == 0 && initialSolutionFile != "") {
				ifstream input;
				input.open(initialSolutionFile.c_str());
				if (!input.is_open()) throw runtime_error(initialSolutionFile + ": no such file");
				cout << "# initial solution file: " << initialSolutionFile << endl;
				string activity, mode;
				for (UInt i = 0; i < getNumActivities(); ++i) {
					if (input.eof()) { break; }
					input >> activity >> mode >> ws;
					Id aid = getActivityId(activity), lid = 0;
					if (aid >= getNumActivities() || scores[aid] != 0 ||
						mode != "---" && (lid = activities[aid]->getModeLocalId(getModeId(mode))) >= activities[aid]->getNumModes()) {
						throw runtime_error(initialSolutionFile + ": invalid format (" + activity + " " + mode + ")");
					}
					//cout << "# " << aid << ":" << activity << " " << getModeId(mode) << "," << lid << ": " << mode << endl;
					scores[aid] = getNumActivities() - i;
					solutions[neighbor].setModeLocalId(aid, lid);
					quit = true;
				}
				input.close();
			}
			else {
				for (Id i = 0; i < getNumActivities(); i++) {
					solutions[neighbor].setModeLocalId(i, rand(0, activities[i]->getNumModes() - 1));
				}
				// randomness; tie break;
				for (Id i = 0; i < getNumActivities(); ++i) { scores[i] = -static_cast<int>(i); }
				for (Id i = 0; i < getNumActivities() - 1; ++i) { swap(scores[i], scores[rand(i, getNumActivities() - 1)]); }
#if 1
				// EDT (Earliest Due Date) rule; primary
				for (Id scc = 0; scc < numSccs; ++scc) {
					for (Id ii1 = 0; ii1 < sccActivities[scc].size(); ++ii1) {
						Id i = sccActivities[scc][ii1];
						Int minDueDate = min<Int>(activities[i]->getDueDate(Activity::DueDateType::START),
							activities[i]->getDueDate(Activity::DueDateType::COMPLETION) - activities[i]->getMinDuration());
						//activities[i]->getDueDate(Activity::DueDateType::COMPLETION));
						for (Id ii2 = 0; ii2 < sccActivities[scc].size(); ++ii2) {
							Id i2 = sccActivities[scc][ii2];
							if (precede(i, i2)) {
								minDueDate = min<Int>(minDueDate, min<Int>(activities[i2]->getDueDate(Activity::DueDateType::START),
									activities[i2]->getDueDate(Activity::DueDateType::COMPLETION) - activities[i2]->getMinDuration()));
								//activities[i2]->getDueDate(Activity::DueDateType::COMPLETION)));
							}
						}
						scores[i] -= min<Int>(minDueDate, static_cast<Int>(Inf / getNumActivities())) * getNumActivities();
					}
				}
#endif
#if 0
				// MTS (Most Total Successors) rule
				for (i = 0; i < getNumActivities(); i++) {
					for (Id j = 0; j < getNumActivities(); j++) {
						if (precede(i, j)) scores[i] += getNumActivities();
					}
				}
#endif
			}
			while (!makeModesFeasible(solutions[neighbor])) {
				cout << "# failed to find a feasible mode vector";
				if (initialSolutionFile != "" || cputime() >= timeLimit || cspIterationLimit >= maxCspIterationLimit) {
					throw runtime_error("");
				}
				cspIterationLimit *= 2;
				csp->setIterationLimit(cspIterationLimit);
				cout << ", doubling CSP iteration limit to " << cspIterationLimit << endl;
			}
			if (network.topological_order(scores, scc)) { throw runtime_error("topological order."); }
			solutions[neighbor].setPrevActivityId(SourceId, UInf);
			solutions[neighbor].setNextActivityId(SinkId, UInf);
			for (UInt ii = 0; ii < getNumActivities() - 1; ++ii) {
				Id i = network.get_topological_order(ii);
				Id j = network.get_topological_order(ii + 1);
				solutions[neighbor].setNextActivityId(i, j);
				solutions[neighbor].setPrevActivityId(j, i);
			}
			schedule(solutions[neighbor]);
			if (reportInterval <= 1) {
				cout << "initial solution " << num_initsol << ": " << solutions[neighbor].getObjective() << endl;
			}
			if (solutions[neighbor].getObjective() < solutions[bestNeighbor].getObjective()) {
				swap(neighbor, bestNeighbor);
			}
			if (quit) { break; }
		}
		updateCurrentSolution();
		//incumbent = current; 
		if (search()) { break; }
	}
}
