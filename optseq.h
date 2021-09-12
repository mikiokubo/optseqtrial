#include <string>
#include <vector>
#include <limits>
#include <stdexcept>
using namespace std;

// ==================== OptSeq ====================
namespace OPTSEQ {
	using Int = long long;
	using UInt = unsigned long long;
	using Id = unsigned long long;
	using LocalId = Id;
	// ----- infinity -----
	constexpr const Int Inf = numeric_limits<Int>::max() / 2;
	// ----- source & sink -----
	constexpr Id SourceId = 0;
	constexpr Id SinkId = 1;

	enum class StateType { VALUE, DIFF };
	enum class TempConstraintType { SS, SC, CC, CS };

	class OptSeq {
	public:
		// ----- class -----
		// --- OptSeq::Interval ---
		class Interval {
		public:
			Interval() {}
			virtual ~Interval() {}
			virtual Int getFrom() const = 0;
			virtual Int getTo() const = 0;
			virtual Int getKey() const = 0;
			virtual const Interval* getPrev() const = 0;
			virtual const Interval* getNext() const = 0;
		};

		// ----- constructors, operator= -----
	protected:
		OptSeq() {}
		OptSeq(const OptSeq&) = delete;
		OptSeq& operator=(const OptSeq&) = delete;

	public:
		// ----- destructor -----
		virtual ~OptSeq() {}
		// ----- virtual constructor (factory function) -----
		static OptSeq* makeOptSeq();

		// ----- resources -----
		virtual Id newResource(const string&) = 0;
		virtual UInt getNumResources() const = 0;
		virtual Id getResourceId(const string&) const = 0;
		virtual const string& getResourceName(Id) const = 0;

		virtual void setResourceCapacities(Id, Int value, Int from = 1, Int to = Inf) = 0;
		virtual void addResourceCapacities(Id, Int value, Int from = 1, Int to = Inf) = 0;
		virtual const Interval* getResourceCapacities(Id) const = 0;

		// ----- state -----
		// state value must be non-negative
		virtual Id newState(const string&) = 0;
		virtual UInt getNumStates() const = 0;
		virtual Id getStateId(const string&) const = 0;
		virtual const string& getStateName(Id) const = 0;

		virtual void setStateValue(Id, UInt value, Int time) = 0;
		virtual const vector<pair<Int, Int> >& getStateValues(Id) const = 0;
		//virtual void setInitialValue(OSInt time, OSInt value) = 0;
		//virtual const OSInterval* getInitialValues() const = 0;


		//----- modes -----
		virtual Id newMode(const string&, Int duration) = 0;
		virtual UInt getNumModes() const = 0;
		virtual Id getModeId(const string&) const = 0;
		virtual const string& getModeName(Id) const = 0;

		virtual Int getModeDuration(Id) const = 0;
		virtual void setModeMaxBreakDurations(Id, Int maxduration, Int from = 0, Int to = Inf) = 0;
		virtual void setModeMaxNumsParallel(Id, Int num, Int from = 1, Int to = Inf) = 0;
		virtual const Interval* getModeMaxBreakDurations(Id) const = 0;
		virtual const Interval* getModeMaxNumsParallel(Id) const = 0;

		virtual LocalId setModeRequirements(Id, Id resource, Int value, Int from = 0, Int to = Inf,
			bool forBreak = false, bool maximum = false) = 0;
		virtual UInt getModeNumRequiredResources(Id) const = 0;
		virtual Id getModeRequiredResourceId(Id, LocalId) const = 0;
		virtual const Interval* getModeRequirements(Id, LocalId, bool forBreak = false, bool maximum = false) const = 0;

		virtual LocalId setModeState(Id mid, Id sid, Int from, Int to) = 0;
		//	 from: non-negative
		//	 to: non-negative, or negative; in the latter case, the value remains the same

		virtual UInt getModeNumStates(Id mid) const = 0;
		virtual Id getModeStateId(Id mid, LocalId lid) const = 0;
		virtual const vector<Int>& getModeState(Id mid, LocalId lid) const = 0; // .[x] < 0: transition from x is not allowed

		// ----- activities -----
		virtual Id newActivity(const string&) = 0;
		virtual UInt getNumActivities() const = 0;
		virtual Id getActivityId(const string&) const = 0;
		virtual const string& getActivityName(Id) const = 0;

		virtual LocalId addActivityMode(Id activity, Id mode) = 0;
		virtual UInt getActivityNumModes(Id) const = 0;
		virtual Id getActivityModeId(Id, LocalId) const = 0;

