#include "optseq.h"
#include "lexer.h"
#include <algorithm>
#include <strstream>

using token = Lexer<Int>::token;

// ---------- MyInt ----------
struct MyInt{
	MyInt(Int i): key(i){}
	Int key;
	friend ostream& operator<<(ostream&, const MyInt&);
};
ostream& 
operator<<(ostream &os, MyInt const &i){
	if (i.key == Inf) { return os << "inf"; }
	else { return os << i.key; }
}

// ==================== input ====================
void 
inputDataMode(OptSeq& optseq, Lexer<Int>& lexer, string& name) {
	Id id = optseq.getModeId(lexer.text());
	if (id < optseq.getNumModes()) { lexer.error(": already defined."); }
	lexer.next("duration"); lexer.next(token::INTEGER);
	id = optseq.newMode(name, lexer.i());
	while (1) {
		lexer.next();
		if (lexer.text() == "break" || lexer.text() == "parallel") {
			bool parallel = (lexer.text() == "parallel");
			Int maxvalue(Inf);
			lexer.next("interval");
			lexer.next(token::INTEGER); Int from = lexer.i();
			lexer.next(token::INTEGER); Int to = lexer.i();
			lexer.next();
			if (lexer.text() == "max") {
				lexer.next(token::INTEGER);
				maxvalue = lexer.i();
			}
			else { lexer.push(); }
			if (parallel) { optseq.setModeMaxNumsParallel(id, maxvalue, from, to); }
			else { optseq.setModeMaxBreakDurations(id, maxvalue, from, to); }
		}
		else {
			Id rid = optseq.getResourceId(lexer.text());
			if (rid < optseq.getNumResources()) {
				// resource
				lexer.next();
				bool maximum = (lexer.text() == "max") ? true : (lexer.push(), false);
				while (1) {
					lexer.next();
					if (lexer.text() != "interval") {
						lexer.push();
						break;
					}
					lexer.next();
					bool forBreak = (lexer.text() == "break") ? true : (lexer.push(), false);
					lexer.next(token::INTEGER); Int from = lexer.i(); if (!forBreak) { ++from; }
					lexer.next(token::INTEGER); Int to = lexer.i();
					lexer.next("requirement");
					lexer.next(token::INTEGER);
					optseq.setModeRequirements(id, rid, lexer.i(), from, to, forBreak, maximum);
				}
			}
			else {
				Id sid = optseq.getStateId(lexer.text());
				if (sid < optseq.getNumStates()) {
					// state
					lexer.next("from");
					lexer.next(token::INTEGER);
					Int from = lexer.i();
					lexer.next("to");
					lexer.next(token::INTEGER);
					Int to = lexer.i();
					optseq.setModeState(id, sid, from, to);
				}
				else break;
			}
		}
	}
	lexer.push();
}

