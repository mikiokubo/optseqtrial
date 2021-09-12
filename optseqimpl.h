#include <iostream>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include "optseq.h"
#include "cputime.h"
#include "random.h"
#include "tabulist.h"
#include "csp.h"

// enumerator
template<typename E>
constexpr auto toUType(E enumerator) noexcept {
	return static_cast<underlying_type_t<E>>(enumerator);
}

// ==================== OptSeqImpl ====================
class OptSeqImpl: public OptSeq {
	// parameters: default values
	static const Int defaultRandomSeed;
	static const double defaultTimeLimit;
	static const UInt defaultIterationLimit;
	static const UInt defaultNeighborhoodSize;
	static const UInt defaultReportInterval;
	static const UInt defaultMaxNumBacktracks;
	static const UInt defaultMaxViolationCount;
	static const UInt defaultTabuTenure;
	static const UInt defaultWeightControl;
	//
	static const Int bigM;
	static const double maxWeightMultiplier;
	static const Int maxStateValue;
	static const UInt maxCspIterationLimit;
	// infinity
	static const UInt UInf;
	static const double DInf;


	// ---------- classes ----------
public:
	// ==================== IntervalImpl ====================
	class IntervalImpl : public OptSeq::Interval {
	public:
		IntervalImpl() {}
		IntervalImpl(Int i1, Int i2, Int k, IntervalImpl* p = nullptr, IntervalImpl* n = nullptr)
			: from(i1), to(i2), key(k), prev(p), next(n) {
			if (prev) { prev->next = this; }
			if (next) { next->prev = this; }
		}
		~IntervalImpl() {
			if (prev) { prev->next = next; }
			if (next) { next->prev = prev; }
		}
		IntervalImpl(const IntervalImpl&) = delete;
		IntervalImpl& operator=(const IntervalImpl&) = delete;

		Int getFrom() const { return from; }
		Int getTo() const { return to; }
		Int getKey() const { return key; }
		const IntervalImpl* getPrev() const { return prev; }
		const IntervalImpl* getNext() const { return next; }
		IntervalImpl* getPrev() { return prev; }
		IntervalImpl* getNext() { return next; }
		void setFrom(Int t) { from = t; }
		void setTo(Int t) { to = t; }
		void setKey(Int k) { key = k; }

		IntervalImpl* split(Int time);
	private:
		Int from = 0, to = 0, key = 0;
		IntervalImpl* prev = nullptr, * next = nullptr;

		// for memory management
	public:
		static void* operator new(size_t s) {
			if (!first) {
				//cout << "get memory: " << chunk_size << endl;
				Int k = 0;
				do {
					auto newone = ::new IntervalImpl;
					newone->next = first;
					first = newone;
				} while (++k < max<Int>(0, chunk_size - 1));
			}
			IntervalImpl* newone = first;
  		first = first->next;
			return newone;
		}
		static void operator delete(void* dead, size_t s) {
			if (!dead) return;
			auto p = static_cast<IntervalImpl*>(dead);
			p->next = first;
			first = p;
		}
		static void clearMemories() {
			while (first){
				auto next = first->next;
				::delete first;
				first = next;
			}
		}
	private:
		static constexpr Int chunk_size = 128; // the size of chunk to be allocated next
		static IntervalImpl* first;
	};


	// ==================== DiscreteFunction ====================
	class DiscreteFunction {
	public:
		DiscreteFunction(Int from, Int to, Int default_value) :
			begin(new IntervalImpl(from, to, default_value, 0, 0)), pos(begin) {
			end = new IntervalImpl(to + 1, to + 1, default_value, begin, 0); // sentinel
		}
		~DiscreteFunction() {
			while (begin->getNext()) delete begin->getNext();
			delete begin;
		}
		DiscreteFunction(const DiscreteFunction&) = delete;
		DiscreteFunction& operator=(const DiscreteFunction&) = delete;

		const IntervalImpl* resetPos() const { pos = begin; return pos; }

		const IntervalImpl* getInterval() const { return pos; }
		const IntervalImpl* getInterval(Int time) const { return find(time); }

		IntervalImpl* getInterval() { return pos; }
		IntervalImpl* getInterval(Int time) { return find(time); }

		void deleteInterval(IntervalImpl* intervalImpl) {
			if (intervalImpl == begin) throw runtime_error("DiscreteFunction::deleteInterval.");
			intervalImpl->getPrev()->setTo(intervalImpl->getTo());
			if (intervalImpl == pos) pos = pos->getPrev();
			delete intervalImpl;
		}
		IntervalImpl* splitInterval(Int time);
		void setKey(Int key, Int from, Int to);
		void addKey(Int delta, Int from, Int to);

	private:
		IntervalImpl* begin, * end;
		mutable IntervalImpl* pos;
		IntervalImpl* find(Int time) const;
	};

	// ==================== Resource ====================
	class Mode;
	class Resource {
	public:
		Resource(const string& str, Id i) : name(str), id(i), capacities(1, Inf, 0), profile(1, Inf, 0){}
		~Resource() {
			clear();
		}
		Resource(const Resource&) = delete;
		Resource& operator=(const Resource&) = delete;

		Id getId() const { return id; }
		const string& getName() const { return name; }
		void setCapacities(Int value, Int from, Int to) { capacities.setKey(value, from, to); }
		void addCapacities(Int value, Int from, Int to) { capacities.addKey(value, from, to); }
		const IntervalImpl* getCapacities(Int time) const { return capacities.getInterval(time); }
	private:
		const string name;
		Id id;
		DiscreteFunction capacities;

		// for construct
	public:
		void clear();
		void consume(Int from, Int to, Int amount, Id aid);
		const IntervalImpl* getProfileBegin() { return profile.resetPos(); }
		const IntervalImpl* getProfile() { return profile.getInterval(); }
		const IntervalImpl* getProfile(Int time) { return profile.getInterval(time); }

		const IntervalImpl* adjustActivityPtr(Int time) {
			while (activityPtr->getFrom() > time) activityPtr = activityPtr->getPrev();
			while (activityPtr->getTo() < time) activityPtr = activityPtr->getNext();
			return activityPtr;
		}
		Id getActivityId(Int cid) { return cells[cid].activityId; }
		Int getNextActivityCell(Int cid) { return cells[cid].next; }

