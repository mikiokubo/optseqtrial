#include "optseqimpl.h"

const UInt OptSeqImpl::UInf = static_cast<UInt>(Inf);
const double OptSeqImpl::DInf = 1e+15;

const Int OptSeqImpl::defaultRandomSeed = 1;
const double OptSeqImpl::defaultTimeLimit = 600;
const UInt OptSeqImpl::defaultIterationLimit = static_cast<int>(Inf);
const UInt OptSeqImpl::defaultNeighborhoodSize = 20;
const UInt OptSeqImpl::defaultReportInterval = static_cast<int>(Inf);
const UInt OptSeqImpl::defaultMaxNumBacktracks = 1000;
const UInt OptSeqImpl::defaultMaxViolationCount = 1000; 
const UInt OptSeqImpl::defaultTabuTenure = 1;
const UInt OptSeqImpl::defaultWeightControl = 20;

const UInt OptSeqImpl::maxCspIterationLimit = 1000000;

UInt OptSeqImpl::num_instances = 0;
//

// ---------- operator<< ----------
ostream& operator<<(ostream& os, const OptSeqImpl::Activity& activity) {
	os << activity.getName();
	return os;
}
ostream& operator<<(ostream& os, const OptSeqImpl::Mode& mode) {
	os << mode.getName();
	return os;
}

ostream& operator<<(ostream& os, const vector<OptSeqImpl::Solution::Execution>& executions) {
	for (auto ptr = executions.begin(); ptr != executions.end(); ++ptr) {
		os << ptr->from;
		if (ptr->from < ptr->to) {
			os << "--" << ptr->to;
			if (ptr->parallel > 1) {
				os << "[" << ptr->parallel << "]";
			}
		}
		os << " ";
	}
	return os;
}

// ---------- OptSeq ----------
OptSeq* 
OptSeq::makeOptSeq(){ 
	OptSeqImpl *optseq = new OptSeqImpl();
	Id dummyModeId = optseq->newMode("dummy", 0);
	optseq->addActivityMode(optseq->newActivity("source"), dummyModeId);
	optseq->addActivityMode(optseq->newActivity("sink"), dummyModeId);
	return optseq;
}

// ---------- OptSeqImpl ----------
const Int OptSeqImpl::maxStateValue = 9999;

// ---------- OptSeqImpl::IntervalImpl ----------
OptSeqImpl::IntervalImpl* OptSeqImpl::IntervalImpl::first = nullptr;

OptSeqImpl::IntervalImpl*
OptSeqImpl::IntervalImpl::split(Int time) {
	// "time \in [from,to]" is required.
	//	[from,to] --> [from,time-1] + [time,to]
	if (time < from || to < time) throw runtime_error("error in IntervalImpl::split()");
	if (from == time) return this;
	IntervalImpl* newone = new IntervalImpl(time, to, key, this, next);
	to = time - 1;
	return newone;
}

// ---------- OptSeqImpl::DiscreteFunction ----------
OptSeqImpl::IntervalImpl*
OptSeqImpl::DiscreteFunction::splitInterval(Int time) {
	IntervalImpl* interval = find(time);
	IntervalImpl* newone = interval->split(time);
	return newone;
}

void
OptSeqImpl::DiscreteFunction::addKey(Int delta, Int from, Int to) {
	if (from > to) throw runtime_error("invalid arguments (addKey).");
	IntervalImpl* first = splitInterval(from);
	IntervalImpl* last = splitInterval(to + 1);
	for (IntervalImpl* ptr = first; ptr != last; ptr = ptr->getNext()) ptr->setKey(ptr->getKey() + delta);
	if (first != begin && first->getPrev()->getKey() == first->getKey()) deleteInterval(first);
	if (last != end && last->getPrev()->getKey() == last->getKey()) deleteInterval(last);
}