		virtual void setActivityBackward(Id, bool) = 0;
		virtual bool getActivityBackward(Id) = 0;
		virtual void setActivityDueDate(Id, Int, bool finish = true) = 0;
		virtual Int getActivityDueDate(Id, bool finish = true) const = 0;
		virtual void setActivityDueDateQuad(Id, bool, bool finish = true) = 0;
		virtual bool getActivityDueDateQuad(Id, bool finish = true) const = 0;
		virtual void setActivityDueDateWeight(Id, Int, bool finish = true) = 0;
		virtual Int getActivityDueDateWeight(Id, bool finish = true) const = 0;

			virtual void setActivityAutoSelect(Id, Int) = 0;
		virtual Int getActivityAutoSelect(Id) const = 0;
		// autoselect	0: Mode is unchanged during the list-scheduling
		//						 1: All modes are always tested (The one with the earliest start time is adopted)
		//						 2: 1 + anther mode is tried if backtrack is required	
		//						 3: The first valid mode is adopted

		virtual void setActivityDependence(Id, Id) = 0;
		virtual UInt getActivityNumDependences(Id) = 0;
		virtual Id getActivityDependence(Id, LocalId) = 0;

		// ----- temporal constraints: s_j (or c_j) - s_i (or c_i) >= delay -----
		virtual Id newTempConstraint(Id pred, Id succ, TempConstraintType = TempConstraintType::CS, 
			Int delay = 0, Id pmode = Inf, Id smode = Inf) = 0;
		virtual UInt getNumTempConstraints() const = 0;
		virtual TempConstraintType getTempConstraintType(Id) const = 0;
		virtual Id getTempConstraintPredId(Id) const = 0;
		virtual Id getTempConstraintSuccId(Id) const = 0;
		virtual Id getTempConstraintPredModeId(Id) const = 0;  // return Inf if no mode specified
		virtual Id getTempConstraintSuccModeId(Id) const = 0;  // return Inf if no mode specified
		virtual Int getTempConstraintDelay(Id) const = 0;

		// -----	non-renewable resource constraint: \sum_{im} c_{im} x_{im} <= rhs -----
		virtual Id newNrrConstraint(Int weight = Inf) = 0;
		virtual UInt getNumNrrConstraints() const = 0;
		virtual Int getNrrConstraintWeight(Id) const = 0;
		virtual void setNrrConstraintRhs(Id, Int) = 0;
		virtual void addNrrConstraintTerm(Id, Int coefficient, Id activity, Id mode) = 0;
		virtual Int getNrrConstraintRhs(Id) const = 0;
		virtual UInt getNrrConstraintNumTerms(Id) const = 0;
		virtual Int getNrrConstraintCoefficient(Id, Id term) const = 0;
		virtual Id getNrrConstraintActivityId(Id, Id term) const = 0;
		virtual Id getNrrConstraintModeId(Id, Id term) const = 0;

		// ----- parameters -----
		virtual void setRandomSeed(Int) = 0;
		virtual void setTimeLimit(double) = 0;
		virtual void setIterationLimit(UInt) = 0;
		virtual void setInitialSolutionFile(string) = 0;
		virtual void setNeighborhoodSize(UInt) = 0;
		virtual void setReportInterval(UInt) = 0;
		virtual void setMaxNumBacktracks(UInt) = 0;
		virtual void setTabuTenure(UInt) = 0;
		virtual void setWeightControl(UInt) = 0;

		virtual Int getRandomSeed() const = 0;
		virtual double getTimeLimit() const = 0;
		virtual UInt getIterationLimit() const = 0;
		virtual UInt getNeighborhoodSize() const = 0;
		virtual UInt getReportInterval() const = 0;
		virtual UInt getMaxNumBacktracks() const = 0;
		virtual UInt getTabuTenure() const = 0;
		virtual UInt getWeightControl() const = 0;

		// ----- solve -----
		virtual void solve() = 0;
		virtual double getCpuTime() const = 0;
		virtual UInt getIteration() const = 0;

		// ----- best solution -----
		virtual Int getSolutionObjective() const = 0;
		virtual bool isSolutionFeasible() const = 0;
		virtual double getSolutionCpuTime() const = 0;
		virtual UInt getSolutionIteration() const = 0;
		virtual Id getSolutionModeId(Id) const = 0;
		virtual const Interval* getSolutionExecutions(Id) const = 0;
		virtual const Interval* getSolutionResourceResiduals(Id) const = 0;
		virtual Id getSolutionNextActivityId(Id) const = 0;
	};
}
using namespace OPTSEQ;