	private:
		DiscreteFunction profile;
		Int maxtime = Inf;

		struct Cell {
			Cell(Id id, UInt n) : activityId(id), next(n) {}
			Id activityId;
			Int next;
		};
		vector<Cell> cells;
		IntervalImpl* activities = nullptr, * activityPtr = nullptr;
	};


	// ==================== State ====================
	class State {
	public:
		State(const string& str, Id s) : name(str), id(s), values(0, Inf, 0), maxtime(0) {
			init.emplace_back(0, 0);
		}
		~State() {
			clear();
		}
		State(const State&) = delete;
		State& operator=(const State&) = delete;

		Id getId() const { return id; }
		const string& getName() const { return name; }
		void setValue(Int value, Int time) {
			Int to = values.getInterval(time)->getTo();
			values.setKey(static_cast<Int>(init.size()), time, to);
			init.emplace_back(value, time);
		}
		const vector<pair<Int, Int> >& getValues() const { return init; }
		Int getValue(const Interval* interval) const {
			Int index = interval->getKey();
			return (index >= 0) ? init[index].first : changed[-index - 1].second;
		}
		Id getActivityAssigned(const Interval* interval) const {
			Int index = interval->getKey();
			if (index >= 0) return UInf;
			return changed[-index - 1].first;
		}
		const IntervalImpl* getInterval(Int time) const { return values.getInterval(time); }
		//
		void addActivity(Id id) { activities.push_back(id); }
		const vector<Id>& getActivities() { return activities; }
	private:
		const string name;
		Id id;
		vector<Id> activities;

		vector<pair<Int, Int> > init; // list of (value,time)
		vector<pair<Id, Int> > changed; // list of (activity Id, to)
		DiscreteFunction values;
		// a key represents	 an index of "init" (scheduled in advance), if key >= 0 
		//										the negative of an index of "changed" minus 1, otherwise

		// for construct
	public:
		void clear();
		void change(Int time, Id activityId, Int to);
		void undo(Int time);

	private:
		Int maxtime;
	};




	// ==================== Mode ====================
	class Mode {
	public:
		Mode(const string& str, Id m, Int d) :
			name(str), id(m), duration(d), maxBreakDurations(0, duration, 0), maxNumsParallel(1, duration, 1) {}
		~Mode() {
			delete reverse;
			for (LocalId rr = 0; rr < requirements.size(); ++rr) {
				delete requirements[rr][0];
				delete requirements[rr][1];
				delete requirements[rr];
			}
			for (LocalId ss = 0; ss < states.size(); ++ss) {
				delete stateTables[ss];
			}
		}
		Mode(const Mode&) = delete;
		Mode& operator=(const Mode&) = delete;

		Id getId() const { return id; }
		const string& getName() const { return name; }

		Int getDuration() const { return duration; }
		void setMaxBreakDurations(Int maxduration, Int from, Int to) {
			to = min(to, duration);
			if (from < 0 || from > to || maxduration < 0) { throw runtime_error("invalid arguments (setMaxBreakDurations)."); }
			maxBreakDurations.setKey(maxduration, from, to);
		}
		void setMaxNumsParallel(Int num, Int from, Int to) {
			to = min(to, duration);
			if (from < 1 || from > to || num < 1) { throw runtime_error("invalid arguments (setMaxNumsParallel)."); }
			maxNumsParallel.setKey(num, from, to);
		}
		const IntervalImpl* getMaxBreakDurations() const { return maxBreakDurations.getInterval(); }
		const IntervalImpl* getMaxBreakDurations(Int time) const { return maxBreakDurations.getInterval(time); }
		const IntervalImpl* getMaxNumsParallel() const { return maxNumsParallel.getInterval(); }
		const IntervalImpl* getMaxNumsParallel(Int time) const { return maxNumsParallel.getInterval(time); }

		LocalId setRequirements(const Resource*, Int value, Int from, Int to, bool forBreak = false, bool maximum = false);
		UInt getNumRequiredResources() const { return static_cast<UInt>(requiredResources.size()); }
		Id getRequiredResourceId(UInt lid) const { return requiredResources[lid]->getId(); }
		const IntervalImpl* getRequirements(LocalId lid, bool forBreak = false, bool m = false) const {
			if (maximum[lid] == m) return requirements[lid][forBreak]->getInterval();
			else return 0;
		}
		const IntervalImpl* getRequirements(LocalId lid, bool forBreak, bool m, Int time) const {
			if (maximum[lid] == m) return requirements[lid][forBreak]->getInterval(time);
			else return 0;
		}
		//
		LocalId updateState(State*, Int from, Int to);
		LocalId setState(State* state, Int from, Int to) { return updateState(state, from, to); }
		UInt getNumStates() const { return static_cast<UInt>(states.size()); }
		Id getStateId(LocalId lid) const { return states[lid]->getId(); }
		LocalId getStateLocalId(Id sid) const {
			map<Id, LocalId>::const_iterator ite = stateDic.find(sid);
			return (ite != stateDic.end()) ? ite->second : getNumStates();
		}
		const vector<Int>& getState(LocalId lid) const { return *stateTables[lid]; }
		Int getStateValue(LocalId lid, Int from) const {
			if (from < 0) throw runtime_error("getStateValue");
			return (static_cast<UInt>(from) >= stateTables[lid]->size()) ? -1 : (*stateTables[lid])[from];
		}
		//
		void calcMinMaxDurations();
		Int getMinDuration() const { return minDuration; }
		Int getMaxDuration() const { return maxDuration; }

		Mode* getReverseMode();
	private:
		const string name;
		Id id;
		Int duration, minDuration = 0, maxDuration = Inf;
		map<Id, LocalId> rscDic; // resource id --> local id
		vector<const Resource*> requiredResources;
		vector<DiscreteFunction**> requirements;
		vector<bool> maximum;

		map<Id, LocalId> stateDic; // state id --> local id
		vector<const State*> states;
		vector<vector<Int>*> stateTables;

		DiscreteFunction maxBreakDurations, maxNumsParallel;

		Mode* reverse = nullptr;
	};


	// ==================== Activity ====================
	class TempConstraint;
	class Activity {
	public:
		enum class DueDateType { START = 0, COMPLETION = 1 };
		enum class PenaltyType { LINEAR, QUADRATIC };
		enum class ModeSelect { RIGID, FIRST, BEST };