void
inputData(OptSeq& optseq, istream& is = cin) {
	Lexer<Int> lexer(is, Inf);
	try {
		while (lexer.next() != token::nil) {
			// ---------- resource
			if (lexer.text() == "resource") {
				lexer.next();
				Id id(optseq.getResourceId(lexer.text()));
				if (id < optseq.getNumResources()) { lexer.error(": already defined."); }
				id = optseq.newResource(lexer.text());
				while (1) {
					lexer.next();
					if (lexer.text() != "interval") { break; }
					lexer.next(token::INTEGER); Int from = lexer.i(); ++from;
					lexer.next(token::INTEGER); Int to = lexer.i();
					lexer.next("capacity");
					if (lexer.next() == token::INTEGER) {
						optseq.setResourceCapacities(id, lexer.i(), from, to);
					}
					else {
						lexer.push(); lexer.next("add"); lexer.next(token::INTEGER);
						optseq.addResourceCapacities(id, lexer.i(), from, to);
					}
				}
				lexer.push();
			}
			// ---------- resource
			else if (lexer.text() == "state") {
				lexer.next();
				Id id = optseq.getStateId(lexer.text());
				if (id < optseq.getNumStates()) { lexer.error(": already defined."); }
				id = optseq.newState(lexer.text());
				while (1) {
					lexer.next();
					if (lexer.text() != "time") break;
					lexer.next(token::INTEGER); Int time = lexer.i();
					lexer.next("value");
					lexer.next(token::INTEGER); Int value = lexer.i();
					optseq.setStateValue(id, value, time);
				}
				lexer.push();
			}
			// ---------- mode
			else if (lexer.text() == "mode") {
				lexer.next();
				string name(lexer.text());
				inputDataMode(optseq, lexer, name);
			}
			// ---------- activity
			else if (lexer.text() == "activity") {
				lexer.next();
				Id id = optseq.getActivityId(lexer.text());
				if (id < optseq.getNumActivities()) {
					if (lexer.text() != "sink") { lexer.error(": already defined."); }
				}
				else { id = optseq.newActivity(lexer.text()); }
				lexer.next();
				if (lexer.text() == "backward") { optseq.setActivityBackward(id, true); }
				else { lexer.push(); }
				do {
					lexer.next();
					if (lexer.text() != "duedate") {
						lexer.push();
						break;
					}
					lexer.next();
					bool finish = true;
					if (lexer.text() == "start") { finish = false; }
					else { lexer.push(); }
					lexer.next(token::INTEGER);
					optseq.setActivityDueDate(id, lexer.i(), finish);
					lexer.next();
					if (lexer.text() == "weight") {
						lexer.next(token::INTEGER);
						optseq.setActivityDueDateWeight(id, lexer.i(), finish);
					}
					else { lexer.push(); }
					lexer.next();
					if (lexer.text() == "quad") { optseq.setActivityDueDateQuad(id, true, finish); }
					else { lexer.push(); }
				} while (1);

				lexer.next();
				if (lexer.text() == "mode") {
					string name = "mode_" + optseq.getActivityName(id);
					inputDataMode(optseq, lexer, name);
					optseq.addActivityMode(id, optseq.getModeId(name));
				}
				else {
					if (lexer.text() == "autoselect") {
						lexer.next();
						if (lexer.text() == "slow") { optseq.setActivityAutoSelect(id, 2); }
						else if (lexer.text() == "fast") { optseq.setActivityAutoSelect(id, 3); }
						else { lexer.push(); optseq.setActivityAutoSelect(id, 1); }
					}
					else { lexer.push(); }
					while (1) {
						lexer.next();
						Id mid = optseq.getModeId(lexer.text());
						if (mid < optseq.getNumModes()) { optseq.addActivityMode(id, mid); }
						else {
							lexer.push();
							break;
						}
					}
				}
			}
			// ---------- temporal constraint
			else if (lexer.text() == "temporal") {
				lexer.next();
				if (optseq.getActivityId(lexer.text()) >= optseq.getNumActivities()) { // name
					lexer.next();
				}
				Id pid = optseq.getActivityId(lexer.text()), pmid = Inf;
				if (pid >= optseq.getNumActivities()) { lexer.error(": not defined yet."); }
				lexer.next();
				if (lexer.text() == "mode") {
					lexer.next();
					pmid = optseq.getModeId(lexer.text());
					if (pmid >= optseq.getNumModes()) { lexer.error(": not defined yet."); }
					lexer.next();
				}
				Id sid = optseq.getActivityId(lexer.text()), smid = Inf;
				if (sid >= optseq.getNumActivities()) { lexer.error(": not defined yet."); }
				lexer.next();
				if (lexer.text() == "mode") {
					lexer.next();
					smid = optseq.getModeId(lexer.text());
					if (smid >= optseq.getNumModes()) { lexer.error(": not defined yet."); }
					lexer.next();
				}
				TempConstraintType type(TempConstraintType::CS);
				if (lexer.text() == "type") {
					lexer.next(token::STRING);
					if (lexer.text() == "SS") { type = TempConstraintType::SS; }
					else if (lexer.text() == "SC") { type = TempConstraintType::SC; }
					else if (lexer.text() == "CS") { type = TempConstraintType::CS; }
					else if (lexer.text() == "CC") { type = TempConstraintType::CC; }
					else { lexer.error(""); }
					lexer.next();
				}
				Int delay(0);
				if (lexer.text() == "delay") {
					lexer.next(token::INTEGER);
					delay = lexer.i();
					lexer.next();
				}
				optseq.newTempConstraint(pid, sid, type, delay, pmid, smid);
				lexer.push();
			}
			// ---------- nonrenewable resource constraint
			else if (lexer.text() == "nonrenewable") {
				Int weight = Inf;
				lexer.next();
				if (lexer.text() == "weight") {
					lexer.next(token::INTEGER);
					weight = lexer.i();
				}
				else { lexer.push(); }
				lexer.next(); 
				if (lexer.tok() == token::INTEGER) { // noname
					lexer.push();
				}
				Id id = optseq.newNrrConstraint(weight);
				while (1) {
					lexer.next();
					if (lexer.text() == "<") {
						lexer.next('=');
						lexer.next(token::INTEGER);
						optseq.setNrrConstraintRhs(id, lexer.i());
						break;
					}
					lexer.push(); lexer.next(token::INTEGER);
					Int coefficient = lexer.i();
					lexer.next('('); lexer.next();
					Id aid = optseq.getActivityId(lexer.text());
					if (aid >= optseq.getNumActivities()) { lexer.error(": not defined yet."); }
					lexer.next(','); lexer.next();
					Id mid = optseq.getModeId(lexer.text());
					if (mid >= optseq.getNumModes()) { lexer.error(": not defined yet."); }
					lexer.next(')');
					optseq.addNrrConstraintTerm(id, coefficient, aid, mid);
				}
			}
			// ---------- mode dependence
			else if (lexer.text() == "dependence") {
				lexer.next();
				Id id1 = optseq.getActivityId(lexer.text());
				if (id1 >= optseq.getNumActivities()) { lexer.error(": not defined yet."); }
				lexer.next();
				Id id2 = optseq.getActivityId(lexer.text());
				if (id2 >= optseq.getNumActivities()) { lexer.error(": not defined yet."); }
				optseq.setActivityDependence(id1, id2);
			}
			// ---------- end
			else if (lexer.text() == "end") { break; }
			// ---------- error
			else lexer.error("");
		}
	}
	catch (runtime_error error) {
		strstream ss; // should be replaced with stringstream if it is supported
		ss << "line " << lexer.lineno() << ": " << error.what() << '\0';
		string message(ss.str());
		ss.freeze(false);
		throw runtime_error(message);
	}
}