void
OptSeqImpl::DiscreteFunction::setKey(Int key, Int from, Int to) {
	if (from > to) throw runtime_error("invalid arguments (setKey).");
	IntervalImpl* first = splitInterval(from);
	IntervalImpl* last = splitInterval(to + 1);
	first->setKey(key);
	while (first->getNext() != last) deleteInterval(first->getNext());
	if (first != begin && first->getPrev()->getKey() == first->getKey()) deleteInterval(first);
	if (last != end && last->getPrev()->getKey() == last->getKey()) deleteInterval(last);
}

OptSeqImpl::IntervalImpl*
OptSeqImpl::DiscreteFunction::find(Int time) const {
	// return the interval [from,to] s.t. time \in [from,to]
	if (time < begin->getFrom() || time > end->getTo()) {
		throw runtime_error("invalid arguments (find).");
	}
	if (time < pos->getFrom()) {
		do {
			pos = pos->getPrev();
		} while (time < pos->getFrom());
	}
	else {
		while (time > pos->getTo()) pos = pos->getNext();
	}
	return pos;
}

// ---------- OptSeqImpl::Resource ----------
void
OptSeqImpl::Resource::clear() {
	for (auto capacity = capacities.resetPos(); capacity->getFrom() <= maxtime; capacity = capacity->getNext()) {
		profile.setKey(capacity->getKey(), capacity->getFrom(), capacity->getTo());
	}
	maxtime = 0;

	cells.clear();
	if (activities) {
		while (activities->getNext()) delete activities->getNext();
		delete activities;
	}

	activityPtr = activities = new IntervalImpl(1, Inf, -1); // -1 in "key" field stands for null pointer
	new IntervalImpl(Inf + 1, Inf + 1, Inf, activities, 0);
}

void
OptSeqImpl::Resource::consume(Int from, Int to, Int amount, Id activityId) {
	if (amount == 0) return;
	profile.addKey(-amount, from, to);
	maxtime = max(maxtime, to);

	adjustActivityPtr(from);

	if (amount > 0) {
		Int cid = activityPtr->getKey();
		if (cid >= 0 && cells[cid].activityId == activityId) {
			if (from != to || activityPtr->getTo() != to) { throw runtime_error("consume"); }
			return;
		}

		if (activityPtr->getFrom() == from && activityPtr->getPrev()) {
			Int cid = activityPtr->getPrev()->getKey();
			if (cid >= 0 && cells[cid].activityId == activityId && cells[cid].next == activityPtr->getKey()) {
				activityPtr->getPrev()->setTo(activityPtr->getTo());
				activityPtr = activityPtr->getPrev();
				delete activityPtr->getNext();
				activityPtr->setKey(cells[cid].next);
				cells.pop_back();
				from = activityPtr->getFrom();
			}
		}
		for (activityPtr = activityPtr->split(from); activityPtr->getFrom() <= to; activityPtr = activityPtr->getNext()) {
			cells.emplace_back(activityId, activityPtr->getKey());
			activityPtr->setKey(static_cast<Int>(cells.size()) - 1);
			if (to < activityPtr->getTo()) activityPtr->split(to + 1)->setKey(cells[activityPtr->getKey()].next);
		}
	}
	else {
		IntervalImpl* first = activityPtr;
		for (Int cid; (cid = activityPtr->getKey()) >= 0 && cells[cid].activityId == activityId; activityPtr = activityPtr->getNext()) {
			activityPtr->setKey(cells[cid].next);
			if (static_cast<UInt>(cid) == cells.size() - 1) {
				while (!cells.empty() && cells.rbegin()->activityId == activityId) cells.pop_back();
			}
		}
		IntervalImpl* last = activityPtr;
		activityPtr = first;
		if (activityPtr->getPrev() && activityPtr->getKey() == activityPtr->getPrev()->getKey()) {
			activityPtr->getPrev()->setTo(activityPtr->getTo());
			activityPtr = activityPtr->getPrev();
			delete activityPtr->getNext();
		}
		if (last != first) {
			activityPtr = last;
			if (activityPtr->getPrev() && activityPtr->getKey() == activityPtr->getPrev()->getKey()) {
				activityPtr->getPrev()->setTo(activityPtr->getTo());
				activityPtr = activityPtr->getPrev();
				delete activityPtr->getNext();
			}
		}
	}
}