		Activity(const string& str, Id i) : name(str), id(i) {}
		~Activity() {}
		Activity(const Activity&) = delete;
		Activity& operator=(const Activity&) = delete;

		Id getId() const { return id; }
		const string& getName() const { return name; }

		LocalId addMode(Mode* mode) {
			if ((id == SourceId || id == SinkId) && mode->getName() != "dummy") {
				throw runtime_error(getName() + ": no user-specified mode permitted.");
			}
			Id mid = mode->getId();
			if (getModeLocalId(mid) < getNumModes()) {
				throw runtime_error("mode " + mode->getName() + ": already added to " + getName() + ".");
			}
			UInt lid = static_cast<UInt>(dic.size()); dic[mid] = lid;
			modes.push_back(mode);
			inTmpConstraintsForMode.resize(modes.size());
			outTmpConstraintsForMode.resize(modes.size());
			return lid;
		}
		UInt getNumModes() const { return static_cast<UInt>(modes.size()); }
		Id getModeId(LocalId lid) const { return modes[lid]->getId(); }
		LocalId getModeLocalId(Id mid) const {
			map<Id, LocalId>::const_iterator ite = dic.find(mid);
			return (ite != dic.end()) ? ite->second : getNumModes();
		}

		void setDueDate(Int d, DueDateType type) { duedate[toUType(type)] = d; }
		Int getDueDate(DueDateType type) const { return duedate[toUType(type)]; }
		void setTardinessPenalty(Int weight, DueDateType type, PenaltyType ptype = PenaltyType::LINEAR) {
			duedateWeight[toUType(type)] = weight;
			penaltyType[toUType(type)] = ptype;
		}
		pair<Int, PenaltyType> getTardinessPenalty(DueDateType type) const {
			pair<Int, PenaltyType> tp(duedateWeight[toUType(type)], penaltyType[toUType(type)]);
			return pair<Int, PenaltyType>(duedateWeight[toUType(type)], penaltyType[toUType(type)]);
		}

		void setBackward(bool b) { backward = b; }
		bool getBackward() const { return backward; }

		void setAutoSelect(Int i) { autoSelect = i; }
		Int getAutoSelect() const { return autoSelect; }

		void setDependence(Id id) { dependence.push_back(id); }
		UInt getNumDependences() { return static_cast<UInt>(dependence.size()); }
		Id getDependence(LocalId lid) { return dependence[lid]; }

		void calcMinMaxDurations() {
			minDuration = Inf; maxDuration = 0;
			for (LocalId mm = 0; mm < getNumModes(); mm++) {
				minDuration = min<Int>(minDuration, modes[mm]->getMinDuration());
				maxDuration = max<Int>(maxDuration, modes[mm]->getMaxDuration());
			}
		}
		Int getMinDuration() const { return minDuration; }
		Int getMaxDuration() const { return maxDuration; }

		void setInTempConstraint(TempConstraint* temp) { inTempConstraint.push_back(temp); }
		void setOutTempConstraint(TempConstraint* temp) { outTempConstraint.push_back(temp); }
		UInt getNumInTempConstraints() const { return static_cast<UInt>(inTempConstraint.size()); }
		UInt getNumOutTempConstraints() const { return static_cast<UInt>(outTempConstraint.size()); }
		const TempConstraint* getInTempConstraint(UInt k) const { return inTempConstraint[k]; }
		const TempConstraint* getOutTempConstraint(UInt k) const { return outTempConstraint[k]; }

		void setInTempConstraintForMode(TempConstraint* temp, LocalId mlid) { inTmpConstraintsForMode[mlid].push_back(temp); }
		void setOutTempConstraintForMode(TempConstraint* temp, LocalId mlid) { outTmpConstraintsForMode[mlid].push_back(temp); }
		UInt getNumInTempConstraintsForMode(LocalId mlid) const { return static_cast<UInt>(inTmpConstraintsForMode[mlid].size()); }
		UInt getNumOutTempConstraintsForMode(LocalId mlid) const { return static_cast<UInt>(outTmpConstraintsForMode[mlid].size()); }
		const TempConstraint* getInTempConstraintForMode(LocalId mlid, UInt k) const { return inTmpConstraintsForMode[mlid][k]; }
		const TempConstraint* getOutTempConstraintForMode(LocalId mlid, UInt k) const { return outTmpConstraintsForMode[mlid][k]; }

	private:
		const string name;
		Id id;
		map<Id, LocalId> dic; // mode id --> local id
		vector<Mode*> modes;
		Int minDuration = Inf, maxDuration = 0;
		Int duedate[2]{ Inf, Inf }, duedateWeight[2]{ 1, 1 };
		PenaltyType penaltyType[2]{ PenaltyType::LINEAR ,PenaltyType::LINEAR };
		bool backward = false;

		vector<const TempConstraint*> inTempConstraint, outTempConstraint;
		vector<vector<const TempConstraint*>> inTmpConstraintsForMode, outTmpConstraintsForMode;
		Int autoSelect = 0;
		vector<Id> dependence;

		// for construct
	public:
		// LB
		void clearLbStart() { lbStart = 0; }
		void clearLbFinish() { lbFinish = 0; }
		void updateLbStart(Int t) { lbStart = max<Int>(lbStart, t); }
		void updateLbFinish(Int t) { lbFinish = max<Int>(lbFinish, t); }
		void updateLbStart(Int t, vector<Id>& criticals, Id id) {
			if (t < lbStart) { return; }
			if (t > lbStart) { lbStart = t; criticals.clear(); }
			criticals.push_back(id);
		}
		void updateLbFinish(Int t, vector<Id>& criticals, Id id) {
			if (t < lbFinish) { return; }
			if (t > lbFinish) { lbFinish = t; criticals.clear(); }
			criticals.push_back(id);
		}
		Int getLbStart() const { return lbStart; }
		Int getLbFinish() const { return lbFinish; }
		// UB
		void clearUbStart() { ubStart = Inf; }
		void clearUbFinish() { ubFinish = Inf; }
		void updateUbStart(Int t) { ubStart = min<Int>(ubStart, t); }
		void updateUbFinish(Int t) { ubFinish = min<Int>(ubFinish, t); }
		void updateUbStart(Int t, vector<Id>& criticals, Id id) {
			if (t > ubStart) { return; }
			if (t < ubStart) { ubStart = t; criticals.clear(); }
			criticals.push_back(id);
		}
		void updateUbFinish(Int t, vector<Id>& criticals, Id id) {
			if (t > ubFinish) { return; }
			if (t < ubFinish) { ubFinish = t; criticals.clear(); }
			criticals.push_back(id);
		}
		Int getUbStart() const { return ubStart; }
		Int getUbFinish() const { return ubFinish; }