// ==================== output ====================
void
outputData(OptSeq& optseq, ostream& os = cout) {
	os << "#---------- resource ----------" << endl;
	for (Id r = 0; r < optseq.getNumResources(); ++r) {
		os << "# id = " << r << endl;
		os << "resource " << optseq.getResourceName(r) << endl;

		for (auto ptr = optseq.getResourceCapacities(r); ptr->getFrom() < Inf; ptr = ptr->getNext()) {
			if (ptr->getKey() == 0) continue;
			os << "\tinterval " << ptr->getFrom() - 1 << " " << MyInt(ptr->getTo()) << "\tcapacity " << MyInt(ptr->getKey()) << endl;
		}
		os << endl;
	}
	os << endl;
	os << "#---------- state ----------" << endl;
	for (Id s = 0; s < optseq.getNumStates(); ++s) {
		os << "# id = " << s << endl;
		os << "state " << optseq.getStateName(s) << endl;

		const vector<pair<Int, Int> >& state = optseq.getStateValues(s);
		for (unsigned int c = 0; c < state.size(); ++c) {
			os << "\ttime " << state[c].second << "\tvalue " << state[c].first << endl;
		}
		os << endl;
	}
	os << endl;
	os << "#---------- mode ----------" << endl;
	for (Id m = 1; m < optseq.getNumModes(); ++m) { // except for dummy
		Int duration = optseq.getModeDuration(m);
		os << "# id = " << m << endl;
		os << "mode " << optseq.getModeName(m)
			<< "\tduration " << duration << endl;

		for (auto ptr = optseq.getModeMaxBreakDurations(m); ptr->getFrom() <= duration; ptr = ptr->getNext()) {
			//			if (ptr->getKey() == 0) continue;
			os << "\tbreak interval " << ptr->getFrom() << " " << ptr->getTo();
			os << " max " << MyInt(ptr->getKey());
			os << endl;
		}
		//for (auto ptr = optseq.getModeMaxNumsParallel(m); ptr; ptr = ptr->getNext()) {
		for (auto ptr = optseq.getModeMaxNumsParallel(m); ptr->getFrom() <= duration; ptr = ptr->getNext()) {
			if (ptr->getKey() == 1) { continue; }
			os << "\tparallel interval " << ptr->getFrom() << " " << ptr->getTo();
			os << " max " << MyInt(ptr->getKey());
			os << endl;
		}
		for (LocalId rr = 0; rr < optseq.getModeNumRequiredResources(m); ++rr) {
			os << "\t" << optseq.getResourceName(optseq.getModeRequiredResourceId(m, rr));
			bool maximum = (optseq.getModeRequirements(m, rr, false, true) != 0); // "false" of the 3rd argument could be true
			if (maximum) { os << "\tmax"; }
			os << endl;
			for (Int forBreak = 0; forBreak <= 1; ++forBreak) {
				for (const OptSeq::Interval* ptr = optseq.getModeRequirements(m, rr, forBreak != 0, maximum); ptr != 0; ptr = ptr->getNext()) {
					if (ptr->getKey() == 0) { continue; }
					os << "\t\tinterval " << (forBreak ? "break " : "") << (forBreak ? ptr->getFrom() : ptr->getFrom() - 1)
						<< " " << ptr->getTo() << "\trequirement " << MyInt(ptr->getKey());
					os << endl;
				}
			}
			os << endl;
		}
		for (LocalId ss = 0; ss < optseq.getModeNumStates(m); ++ss) {
			const vector<Int>& table = optseq.getModeState(m, ss);
			os << "\t" << optseq.getStateName(optseq.getModeStateId(m, ss)) << endl;
			for (UInt k = 0; k < table.size(); ++k) {
				if (table[k] >= 0) {
					os << "\t\tfrom " << k << "\tto " << table[k] << endl;
				}
			}
			os << endl;
		}
	}
	os << endl;

	os << endl;
	os << "#---------- activity ----------" << endl;
	for (Id i = 0; i < optseq.getNumActivities(); ++i) {
		// do not print source
		if (optseq.getActivityName(i) == "source") { continue; }

		// print the due date of sink, if it is not Inf 
		if (optseq.getActivityName(i) == "sink" &&
			optseq.getActivityDueDate(i, 0) == Inf &&
			optseq.getActivityDueDate(i, 1) == Inf) {
			continue;
		}
	
		os << "# id = " << i << endl;
		os << "activity " << optseq.getActivityName(i) << endl;
		if (optseq.getActivityBackward(i)) { cout << "\tbackward" << endl; }
		if (optseq.getActivityDueDate(i, 0) != Inf) {
			os << "\tduedate start " << optseq.getActivityDueDate(i, 0)
				<< "\tweight " << optseq.getActivityDueDateWeight(i, 0);
			if (optseq.getActivityDueDateQuad(i, 0)) { os << "\tquad"; }
			os << endl;
		}
		if (optseq.getActivityDueDate(i, 1) != Inf) {
			os << "\tduedate " << optseq.getActivityDueDate(i, 1)
				<< "\tweight " << optseq.getActivityDueDateWeight(i, 1);
			if (optseq.getActivityDueDateQuad(i, 1)) { os << "\tquad"; }
			os << endl;
		}

		if (optseq.getActivityAutoSelect(i)) { cout << "\tautoselect" << endl; }
		if (optseq.getActivityAutoSelect(i) == 2) { cout << "\tslow" << endl; }
		else if (optseq.getActivityAutoSelect(i) == 3) { cout << "\tfast" << endl; }
		for (LocalId mm = 0; mm < optseq.getActivityNumModes(i); ++mm) {
			os << "\t" << optseq.getModeName(optseq.getActivityModeId(i, mm))
				<< "\t#	 id = " << optseq.getActivityModeId(i, mm) << endl;
		}
		os << endl;
	}

	os << endl;
	os << "#---------- temporal constraint ----------" << endl;
	for (Id k = 0; k < optseq.getNumTempConstraints(); ++k) {
		os << "# id = " << k << endl;
		os << "temporal ";
		Id pmid(optseq.getTempConstraintPredModeId(k));
		os << optseq.getActivityName(optseq.getTempConstraintPredId(k))
			<< (pmid != Inf ? " mode " + optseq.getModeName(pmid) : "") << " ";
		Id smid(optseq.getTempConstraintSuccModeId(k));
		os << optseq.getActivityName(optseq.getTempConstraintSuccId(k))
			<< (smid != Inf ? " mode " + optseq.getModeName(smid)  : "") << " ";
		TempConstraintType type(optseq.getTempConstraintType(k));
		switch (type) {
		case TempConstraintType::SS: os << " type SS"; break;
		case TempConstraintType::SC: os << " type SC"; break;
		case TempConstraintType::CS: os << " type CS"; break;
		case TempConstraintType::CC: os << " type CC"; break;
		}
		Int delay(optseq.getTempConstraintDelay(k));
		if (delay) os << " delay " << delay;
		os << endl << endl;
	}
	os << endl;
	os << "#---------- nonrenewable resource constraint ----------" << endl;
	for (Id k = 0; k < optseq.getNumNrrConstraints(); ++k) {
		os << "# id = " << k << endl;
		os << "nonrenewable";
		if (optseq.getNrrConstraintWeight(k) < Inf) os << " weight " << optseq.getNrrConstraintWeight(k);
		os << endl;
		for (Id l = 0; l < optseq.getNrrConstraintNumTerms(k); ++l) {
			os << "\t" << optseq.getNrrConstraintCoefficient(k, l)
				<< " (" << optseq.getActivityName(optseq.getNrrConstraintActivityId(k, l))
				<< "," << optseq.getModeName(optseq.getNrrConstraintModeId(k, l)) << ") " << endl;
		}
		os << "\t <= " << optseq.getNrrConstraintRhs(k) << endl;
	}

	os << endl;
	os << "#---------- mode dependence ----------" << endl;
	for (Id id = 0; id < optseq.getNumActivities(); ++id) {
		if (optseq.getActivityNumDependences(id) == 0) continue;
		for (LocalId lid = 0; lid < optseq.getActivityNumDependences(id); ++lid) {
			cout << "dependence " << optseq.getActivityName(id) << " "
				<< optseq.getActivityName(optseq.getActivityDependence(id, lid)) << endl;
		}
	}
}