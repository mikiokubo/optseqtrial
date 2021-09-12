#include <iostream>
#include <climits>
#include "csp.h"
#include "random.h"
#include "tabulist.h"

static constexpr auto DEBUG = 0;

// ==================== CSPImpl ====================
class CSPImpl: public CSP{
public:
	CSPImpl(vector<Id> num_values)
		: numVariables(num_values.size()), numValues(num_values), labels(numVariables+1){
		for (Id i = 0; i < numVariables; ++i){
			labels[i].resize(numValues[i]);
			for (Id j = 0; j < numValues[i]; ++j){ labels[i][j].variable = i; labels[i][j].value = j; }
		}
		// label that will be used as a sentinel
		labels[numVariables].resize(1); 
		labels[numVariables][0].variable = numVariables; 
		labels[numVariables][0].value = 0;
		// tabulist
		tabu.init(numVariables);
		// fixed variable
		latestCounts.resize(numVariables, 0);
	}
	~CSPImpl(){
		for (auto constraint : constraints) { delete constraint; }
	}
	CSPImpl(const CSPImpl&) = delete;
	CSPImpl &operator=(const CSPImpl&) = delete;
	
	Id addConstraint(Constraint* c) {
		constraints.push_back(c); 
		return constraints.size() - 1;
	}
	void preprocess();
	void resetFixedVariables() { ++counter; }
	void resetFixedVariable(Id id) { latestCounts[id] = counter - 1; }
	void setFixedVariable(Id id) { latestCounts[id] = counter; }
	bool isFixed(Id id) { return (latestCounts[id] == counter); }
	UInt solve(Solution&);
	UInt getIterationLimit() const { return iterationLimit; }
	void setIterationLimit(UInt limit){ iterationLimit = limit; }
		
private:
	// functions invoked by Constraints
	UInt getNumVariables(){ return numVariables; }
	UInt getNumValues(Id variable){ return numValues[variable]; }
	void labelsConcerned(Id c, Id variable, Id value){
		labels[variable][value].constraintIds.push_back(c); 
	}
	void setDiffPenalty(Id c, Id variable, Id value, Int diff){
		memories[c].cdiff->emplace_back(&labels[variable][value],diff);
	}
	// functions (completely) hidden from users
	void calculate(Id c);
	void nominate(Id variable, Id value){
		if (++labels[variable][value].numNominated == 1){
			labels[variable][value].candidateId = candidates.size();
			candidates.push_back(&labels[variable][value]);
		}
	}
	void unnominate(Id variable, Id value){
		if (--labels[variable][value].numNominated == 0){
			candidates[labels[variable][value].candidateId] = *candidates.rbegin();
			(*candidates.rbegin())->candidateId = labels[variable][value].candidateId;
			candidates.pop_back();
		}
	}
	// ----- struct Label -----
	struct Label{
		Id variable = 0, value = 0;
		vector<Id> constraintIds;
		Int diffPenalty = 0;
		UInt numNominated = 0, candidateId = 0;
		void clear(){ diffPenalty = numNominated = 0; }
	};
	// ----- DiffPenalty-----
	struct DiffPenalty{
		DiffPenalty(Label *l, Int diff): label(l), diffPenalty(diff){}
		Label *label;
		Int diffPenalty;
	};
	// ----- struct Memory -----
	struct Memory{
		Memory(): pdiff(&diff[0]), cdiff(&diff[1]){}
		Memory(const Memory&): pdiff(&diff[0]), cdiff(&diff[1]){}
		Int penalty = 0;
		vector<DiffPenalty> *pdiff, *cdiff; // previous and current differences
		vector<DiffPenalty> diff[2];
	};
	// ----- data members -----
	Random<Int> rand;
	UInt numVariables = 0;
	vector<Id> numValues;
	vector<Constraint*> constraints;
	vector<Memory> memories;
	vector<vector<Label> > labels;
	vector<Label*> candidates, bests;
	Tabulist<Int, UInt> tabu;
	UInt counter = 1;
	vector<Id> latestCounts;
	Solution solution;
	vector<vector<Id>::iterator> iterators;
	Int penalty = 0;
	UInt iterationLimit = 0;
};

// ---------- CSP ----------
CSP*
CSP::makeCSP(vector<Id> &numValues){
	return new CSPImpl(numValues);
}

// ---------- CSPImpl ----------
void
CSPImpl::preprocess() {
	memories.resize(constraints.size());
	for (auto constraint : constraints) { constraint->preprocess(); }
	// set sentinels
	for (Id i = 0; i < numVariables; ++i) {
		for (Id j = 0; j < numValues[i]; ++j) {
			labels[i][j].constraintIds.push_back(constraints.size());
		}
	}
}