		// for Test (autoselect)
		void memoryLbs() { lbStartStack = lbStart; lbFinishStack = lbFinish; }
		void resumeLbs() { lbStart = lbStartStack; lbFinish = lbFinishStack; }

		// autoselect 
		//void memoryInitialize() { if (getAutoSelect()) memory.resize(getNumModes()); }
		//void memoryClear() { memory_ite = memory.end(); }
		//bool memoryNeedsUpdate() const { return memory_ite == memory.end(); }
		//void memoryUpdate(LocalId mm, Int t) { memory[mm].mode = mm; memory[mm].start = t; }

		//LocalId getNextModeLocalId();
		//LocalId memorySort();


	private:
		Int lbStart = 0, lbFinish = 0;
		Int lbStartStack = 0, lbFinishStack = 0;
		Int ubStart = Inf, ubFinish = Inf;
		Int ubStartStack = 0, ubFinishStack = 0;

		//struct Memory {
		//	LocalId mode;
		//	Int start;
		//	Int lbStart, lbFinish;
		//};
		//vector<Memory> memory;
		//vector<Memory>::iterator memory_ite, memory_best_ite;
	};


	// ==================== TempConstraint ====================
	class TempConstraint {
	public:
		TempConstraint(Id k, Activity* p, Activity* s, TempConstraintType t, Int d = 0, Id pm = Inf, Id sm = Inf) :
			id(k), pred(p), succ(s), type(t), delay(d), pmodeLocalId(pm), smodeLocalId(sm) {
			if (pm < Inf) { pred->setOutTempConstraintForMode(this, pmodeLocalId); }
			else { pred->setOutTempConstraint(this); }
			if (sm < Inf) { succ->setInTempConstraintForMode(this, smodeLocalId); }
			else { succ->setInTempConstraint(this); }
		}
		~TempConstraint() {}
		TempConstraint(const TempConstraint&) = delete;
		TempConstraint& operator=(const TempConstraint&) = delete;

		Id getId() const { return id; }
		TempConstraintType getType() const { return type; }
		Id getPredId() const { return pred->getId(); }
		Id getSuccId() const { return succ->getId(); }
		Id getPredModeLocalId() const { return pmodeLocalId; }
		Id getSuccModeLocalId() const { return smodeLocalId; }
		Int getDelay() const { return delay; }

	private:
		Id id;
		Activity* pred, * succ;
		LocalId pmodeLocalId, smodeLocalId;
		TempConstraintType type;
		Int delay;
	};

	class Solution;
	// ==================== NrrConstraint ====================
	class NrrConstraint {
		struct Term {
			Term(Int c, Id aid, LocalId lid) : coefficient(c), activityId(aid), modeLocalId(lid) {}
			Int coefficient;
			Id activityId;
			LocalId modeLocalId;
		};

	public:
		NrrConstraint(Id k, Int w) : id(k), weight(w), rhs(0) {}
		~NrrConstraint() {}
		NrrConstraint(const NrrConstraint&) = delete;
		NrrConstraint& operator=(const NrrConstraint&) = delete;

		Id getId() const { return id; }
		Int getWeight() const { return weight; }
		void setRhs(Int r) { rhs = r; }
		void addTerm(Int coefficient, Activity* activity, LocalId lid) {
			if (activity->getAutoSelect() && weight == Inf) {
				throw runtime_error("Error: " + activity->getName() + " cannot be added to a hard Non-renewable resource constraint.\n"
					+ "Its mode will be auto-selected.");
			}
			terms.emplace_back(coefficient, activity->getId(), lid);
		}
		Int getRhs() const { return rhs; }
		UInt getNumTerms() const { return static_cast<UInt>(terms.size()); }
		Int getCoefficient(Id term) const { return terms[term].coefficient; }
		Id getActivityId(Id term) const { return terms[term].activityId; }
		Id getModeLocalId(Id term) const { return terms[term].modeLocalId; }

		Int calculatePenalty(const Solution& solution) { // used only when weight < inf (soft constraint)
			const vector<LocalId>& mode = solution.getModeLocalIds();
			Int lhs = 0;
			for (vector<Term>::iterator ite = terms.begin(); ite != terms.end(); ++ite) {
				if (mode[ite->activityId] == ite->modeLocalId) lhs += ite->coefficient;
			}
			return max<Int>(0, lhs - rhs);
		}
	private:
		Id id;
		Int weight;
		Int rhs;
		vector<Term> terms;
	};

	// ==================== Violation ====================
	class Violation {
	public:
		enum class Type { NRR = 0, TMP = 1, DUE = 2 };
		Violation(Type type_, Id id_, Int penalty_) : type(type_), id(id_), penalty(penalty_) {}
		Type type;
		Id id;
		Int penalty;
	};

	// ==================== Solution ====================
	class Solution {
	public:
		struct Execution {
			Execution(Int f, Int t, Int p) : from(f), to(t), parallel(p) {}
			Int from, to, parallel;
		};
		struct ResourceResidual {
			ResourceResidual(Int f, Int t, Int r) : from(f), to(t), residual(r) {}
			Int from, to, residual;
		};
		Solution(){} // evaluation value = weighted penalty
		~Solution() {}
		Solution(const Solution&) = delete;
		Solution& operator=(const Solution&) = delete;

		bool isFeasible() const { 
			return numActivitiesAssigned == numActivities;
			//return !executions[SinkId].empty(); 
		}
		//Mode* getMode(Id activityId) const;