// ---------- OptSeqImpl::State ----------
void
OptSeqImpl::State::clear() {
	for (const Interval* value1 = values.resetPos(), *value2 = nullptr; value1->getFrom() <= maxtime; value1 = value2) {
		value2 = value1->getNext();
		while (value2->getKey() < 0) value2 = value2->getNext();
		// value{1,2}->getKey() both must be non-negative (i.e., values scheduled in advance)
		values.setKey(value1->getKey(), value1->getFrom(), value2->getFrom() - 1);
	}
	maxtime = 0;
	changed.clear();
}

void
OptSeqImpl::State::change(Int time, Id activityId, Int to) {
	Interval* interval = values.getInterval(time);
	if (interval->getFrom() == time) {
		// in this case, interval->getKey() must be >= 0; i.e., // the state of "time" cannot be changed.
		if (interval->getKey() < 0) throw runtime_error("error in State::change.");
		return;
	}
	maxtime = max(maxtime, time);
	changed.emplace_back(activityId, to);
	values.setKey(-static_cast<Int>(changed.size()), time, interval->getTo());
}

void
OptSeqImpl::State::undo(Int time) {
	Interval* interval = values.getInterval(time);
	if (interval->getFrom() != time) {
		if (interval->getKey() < 0) throw runtime_error("error in State::unchange.");
		return;
	}
	maxtime = max(maxtime, time);
	values.setKey(interval->getPrev()->getKey(), interval->getPrev()->getFrom(), interval->getTo());
}


// ---------- OptSeqImpl::Mode ----------
LocalId
OptSeqImpl::Mode::setRequirements(const Resource* resource, Int value, Int from, Int to, bool forBreak, bool m) {
	from = max<Int>(from, forBreak ? 0 : 1);
	to = min(to, duration);
	if (from < 0 || from > to || value < 0) { throw runtime_error("invalid arguments (setRequirements)."); }
	Id rid = resource->getId();
	map<Id, LocalId>::iterator ite = rscDic.find(rid);
	LocalId lid;
	if (ite != rscDic.end()) {
		lid = ite->second;
		if (maximum[lid] != m) { throw runtime_error("inconsistent requirement-type declaration."); }
	}
	else {
		lid = static_cast<Id>(rscDic.size()); rscDic[rid] = lid;
		requiredResources.push_back(resource);
		requirements.push_back(new DiscreteFunction * [2]);
		requirements[lid][0] = new DiscreteFunction(1, duration, 0);
		requirements[lid][1] = new DiscreteFunction(0, duration, 0);
		maximum.push_back(m);
	}
	requirements[lid][forBreak]->setKey(value, from, to);
	return lid;
}

LocalId
OptSeqImpl::Mode::updateState(State* state, Int from, Int to) {
	if (from > maxStateValue) {
		stringstream ss;
		ss << "State value (" << state->getName() << "," << from
			<< ") exceeds the limit (" << maxStateValue << ")";
		string message(ss.str());
		throw runtime_error(message);
	}
	else if (from < 0) {
		stringstream ss;
		ss << "State value (" << state->getName() << "," << from << ") must be non-negative";
		string message(ss.str());
		throw runtime_error(message);
	}

	Id sid = state->getId();
	map<Id, LocalId>::iterator ite = stateDic.find(sid);
	LocalId lid;
	if (ite != stateDic.end()) lid = ite->second;
	else {
		lid = static_cast<Id>(stateDic.size()); stateDic[sid] = lid;
		states.push_back(state);
		stateTables.push_back(new vector<Int>);
	}
	if (static_cast<Int>(stateTables[lid]->size()) < from + 1) {
		UInt oldsize = static_cast<UInt>(stateTables[lid]->size());
		stateTables[lid]->resize(from + 1);
		for (Int value = oldsize; value < from; ++value) {
			(*stateTables[lid])[value] = -1;
		}
	}
	(*stateTables[lid])[from] = to;
	return lid;
}

