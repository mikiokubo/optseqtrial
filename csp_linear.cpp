#include "csp.h"

// ==================== CSP::Linear ====================
void
CSP::Linear::preprocess() {
	sort(terms.begin(), terms.end(),
		[](auto const& x, auto const& y) {
			return x.variable < y.variable || x.variable == y.variable && x.value < y.value;
		});
	for (auto term = terms.begin(); term != terms.end(); ++term) {
		labelsConcerned(term->variable, term->value);
	}
}

CSP::Int
CSP::Linear::calculate(const CSP::Solution& solution) {
	// calculate penalty
	excess = -rhs;
	for (auto term = terms.begin(); term != terms.end(); ++term) {
		if (term->value == solution[term->variable]) excess += term->coefficient;
	}
	// calculate differences of penalty
	auto term = terms.begin();
	while (term != terms.end()) {
		UInt variable = term->variable;
		auto end = term, current = terms.end();
		// "end" will be used as an iterator within the following while-statement, then used as a sentinel
		do {
			if (end->value == solution[variable]) current = end;
		} while (++end != terms.end() && end->variable == variable);

		if (current != terms.end()) { // coefficient_{variable}{solutions[variable]} = current->coefficient
			for (Id value = 0; value < getNumValues(variable); ++value) {
				Int delta = -current->coefficient;
				if (term != end && term->value == value) {
					delta += term->coefficient;
					++term;
				}
				setDiffPenalty(variable, value, max<Int>(0, excess + delta) - max<Int>(0, excess));
			}
		}
		else { // coefficient_{variable}{solutions[variable]} = 0
			for (; term != end; ++term) {
				setDiffPenalty(variable, term->value, max<Int>(0, excess + term->coefficient) - max<Int>(0, excess));
			}
		}
	}
	return max<Int>(0, excess);
}