		void initialize(const Solution& rhs) {
			modes = rhs.modes;
			prev = rhs.prev;
			next = rhs.next;
		}
		void initialize(const OptSeqImpl& optseq) {
			numActivities = optseq.getNumActivities();
			modes.resize(numActivities);
			executions.resize(numActivities);
			resourceResiduals.resize(optseq.getNumResources());
			prev.resize(numActivities);
			next.resize(numActivities);
			positions.resize(numActivities);
			critical_set.resize(numActivities);
			critical_vector.resize(numActivities);
		}
		void setObjective(Int value) { objective = value; }
		void setEvaluation(double value) { evaluation = value; }
		void setCpuTime(double time) { cpuTime = time; }
		void setIteration(UInt i) { iteration = i; }

		Int getObjective() const { return objective; }
		double getEvaluation() const { return evaluation; }
		double getCpuTime() const { return cpuTime; }
		UInt getIteration() const { return iteration; }

		void resetNumActivitiesAssigned() { numActivitiesAssigned = 0; }
		void updateNumActivitiesAssigned(UInt num) { numActivitiesAssigned = max(numActivitiesAssigned, num); }
		UInt getNumActivitiesAssigned() const { return numActivitiesAssigned; }


		void setModeLocalId(Id activity, LocalId mode) { modes[activity] = mode; }
		Id getModeLocalId(Id activity) const { return modes[activity]; }
		vector<LocalId>& getModeLocalIds() { return modes; }
		const vector<LocalId>& getModeLocalIds() const { return modes; }

		//void setExecution(Id activity, vector<Execution>& e) { executions[activity] = e; }
		const vector<Execution>& getExecutions(Id activity) const { return executions[activity]; }
		vector<Execution>& getExecutions(Id activity) { return executions[activity]; }
		//void clearExecutions(Id activity) { executions[activity].clear(); }

		void setResourceResiduals(Id resource, const Interval* first, Int makespan) {
			for (const Interval* interval = first; interval->getFrom() <= makespan; interval = interval->getNext()) {
				resourceResiduals[resource].emplace_back(interval->getFrom(), interval->getTo(), interval->getKey());
			}
		}
		const vector<ResourceResidual>& getResourceResiduals(Id resource) const { return resourceResiduals[resource]; }
		void clearResourceResiduals(Id resource) { resourceResiduals[resource].clear(); }

		void setPrevActivityId(Id activity, Id p) { prev[activity] = p; }
		void setNextActivityId(Id activity, Id s) { next[activity] = s; }
		Id getPrevActivityId(Id activity) const { return prev[activity]; }
		Id getNextActivityId(Id activity) const { return next[activity]; }

		void calculatePositions() {
			for (Id i = SourceId, position = 0; position < positions.size(); i = next[i], ++position) { positions[i] = position; }
		}
		UInt getPosition(Id activity) const { return positions[activity]; }
		void shiftAfter(Id activity1, Id activity2) {
			next[prev[activity1]] = next[activity1]; prev[next[activity1]] = prev[activity1];
			prev[next[activity2]] = activity1; next[activity1] = next[activity2];
			next[activity2] = activity1; prev[activity1] = activity2;
		}

		void setMakeCriticalGraph(bool flag) { makeCriticalGraph = flag; }
		void clearCritical(Id id) { critical_set[id].clear(); critical_vector[id].clear(); }
		void setCritical(Id id1, Id id2) { 
			if (!makeCriticalGraph) { return; }
			if (critical_set[id1].insert(id2).second) { critical_vector[id1].emplace_back(id2); } 
		}
		void setCritical(Id id, Resource* resource, Int from, Int to);
		vector<Id>& getCritical(Id id) { return critical_vector[id]; }

		void clearViolations() { violations.clear(); }
		void addViolation(Violation::Type type, Id id, Int penalty) { violations.emplace_back(Violation(type, id, penalty)); }
		void sortViolations() {
			sort(violations.begin(), violations.end(), [](auto const& x, auto const& y) { return x.penalty < y.penalty; });
		}
		vector<Violation>& getViolations() { return violations; }
	
		//
	private:
		Int objective = Inf;
		double evaluation = DInf;
		vector<LocalId> modes;
		vector<vector<Execution> > executions;
		vector<vector<ResourceResidual> > resourceResiduals;
		vector<Id> prev, next; // activity list; 
		vector<UInt> positions; // position of each activity in activity list
		vector<set<Id> > critical_set;
		vector<vector<Id> > critical_vector;
		vector<Violation> violations;
		bool makeCriticalGraph = false;

		UInt numActivities = 0, numActivitiesAssigned = 0; 

		double cpuTime = 0;
		UInt iteration = 0;
	};

	// ==================== Lb ====================
	struct Lb {
		Lb(Int f, Int t, Int v, Int g) : from(f), to(t), value(v), grad(g) {}
		Int from, to, value, grad;
	};

	// ==================== Move ====================
	struct Move {
		enum class Type { ChangeMode, ShiftForward };
		Move() {}
		Move(Type t, Id i1, Id i2, UInt attr, UInt rattr) :
			type(t), id1(i1), id2(i2), attribute(attr), reverse_attribute(rattr) {}
		Type type = Type::ChangeMode;
		Id id1 = 0, id2 = 0; // if type = ChangeMode, id2 is regarded as a LocalId
		UInt attribute = 0, reverse_attribute = 0;
	};


public:
	static UInt num_instances;
	OptSeqImpl()	{
		++num_instances;
	}
	~OptSeqImpl(){ // not virtual! ---	Don't use OptSeqImpl as a base class
		for (LocalId rr = 0; rr < resources.size(); rr++) { delete resources[rr]; }
		for (LocalId mm = 0; mm < modes.size(); mm++) { delete modes[mm]; }
		for (LocalId ii = 0; ii < activities.size(); ii++) { delete activities[ii]; }
		for (LocalId kk = 0; kk < tempConstraints.size(); kk++) { delete tempConstraints[kk]; }
		for (LocalId kk = 0; kk < nrrConstraints.size(); kk++) { delete nrrConstraints[kk]; }
		if (--num_instances == 0) {
			IntervalImpl::clearMemories();
		}
	}
	OptSeqImpl(const OptSeqImpl&) = delete;
	OptSeq &operator=(const OptSeq&) = delete;
	
	// ---------- resource ----------
	Id newResource(const string &name){
		if (rscDic.getId(name) < resources.size()){ throw runtime_error("resource "+name+": already declared."); }
		Id r(static_cast<Id>(resources.size()));
		Resource *resource = new Resource(name, r);
		resources.push_back(resource);
		rscDic.insert(resource->getName(), r);
		return r;
	}
	UInt getNumResources() const { return static_cast<UInt>(resources.size()); }
	Id getResourceId(const string &name) const { return rscDic.getId(name);}
	const string& getResourceName(Id id) const { return resources[id]->getName();}