void
OptSeqImpl::Mode::calcMinMaxDurations() {
	minDuration = 0;
	auto ptr = maxNumsParallel.resetPos(), ptr2 = ptr->getNext();
	vector<pair<Int, Int>> modify;
	for (Int key = ptr->getKey(), key2 = 0; ptr2->getTo() <= duration; ptr = ptr2, ptr2 = ptr2->getNext(), key = key2) {
		if ((key2 = ptr2->getKey()) < key - 1) {
			for (Int t = ptr2->getFrom(); t <= ptr2->getTo() && key2 < key - 1; ++t) {
				modify.emplace_back(t, --key);
			}
			key2 = key;
		}
	}
	if (!modify.empty()) {
		cout << "WARNING: maxNumParallel modified (" << name << ")" << endl;
		for (auto ite = modify.begin(); ite != modify.end(); ++ite) {
			cout << "\tinterval " << ite->first << " " << ite->first << " max " << ite->second << endl;
			setMaxNumsParallel(ite->second, ite->first, ite->first);
		}
	}

	ptr = maxNumsParallel.resetPos();
	for (Int t = 0; t < duration; t += ptr->getKey()) {
		while (ptr->getTo() < t + 1) { ptr = ptr->getNext(); }
		minDuration++;
	}
	maxDuration = duration;
	for (ptr = maxBreakDurations.resetPos(); ptr->getTo() <= duration; ptr = ptr->getNext()) {
		Int key(ptr->getKey()), interval(ptr->getTo() - ptr->getFrom() + 1);
		// if maxd + key*interval >= inf, then maxd := inf
		if (key > (Inf - maxDuration) / interval || key == (Inf - maxDuration) / interval && (Inf - maxDuration) % interval == 0) {
			maxDuration = Inf;
			break;
		}
		maxDuration += key * interval;
	}
}

