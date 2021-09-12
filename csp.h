#include <vector>
#include <memory>
#include <algorithm>

using namespace std;
// ==================== CSP ====================
class CSP {
public:
	using Int = long long ;
	using UInt = unsigned long long;
	using Id = unsigned long long; 
	using Solution = vector<UInt>;
	// ---------- class Constraint ----------
	class Constraint {
	public:
		Constraint(CSP* c) : csp(c) {
			id = csp->addConstraint(this);
		}
		virtual ~Constraint() {}
		virtual void preprocess() = 0;
		virtual Int calculate(const Solution&) = 0;
	protected:
		void labelsConcerned(Id variable, Id value) {
			csp->labelsConcerned(id, variable, value);
		}
		void setDiffPenalty(Id variable, Id value, Int diff) {
			csp->setDiffPenalty(id, variable, value, diff);
		}
		UInt getNumVariables() { return csp->getNumVariables(); }
		UInt getNumValues(Id variable) { return csp->getNumValues(variable); }
	private:
		CSP* const csp;
		Id id; // constant
	};

	// ---------- class linear ----------
	class Linear : public Constraint {
	public:
		Linear(CSP* c) : Constraint(c) {}
		~Linear() {}
		void setRhs(Int r) { rhs = r; }
		void addTerm(Int coefficient, Id variable, Id value) {
			terms.emplace_back(coefficient, variable, value);
		}
		void preprocess();
		Int calculate(const Solution&);
	private:
		Int rhs = 0;
		Int excess = 0;
		struct Term {
			Term(Int c, Id x, Id v) : coefficient(c), variable(x), value(v) {}
			Int coefficient;
			Id variable, value;
		};
		vector<Term> terms;
	};

	// ---------- constructor ----------
protected:
	CSP() {}
public:
	// ---------- destructor ----------
	virtual ~CSP() {}
	// ---------- virtual constructor (factory function) ----------
	static CSP* makeCSP(vector<Id>& numValues);

public:
	virtual void preprocess() = 0;
	virtual void resetFixedVariables() = 0;
	virtual void setFixedVariable(Id id) = 0;
	virtual UInt solve(Solution&) = 0;
	virtual UInt getIterationLimit() const = 0;
	virtual void setIterationLimit(UInt limit) = 0;
private:
	friend class Constraint;
	// functions invoked by Constraints
	virtual UInt getNumVariables() = 0;
	virtual UInt getNumValues(Id) = 0;
	virtual UInt addConstraint(Constraint*) = 0; // invoked by only the constructor of Constraint
	virtual void labelsConcerned(Id c, Id variable, Id value) = 0;
	virtual void setDiffPenalty(Id c, Id variable, Id value, Int diff) = 0;
};