	void setResourceCapacities(Id id, Int value, Int from = 1, Int to = Inf){
		resources[id]->setCapacities(value, max<Int>(from,1), min<Int>(to,Inf));
	}
	void addResourceCapacities(Id id, Int value, Int from = 1, Int to = Inf){
		resources[id]->addCapacities(value, max<Int>(from,1), min<Int>(to, Inf));
	}
	const OptSeq::Interval *getResourceCapacities(Id id) const { return resources[id]->getCapacities(1); }	

	// ----- state -----
	UInt newState(const string &name){
		if (stateDic.getId(name) < states.size()){ throw runtime_error("state"+name+": already declared."); }
		Id s(static_cast<Id>(states.size()));
		State *state = new State(name, s);
		states.push_back(state);
		stateDic.insert(state->getName(), s);
		return s;
	}

	UInt getNumStates() const { return states.size(); }
	Id getStateId(const string &name) const { return stateDic.getId(name);}
	const string& getStateName(Id id) const { return states[id]->getName();}

	
	void setStateValue(Id id, UInt value, Int time){ states[id]->setValue(value, max<Int>(time,0)); }
	const vector<pair<Int,Int> > &getStateValues(Id id) const { return states[id]->getValues(); }		
	const IntervalImpl *getStateInterval(Id id, Int time) const { return states[id]->getInterval(time); }


	//---------- mode ----------
	Id newMode(const string &name, Int duration){
		if (modeDic.getId(name) < modes.size()){ throw runtime_error("mode "+name+": already declared."); }
		if (duration < 0){ throw runtime_error("mode "+name+": invalid duration."); }
		Id m(static_cast<Id>(modes.size()));
		Mode *mode = new Mode(name, m, duration);
		modes.push_back(mode);
		modeDic.insert(mode->getName(), m);
		return m;
	}
	UInt getNumModes() const { return static_cast<UInt>(modes.size()); }
	Id getModeId(const string &name) const { return modeDic.getId(name);}
	const string& getModeName(Id id) const { return modes[id]->getName(); }
	
	Int getModeDuration(Id id) const { return modes[id]->getDuration(); }
	void setModeMaxBreakDurations(Id id, Int breakduration, Int from = 0, Int to = Inf){
		modes[id]->setMaxBreakDurations(breakduration, from, to); 
	}
	void setModeMaxNumsParallel(Id id, Int num, Int from = 1, Int to = Inf){
		modes[id]->setMaxNumsParallel(num, from, to);
	}
	const OptSeq::Interval *getModeMaxBreakDurations(Id id) const { return modes[id]->getMaxBreakDurations(0); }
	const OptSeq::Interval *getModeMaxNumsParallel(Id id) const { return modes[id]->getMaxNumsParallel(1);}

	LocalId setModeRequirements(Id mid, Id rid, Int value, Int from = 0, Int to = Inf, bool forBreak = false,
						bool maximum = false){
		return modes[mid]->setRequirements(resources[rid], value, from, to, forBreak, maximum); 
	}
	UInt getModeNumRequiredResources(Id id) const { return modes[id]->getNumRequiredResources(); }
	Id getModeRequiredResourceId(Id id, LocalId lid) const { return modes[id]->getRequiredResourceId(lid); }
	const Interval *getModeRequirements(Id id, LocalId lid, bool forBreak = false, bool maximum = false) const { 
		getModeRequiredResourceId(id, lid); // only for checking if lid is valid
		return modes[id]->getRequirements(lid, forBreak, maximum, (forBreak)? 0 : 1); 
	} 

	LocalId setModeState(Id mid, Id sid, Int from, Int to){ return modes[mid]->setState(states[sid], from, to);	}
	UInt getModeNumStates(Id mid) const { return modes[mid]->getNumStates(); }
	Id getModeStateId(Id mid, LocalId lid) const { return modes[mid]->getStateId(lid); }
	const vector<Int>& getModeState(Id mid, LocalId lid) const { return modes[mid]->getState(lid); }
	
	// ---------- activity ----------
	Id newActivity(const string &name){
		if (actDic.getId(name) < activities.size()){ throw runtime_error("activity "+name+": already declared."); }
		Id i(static_cast<Id>(activities.size()));
		Activity *act = new Activity(name, i);
		activities.push_back(act);
		actDic.insert(act->getName(), i);
		return i;
	}
	UInt getNumActivities() const { return static_cast<UInt>(activities.size()); }
	Id getActivityId(const string &name) const { return actDic.getId(name);}
	const string& getActivityName(Id id) const { return activities[id]->getName();}

	LocalId addActivityMode(Id activity, Id mode){ return activities[activity]->addMode(modes[mode]); }
	UInt getActivityNumModes(Id id) const { return activities[id]->getNumModes(); }
	Id getActivityModeId(Id activity, LocalId lid) const { return activities[activity]->getModeId(lid); }
	LocalId getActivityModeLocalId(Id activity, Id mode) const { 
		Id lid = activities[activity]->getModeLocalId(mode);
		if (lid >= getActivityNumModes(activity)) {
			throw runtime_error(getModeName(mode) + ": not a mode of " + getActivityName(activity));
		}
		return lid;
	}	
	void setActivityBackward(Id id, bool backward) { 
		if (backward && (id == SourceId || id == SinkId)) {
			throw runtime_error("Source and Sink are to be scheduled forward");
		}
		activities[id]->setBackward(backward); 
	}
	bool getActivityBackward(Id id) { return activities[id]->getBackward(); }

