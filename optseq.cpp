#include "optseq.h"
#include "cputime.h"
#include <iostream>
#include <memory>
#include <csignal>
#include <fstream>
#include <cstdlib>
#include <cstring>

// ========== export functions (defined in iodata.cpp)==========
void inputData(OptSeq &optseq, istream &is = cin);
void outputData(OptSeq &optseq, ostream &os = cout);

// ========== halt ==========
void halt(int i){
	 cout << endl << "Interrupted." << endl;
	 exit(i);
}

// ========== main ==========
int 
main(int argc, char *argv[]){
	unique_ptr<OptSeq>optseq(OptSeq::makeOptSeq());
	//signal(SIGINT, halt);
	try{
		// options & program parameters
		Int randomSeed = optseq->getRandomSeed();
		double timeLimit = optseq->getTimeLimit();
		UInt iterationLimit = optseq->getIterationLimit();
		UInt neighborhoodSize = optseq->getNeighborhoodSize();
		UInt reportInterval = optseq->getReportInterval();
		UInt maxNumBacktracks = optseq->getMaxNumBacktracks();
		UInt tabuTenure = optseq->getTabuTenure();
		UInt weightControl = optseq->getWeightControl();
		bool printdata = false;
		for (int i = 1; i < argc; i++){
			if (!strcmp(argv[i], "-backtrack") && i+1 < argc){ optseq->setMaxNumBacktracks(max(0,atoi(argv[++i]))); }
			else if (!strcmp(argv[i], "-data")){ printdata = true; }
			else if (!strcmp(argv[i], "-initial") && i+1 < argc){ optseq->setInitialSolutionFile(argv[++i]); }
			else if (!strcmp(argv[i], "-iteration") && i+1 < argc){ optseq->setIterationLimit(max(0, atoi(argv[++i]))); }
			else if (!strcmp(argv[i], "-neighborhood") && i+1 < argc){ optseq->setNeighborhoodSize(max(0, atoi(argv[++i]))); }
			else if (!strcmp(argv[i], "-report") && i+1 < argc){ optseq->setReportInterval(max(0, atoi(argv[++i]))); }
			else if (!strcmp(argv[i], "-seed") && i+1 < argc){ optseq->setRandomSeed(atoi(argv[++i])); }
			else if (!strcmp(argv[i], "-tenure") && i+1 < argc){ optseq->setTabuTenure(atoi(argv[++i])); }
			else if (!strcmp(argv[i], "-time") && i+1 < argc){ optseq->setTimeLimit(max(0.0, atof(argv[++i]))); }
			else if (!strcmp(argv[i], "-weightcontrol") && i+1 < argc){ optseq->setWeightControl(max(0, atoi(argv[++i]))); }
			else{
				cout << "Usage: " << argv[0] << " [-options...]" << endl << endl
						 << "	-backtrack #			 set max number of backtrack (default: " << maxNumBacktracks << ")" << endl
						 << "	-data							print input data and terminate immediately" << endl
						 << "	-initial f				 set initial solution file (default: not specified)" << endl
						 << "	-iteration #			 set iteration limit (default: " << iterationLimit << ")" << endl
						 << "	-neighborhood #		set neighborhood size (default: " << neighborhoodSize << ")" << endl
						 << "	-report #					set report interval (default: " << reportInterval << ")" << endl
						 << "	-seed #						set random seed (default: " <<	 randomSeed << ")" << endl
						 << "	-tenure #					set tabu tenure (default: " <<	 tabuTenure << ")" << endl
						 << "	-time #						set cpu time limit in second (default: " << timeLimit << ")" << endl
						 << "	-weightcontrol #	 set weight control interval (default: " << weightControl << ")" << endl
  						 << endl << "# = number, f = file name" << endl;
				return 0;
			}
		}
		
		// input data	
		cout << "# reading data ..." << flush;
		CpuTime cputime;
	
		inputData(*optseq, cin);
		cout.setf(ios::fixed); cout.precision(2);
		cout << " done: " << cputime() << "(s)" << endl;

		if (optseq->getNumActivities() > 17){
			throw runtime_error("The number of activities is limited to 15");
		}

		// output data (optional)
		if (printdata){ 
			outputData(*optseq, cout);
			return 0;
		}

		// solve
		cout << "# random seed: " << optseq->getRandomSeed() << endl;
		cout << "# tabu tenure: " << optseq->getTabuTenure() << endl;
		cout << "# cpu time limit: " << optseq->getTimeLimit() << "(s)" << endl;
		cout << "# iteration limit: " << optseq->getIterationLimit() << endl;
		optseq->solve();
	}
	catch (std::bad_alloc){
	cout << "Memory overflow" << endl;
	}
	catch (runtime_error error){
		cout << endl << error.what() << endl;
	}
	cout << endl;
	if (optseq->isSolutionFeasible()){
		// print solution
		//cout << "--- best activity list ---" << endl;
		//for (Id i = OptSeq::source; i < optseq->getNumActivities(); i = optseq->getSolutionNextActivityId(i)){
		//	cout << optseq->getActivityName(i) << " ";
		//}
		//cout << endl << endl;
		cout << "--- best solution ---" << endl;
		vector<pair<Id, Int> > tardy;
		for (Id i = 0; i < optseq->getNumActivities(); i++){
			cout << optseq->getActivityName(i) << "," 
				<< (optseq->getActivityNumModes(i) > 1? optseq->getModeName(optseq->getSolutionModeId(i)) : "---") << ",";
			for (auto ptr = optseq->getSolutionExecutions(i); ptr; ptr = ptr->getNext()) {
				cout << " " << ptr->getFrom();
				if (ptr->getFrom() < ptr->getTo()) {
					cout << "--" << ptr->getTo();
					if (ptr->getKey() > 1) {
						cout << "[" << ptr->getKey() << "]";
					}
				}
				if (!ptr->getNext()) {
					if (optseq->getActivityBackward(i)) {
						if (optseq->getActivityDueDate(i) < Inf && optseq->getActivityDueDate(i) > ptr->getTo()) {
							tardy.emplace_back(i, ptr->getTo() - optseq->getActivityDueDate(i));
						}
					}
					else {
						if (optseq->getActivityDueDate(i) < ptr->getTo()) {
							tardy.emplace_back(i, ptr->getTo() - optseq->getActivityDueDate(i));
						}
					}
				}
			}
			cout << endl;
		}

		cout << "--- tardy activity ---" << endl;
		for (vector<pair<Id, Int> >::iterator ite = tardy.begin(); ite != tardy.end(); ++ite){
			cout << optseq->getActivityName(ite->first) << ": " << ite->second << endl;
		}

		cout << "--- resource residuals ---" << endl;
		for (Id r = 0; r < optseq->getNumResources(); r++){
			cout << optseq->getResourceName(r) << ": ";
			for (const OptSeq::Interval *ptr = optseq->getSolutionResourceResiduals(r); ptr; ptr = ptr->getNext()){
				cout << "[" << ptr->getFrom() << "," << ptr->getTo() << "] " << ptr->getKey() << " ";
			}
			cout << endl;
		}
		cout << endl;
		cout << "--- best activity list ---" << endl;
		for (Id i = SourceId; i < optseq->getNumActivities(); i = optseq->getSolutionNextActivityId(i)){
			cout << optseq->getActivityName(i) << " " << (optseq->getActivityNumModes(i) > 1? optseq->getModeName(optseq->getSolutionModeId(i)) : "---") << endl;
		}
		cout << endl;

		cout << "objective value = " << optseq->getSolutionObjective() << endl;
		cout << "cpu time = " << optseq->getSolutionCpuTime() << "/" << optseq->getCpuTime() << "(s)" << endl;
		cout << "iteration = " << optseq->getSolutionIteration() << "/" << optseq->getIteration() << endl;
	}
	else{
		cout << "no feasible schedule found." << endl;
	}

	//optseq.reset();
	//while (1);

}