void
CSPImpl::calculate(Id c){
	memories[c].cdiff->clear();
	auto p = constraints[c]->calculate(solution); // in which setDiffPenalty should be invoked in an appropriate order
	penalty += p - memories[c].penalty;
	memories[c].cdiff->emplace_back(&labels[numVariables][0],0); // sentinel
	auto pite = memories[c].pdiff->begin();
	auto cite = memories[c].cdiff->begin();
	while (1){
		auto variable = pite->label->variable, value = pite->label->value;
		if (cite->label->variable < variable){ variable = cite->label->variable; value = cite->label->value; }
		else if (cite->label->variable == variable){ value = min(value, cite->label->value); }
		if (variable == numVariables) break;
		Int pdiff = 0, cdiff = 0;
		if (pite->label->variable == variable && pite->label->value == value){ pdiff = pite->diffPenalty; ++pite; }
		if (cite->label->variable == variable && cite->label->value == value){ cdiff = cite->diffPenalty; ++cite; }
		if (pdiff < 0 && cdiff >= 0){ unnominate(variable, value); }
		if (pdiff >= 0 && cdiff < 0){ nominate(variable, value); }
		//if (cdiff != pdiff) cout << variable << " " << value << " " << cdiff << " " << pdiff << endl;
		labels[variable][value].diffPenalty += cdiff - pdiff;
	}
	memories[c].penalty = p;
	swap(memories[c].pdiff, memories[c].cdiff);
}

CSP::UInt
CSPImpl::solve(Solution& initial) {
	// initialize 
	if (solution.empty()) {
		solution = initial;
		penalty = 0;
		candidates.clear();
		for (Id i = 0; i < numVariables; i++) {
			for (Id j = 0; j < numValues[i]; j++) {
				labels[i][j].clear();
			}
		}
		for (Id c = 0; c < constraints.size(); ++c) {
			memories[c].penalty = 0;
			memories[c].pdiff->clear();
			memories[c].pdiff->emplace_back(&labels[numVariables][0], 0);
			calculate(c);
		}
	}
	else {
		iterators.clear();
		for (Id i = 0; i < numVariables; ++i) {
			if (solution[i] != initial[i]) {
				iterators.push_back(labels[i][solution[i]].constraintIds.begin());
				iterators.push_back(labels[i][initial[i]].constraintIds.begin());
				solution[i] = initial[i];
			}
		}
		while (1) {
			Id c = constraints.size();
			for (UInt k = 0; k < iterators.size(); ++k) {
				c = min(c, *iterators[k]);
			}
			if (c == constraints.size()) { break; }
			calculate(c);
			for (UInt k = 0; k < iterators.size(); ++k) {
				if (*iterators[k] == c) { ++iterators[k]; }
			}
		}
	}
	// main loop
	tabu.clear(1);
	Id iteration;
	for (iteration = 1; penalty > 0 && iteration <= iterationLimit; ++iteration) {
		// neighbor search
		bests.clear();
		Int minDiffPenalty = numeric_limits<Int>::max();
		for (auto candidate : candidates) {
			Id i = candidate->variable;
			if (isFixed(i)) { continue; }
			Id j = candidate->value;
			if (solution[i] == j) { continue; }
			if (tabu(i, penalty + labels[i][j].diffPenalty, labels[i][j].diffPenalty)) { continue; }
			if (labels[i][j].diffPenalty < minDiffPenalty) { minDiffPenalty = labels[i][j].diffPenalty; bests.clear(); }
			if (labels[i][j].diffPenalty == minDiffPenalty) { bests.push_back(candidate); }
		}
		if (bests.empty()) {
			tabu.clear(1);
			continue;
		}
		Label* best = bests[rand(0, bests.size() - 1)];
		Id out_value = solution[best->variable];
		solution[best->variable] = best->value;
		// update penalties and differences of penalty
		auto in = labels[best->variable][best->value].constraintIds.begin();
		auto out = labels[best->variable][out_value].constraintIds.begin();
		while (1) {
			Id c = min(*in, *out);
			if (c == constraints.size()) break;
			calculate(c);
			if (c == *in) { ++in; }
			if (c == *out) { ++out; }
		}
		// update tabu
		tabu.update(best->variable, best->variable, penalty, minDiffPenalty, rand(-1, 1), DEBUG);
	}
	if (penalty == 0) { initial = solution; }
	return iteration;
}