	void setActivityDueDate(Id id, Int d, bool finish = true) { 
		activities[id]->setDueDate(d, finish ? Activity::DueDateType::COMPLETION : Activity::DueDateType::START); 
	}
	Int getActivityDueDate(Id id, bool finish = true) const { 
		return activities[id]->getDueDate(finish ? Activity::DueDateType::COMPLETION : Activity::DueDateType::START); 
	}
	void setActivityDueDateQuad(Id id, bool q, bool finish = true) {
		Int w = getActivityDueDateWeight(id, finish);
		activities[id]->setTardinessPenalty(w, finish ? Activity::DueDateType::COMPLETION : Activity::DueDateType::START,
			q ? Activity::PenaltyType::QUADRATIC : Activity::PenaltyType::LINEAR);
	}
	bool getActivityDueDateQuad(Id id, bool finish = true) const {
		return activities[id]->getTardinessPenalty(finish ? Activity::DueDateType::COMPLETION : Activity::DueDateType::START).second
			== Activity::PenaltyType::QUADRATIC;
	}	
	void setActivityDueDateWeight(Id id, Int w, bool finish = true){
		activities[id]->setTardinessPenalty(w, finish ? Activity::DueDateType::COMPLETION : Activity::DueDateType::START); 
	}
	Int getActivityDueDateWeight(Id id, bool finish = true) const { 
		return activities[id]->getTardinessPenalty(finish ? Activity::DueDateType::COMPLETION : Activity::DueDateType::START).first;
	}
		
	void setActivityAutoSelect(Id id, Int i){ activities[id]->setAutoSelect(i); }		
	Int getActivityAutoSelect(Id id) const { return activities[id]->getAutoSelect(); }
	
	void setActivityDependence(Id id, Id id2){ activities[id]->setDependence(id2); }
	UInt getActivityNumDependences(Id id){ return activities[id]->getNumDependences(); }
	Id getActivityDependence(Id id, LocalId lid){ return activities[id]->getDependence(lid); }

	// ---------- temporal constraints ----------
	Id newTempConstraint(Id pid, Id sid, TempConstraintType type = TempConstraintType::CS, Int delay = 0, Id pmid = Inf, Id smid = Inf){
		if (pid == sid){ throw runtime_error("predecessor and successor must be different."); }
		Id k(static_cast<Id>(tempConstraints.size()));
		Id pmLocalId = (pmid == Inf) ? Inf : getActivityModeLocalId(pid, pmid);
		Id smLocalId = (smid == Inf) ? Inf : getActivityModeLocalId(sid, smid);
		TempConstraint *temp = new TempConstraint(static_cast<Id>(tempConstraints.size()), activities[pid], activities[sid], type, delay, pmLocalId, smLocalId);
		tempConstraints.push_back(temp);
		return k;
	}

	UInt getNumTempConstraints() const { return static_cast<UInt>(tempConstraints.size()); }
	TempConstraintType getTempConstraintType(Id id) const { return tempConstraints[id]->getType(); }
	Id getTempConstraintPredId(Id id) const { return tempConstraints[id]->getPredId(); }
	Id getTempConstraintSuccId(Id id) const { return tempConstraints[id]->getSuccId(); }
	Id getTempConstraintPredModeId(Id id) const {
		Id lid(getTempConstraintPredModeLocalId(id));
		return (lid == Inf) ? Inf : getActivityModeId(getTempConstraintPredId(id), lid);
	}
	Id getTempConstraintSuccModeId(Id id) const { 
		Id lid(getTempConstraintSuccModeLocalId(id));
		return (lid == Inf) ? Inf : getActivityModeId(getTempConstraintSuccId(id), lid);
	}
	LocalId getTempConstraintPredModeLocalId(Id id) const { return tempConstraints[id]->getPredModeLocalId(); }
	LocalId getTempConstraintSuccModeLocalId(Id id) const { return tempConstraints[id]->getSuccModeLocalId(); }
	Int getTempConstraintDelay(Id id) const { return tempConstraints[id]->getDelay();}
	
	// ----------	non-renewable resource constraint ----------
	Id newNrrConstraint(Int weight = Inf){
		Id k(static_cast<Id>(nrrConstraints.size()));
		NrrConstraint *nrr = new NrrConstraint(static_cast<Id>(nrrConstraints.size()),min<Int>(max<Int>(weight,0), Inf));
		nrrConstraints.push_back(nrr);
		if (weight != Inf) softNrrConstraintIds.push_back(k);
		return k;
	}
	UInt getNumNrrConstraints() const { return static_cast<UInt>(nrrConstraints.size()); }
	Int getNrrConstraintWeight(Id id) const { return nrrConstraints[id]->getWeight(); }
	void setNrrConstraintRhs(Id id, Int rhs){ nrrConstraints[id]->setRhs(rhs); }
	void addNrrConstraintTerm(Id id, Int coefficient, Id activity, Id mode) {
		nrrConstraints[id]->addTerm(coefficient, activities[activity], getActivityModeLocalId(activity, mode));
	}
	Int getNrrConstraintRhs(Id id) const { return nrrConstraints[id]->getRhs(); }
	UInt getNrrConstraintNumTerms(Id id) const { return nrrConstraints[id]->getNumTerms(); }
	Int getNrrConstraintCoefficient(Id id, Id term) const { return nrrConstraints[id]->getCoefficient(term); }
	Id getNrrConstraintActivityId(Id id, Id term) const { return nrrConstraints[id]->getActivityId(term); }
	Id getNrrConstraintModeId(Id id, Id term) const { 
		return getActivityModeId(nrrConstraints[id]->getActivityId(term),nrrConstraints[id]->getModeLocalId(term)); 
	}

	// ---------- parameters ----------
	void setRandomSeed(Int seed){ rand.init(randomSeed = seed); }
	void setTimeLimit(double time){ timeLimit = time; }
	void setIterationLimit(UInt iteration){ iterationLimit = iteration; }
	void setInitialSolutionFile(string name){ initialSolutionFile = name; }
	void setNeighborhoodSize(UInt size){ neighborhoodSize = size; }
	void setReportInterval(UInt interval){ reportInterval = interval; }
	void setMaxNumBacktracks(UInt num){ maxNumBacktracks = num; }
	void setTabuTenure(UInt num){ tabuTenure = num; }
	void setWeightControl(UInt num){ weightControl = num; }
	
	Int getRandomSeed() const { return randomSeed; }
	double getTimeLimit() const { return timeLimit; }
	UInt getIterationLimit() const { return iterationLimit; }
	UInt getNeighborhoodSize() const { return neighborhoodSize; }
	UInt getReportInterval() const { return reportInterval; }
	UInt getMaxNumBacktracks() const { return maxNumBacktracks; }
	UInt getTabuTenure() const { return tabuTenure; }
	UInt getWeightControl() const { return weightControl; }