OptSeqImpl::Mode *
OptSeqImpl::Mode::getReverseMode(){
	if (!reverse) {
		// make reverse-mode
		reverse = new Mode(name, id, duration);
		for (auto ptr = maxBreakDurations.resetPos(); ptr->getFrom() <= duration; ptr = ptr->getNext()) {
			if (ptr->getKey() > 0) {
				reverse->setMaxBreakDurations(ptr->getKey(), duration - ptr->getTo(), duration - ptr->getFrom());
			}
		}
		{
			auto ptr = maxNumsParallel.resetPos();
			for (Int key0 = 1, key = 0; ptr->getFrom() <= duration; ptr = ptr->getNext(), key0 = key) {
				key = ptr->getKey();
				Int from = min<Int>(ptr->getFrom() + key - 1, duration);
				Int to = min<Int>(ptr->getTo() + key - 1, duration);
				if (key < key0) { // key == key0 - 1
					if (ptr->getFrom() < ptr->getTo() && from < duration) {
						reverse->setMaxNumsParallel(key, duration - to + 1, duration - (from + 1) + 1);	// [to+key-1, (from+1)+key-1]
					}
				}
				else { // key > key0
					if (ptr->getFrom() == 1) { reverse->setMaxNumsParallel(from, duration - from + 1, Inf); }
					else {
						for (Int progress = ptr->getFrom() + key0 - 1; progress <= from; ++progress) {
							reverse->setMaxNumsParallel(progress - ptr->getFrom() + 1, duration - progress + 1, duration - progress + 1);
						}
					}
					if (from < duration) {
						reverse->setMaxNumsParallel(key, duration - to + 1, duration - from + 1); 	// [to+key-1, from+key-1]
					}
				}
			}
		}
		for (LocalId rr = 0; rr < getNumRequiredResources(); ++rr) {
			// forBreak = false
			for (auto ptr = requirements[rr][false]->resetPos(); ptr != 0; ptr = ptr->getNext()) {
				if (ptr->getKey() > 0) {
					reverse->setRequirements(requiredResources[rr], ptr->getKey(), duration - ptr->getTo() + 1, duration - ptr->getFrom() + 1, false, maximum[rr]);
				}
			}
			// forBreak = true
			for (auto ptr = requirements[rr][true]->resetPos(); ptr != 0; ptr = ptr->getNext()) {
				if (ptr->getKey() > 0) {
					reverse->setRequirements(requiredResources[rr], ptr->getKey(), duration - ptr->getTo(), duration - ptr->getFrom(), true, maximum[rr]);
				}
			}
		}
#if 0
		cout << getName() << endl;
		for (auto ptr = reverse->getMaxBreakDurations(0); ptr->getFrom() <= duration; ptr = ptr->getNext()) {
			//			if (ptr->getKey() == 0) continue;
			cout << "\tbreak interval " << ptr->getFrom() << " " << ptr->getTo();
			cout << " max " << ptr->getKey();
			cout << endl;
		}
		//for (auto ptr = optseq.getModeMaxNumsParallel(m); ptr; ptr = ptr->getNext()) {
		for (auto ptr = reverse->getMaxNumsParallel(1); ptr->getFrom() <= duration; ptr = ptr->getNext()) {
			if (ptr->getKey() == 1) { continue; }
			cout << "\tparallel interval " << ptr->getFrom() << " " << ptr->getTo();
			cout << " max " << ptr->getKey();
			cout << endl;
		}
		for (LocalId rr = 0; rr < reverse->getNumRequiredResources(); ++rr) {
			cout << "\t" << requiredResources[rr]->getName();
			bool maximum = (reverse->getRequirements(rr, false, true) != 0); // "false" of the 3rd argument could be true
			if (maximum) { cout << "\tmax"; }
			cout << endl;
			for (Int forBreak = 0; forBreak <= 1; ++forBreak) {
				for (auto ptr = reverse->getRequirements(rr, forBreak != 0, maximum, 1 - forBreak); ptr != 0; ptr = ptr->getNext()) {
					if (ptr->getKey() == 0) { continue; }
					cout << "\t\tinterval " << (forBreak ? "break " : "") << (forBreak ? ptr->getFrom() : ptr->getFrom() - 1)
						<< " " << ptr->getTo() << "\trequirement " << ptr->getKey();
					cout << endl;
				}
			}
			cout << endl;
		}
#endif
	}
	return reverse;
}

// ---------- OptSeqImpl::Activity ----------
//LocalId
//OptSeqImpl::Activity::memorySort() {
//	sort(memory.begin(), memory.end(), [](auto const& x, auto const& y) { return x.start < y.start; });
//	memory_ite = memory_best_ite = memory.begin();
//	return memory_ite->mode;
//}

//LocalId
//OptSeqImpl::Activity::getNextModeLocalId() { // lbStart & lbFinish are restored
//	memory_ite->lbStart = lbStart;
//	memory_ite->lbFinish = lbFinish;
//	if (memory_ite->lbStart < memory_best_ite->lbStart ||
//		memory_ite->lbStart == memory_best_ite->lbStart && memory_ite->lbFinish < memory_best_ite->lbFinish) {
//		// redundant if memory_ite == memory.begin()
//		memory_best_ite = memory_ite;
//	}
//	if (++memory_ite == memory.end() ||
//		memory_ite->start >= max(memory_best_ite->lbStart, memory_best_ite->lbFinish)) {
//		lbStart = memory_best_ite->lbStart;
//		lbFinish = memory_best_ite->lbFinish;
//		return memory_best_ite->mode;
//	}
//	resumeLbs();
//	return memory_ite->mode;
//}


// ---------- OptSeqImpl::Solution ----------
void
OptSeqImpl::Solution::setCritical(Id id, Resource* resource, Int from, Int to) {
	if (!makeCriticalGraph) { return; }
	for (const IntervalImpl* activityPtr = resource->adjustActivityPtr(from);
		activityPtr->getFrom() <= to; activityPtr = activityPtr->getNext()) {
		for (Int cid = activityPtr->getKey(); cid >= 0; cid = resource->getNextActivityCell(cid)) {
			setCritical(id, resource->getActivityId(cid));
		}
	}
}