	// ---------- solve ----------
	void solve();
	double getCpuTime() const { return cputime(); }
	UInt getIteration() const { return iteration; }

	Int getSolutionObjective() const { return solutions[incumbent].getObjective(); }
	bool isSolutionFeasible() const { return (solutions[incumbent].getObjective() > bigM)? false : true; }
	Id getSolutionModeId(Id activity) const { 
		return activities[activity]->getModeId(solutions[incumbent].getModeLocalId(activity));
	}
	const OptSeq::Interval *getSolutionExecutions(Id activity) const { 
		while (executionLists[activity]){
			IntervalImpl *ptr = executionLists[activity]->getNext();
			delete executionLists[activity];
			executionLists[activity] = ptr;
		}
		const vector<Solution::Execution> &exec = solutions[incumbent].getExecutions(activity);
		for (vector<Solution::Execution>::const_reverse_iterator ite = exec.rbegin(); ite != exec.rend(); ++ite){
			executionLists[activity] = new IntervalImpl(ite->from, ite->to, ite->parallel, 0, executionLists[activity]);
		}
		return executionLists[activity]; 
	}
	const OptSeq::Interval *getSolutionResourceResiduals(Id resource) const {
		while (resourceResidualLists[resource]){
			IntervalImpl *ptr = resourceResidualLists[resource]->getNext();
			delete resourceResidualLists[resource];
			resourceResidualLists[resource] = ptr;
		}
		const vector<Solution::ResourceResidual> &res = solutions[incumbent].getResourceResiduals(resource);
		for (vector<Solution::ResourceResidual>::const_reverse_iterator ite = res.rbegin(); ite != res.rend(); ++ite){
			resourceResidualLists[resource] = new IntervalImpl(ite->from-1, ite->to, ite->residual, 0, resourceResidualLists[resource]);
		}
		return resourceResidualLists[resource]; 
	}
	Id getSolutionNextActivityId(Id activity) const { return solutions[incumbent].getNextActivityId(activity); }
	double getSolutionCpuTime() const { return solutions[incumbent].getCpuTime(); }
	UInt getSolutionIteration() const { return solutions[incumbent].getIteration(); }

	// ========== private members ========== 
private:
	// ---------- member functions ----------
	Id getActivityScc(Id activity) const { return scc[activity]; }
	bool precede(Id activity1, Id activity2) const { return precedenceTable[activity1][activity2]; }

	void updateCurrentSolution();
	bool search();
	void updatePenaltyWeights(Solution&);
	void shiftForward(Solution&, Id, Id);
	void collectMovesChangeMode(Id);
	void collectMovesSoftNrr(Id);
	void collectMovesTemporal(Id);
	void collectMovesDueDate(Id, Int);
	void addMove(Move::Type, Id, Id);
		
	bool makeModesFeasible(Solution&, Id fixed = Inf);
	void schedule(Solution&);
	Int schedule(Solution&, Id&, bool test = false);
	Int scheduleReverse(Solution&, Id&, bool test = false);
	void calculatePenalty(Solution&);
	void assign(Id aid, const Mode*, const vector<Solution::Execution>&, Int);

	// ---------- Data ----------
	// ----- dictionaries for resources, activities, etc.
	class Dic{ // anonymous 
	public:
		void insert(const string &name, Id id){ dic[&name] = id; }
		Id getId(const string &name) const { 
			map<const string*,UInt,lt>::const_iterator ite = dic.find(&name);
			if (ite == dic.end()) return static_cast<Id>(dic.size());
			else return ite->second;
		}
	private:
		struct lt{ bool operator()(const string *s1, const string *s2) const { return *s1 < *s2; } };
		map<const string*,Id,lt> dic;
	};
	Dic rscDic, stateDic, modeDic, actDic;
	// ----- Arrays of pointers -----
	vector<Resource*> resources;
	vector<State*> states;
	vector<Mode*> modes;
	vector<Activity*> activities;
	vector<TempConstraint*> tempConstraints;
	vector<NrrConstraint*> nrrConstraints;
	vector<Id> softNrrConstraintIds;

	// ----- CSP -----
	unique_ptr<CSP> csp;
	UInt cspIterationLimit = 0;

	// ----- Temporal Scheduling Network, Precedence table -----
	vector<Id> scc;
	vector<vector<bool> > precedenceTable;

	// ---------- Parameters ----------
	Int randomSeed = defaultRandomSeed;
	double timeLimit = defaultTimeLimit;
	UInt iterationLimit = defaultIterationLimit;
	string initialSolutionFile = "";
	UInt neighborhoodSize = defaultNeighborhoodSize;
	UInt reportInterval = defaultReportInterval;
	UInt maxNumBacktracks = defaultMaxNumBacktracks;
	UInt maxViolationCount = defaultMaxViolationCount;
	UInt tabuTenure = defaultTabuTenure;
	UInt weightControl = defaultWeightControl;

	// ----------Other private data ----------
	Random<Int> rand;
	CpuTime cputime;
	UInt iteration = 0;
	Solution solutions[4];
	Id incumbent = 0, current = 1, neighbor = 2, bestNeighbor = 3;
	mutable vector<IntervalImpl*> executionLists;
	mutable vector<IntervalImpl*> resourceResidualLists;
	Tabulist<Int,UInt> tabu;
	UInt numSccs = 0;

	//	- for construct
	//vector<Solution::Execution> executions;
	vector<Lb> lbs; 
	vector<Int> stateValues; // used in assinging an activity; stores the value for each State (LocalId)
	vector<Id> criticalsS, criticalsF;
	UInt numBacktracks = 0;
	vector<UInt> violationCount;

	// - for neighborhood search
	vector<Move> moves;
	vector<UInt> changeModeActivityFlag;
	vector<vector<UInt> > changeModeFlag;
	vector<vector<UInt> > shiftForwardFlag;
	vector<vector<UInt> > shiftForwardSccFlag;
	vector<UInt> ltm;


	// penalty weights
	vector<double> penaltyWeightStart, penaltyWeightFinish, penaltyWeightSoftNrr;
};

ostream& operator<<(ostream&, const OptSeqImpl::Activity&);
ostream& operator<<(ostream&, const OptSeqImpl::Mode&);
ostream& operator<<(ostream&, const vector<OptSeqImpl::Solution::Execution>&);