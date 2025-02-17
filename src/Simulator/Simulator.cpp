/**
 * @file SimulationDriver.cc
 * @brief This is the simulation driver which will call the necessary functions and make
 * the correct calls as and when required
 * @author Amit Jain
 * @date 2007-04-10
 */

#include "SimulationEngine.h"
#include "GlobalResource.h"
#include "SimulationRewardCollector.h"
#include <string>
#include <stdlib.h>
#include <sstream>
#include <fstream>
#include <ctime>

#include "CPTimer.h"

#define LUT_END_NAME "_sarsopData.bin"

#ifdef _MSC_VER
#else
//for timing
#include <sys/param.h>
#include <sys/types.h>
#include <sys/times.h>
//end for timing
#endif

#include "MOMDP.h"
#include "ParserSelector.h"
#include "AlphaVectorPolicy.h"

using namespace std;
using namespace momdp;

void print_usage(const char* cmdName) 
{
	cout << "Usage: " << cmdName << " POMDPModelFileName --policy-file policyFileName --simLen numberSteps \n" 
<<"	--simNum numberSimulations [--fast] [--srand randomSeed] [--output-file outputFileName]\n" 
<<"    or " << cmdName << " --help (or -h)  Print this help\n" 
<<"    or " << cmdName << " --version	  Print version information\n" 
<<"\n"
<<"Simulator options:\n"
<<"  --policy-file policyFileName	Use policyFileName as the policy file name (compulsory).\n"
<<"  --simLen numberSteps		Use numberSteps as the number of steps for each\n" 
<<"				simulation run (compulsory).\n"
<<"  --simNum numberSimulations	Use numberSimulations as the number of simulation runs\n" 
<<"				(compulsory).\n"
<<"  -f or --fast			Use fast (but very picky) alternate parser for .pomdp files.\n"
<<"  --srand randomSeed		Set randomSeed as the random seed for simulation.\n" 
<<"				It is the current time by default.\n"
//<<"  --lookahead yes/no		Set 'yes' ('no') to select action with (without) one-step\n" 
//<<"				look ahead. Action selection is with one-step look ahead\n" 
//<<"				by default.\n" 
<<"\n"
<<"Output options:\n"
<<"  --output-file outputFileName	Use outputFileName as the name for the output file\n" 
<<"				that contains the simulation trace.\n"
		<< "Example:\n"
		<< "  " << cmdName << " --simLen 100 --simNum 100 --policy-file out.policy Hallway.pomdp\n";

// 	cout << "usage: binary [options] problem:\n"
// 		<< "--help, print this message\n"
// 		<< "--policy-file, policy file to be used\n"
// 		<< "--output-file, output file to be used\n"
// 		<< "--simLen, length of simulation\n"
// 		<< "--simNum, number of simulations\n"
// 		<< "--srand, random seed (default: current time)\n"
// 		<< "--lookahead, use \"one-step look ahead\" when selecting action (default: yes)\n"
// 		<< "Examples:\n"
// 		<< " ./simulate --simLen 100 --simNum 100 --policy-file out.policy Hallway.pomdp\n";

}

void generateSimLog(SolverParams& p, double& globalExpRew, double& confInterval)
{
    int length;
    char str1[102];
    string str_comb;

    int startpos = 0;
    int i;
    for (i = p.problemName.length() - 1; i >= 0; i--) {
        if (p.problemName[i] == '/') {
            startpos = i + 1;
            break;
        }
    }

    str_comb.append(p.problemName.begin() + startpos, p.problemName.end());

    str_comb.append("SimLog");
    cout << str_comb << endl;

    length = str_comb.copy(str1, 100);
    str1[length] = '\0';

    FILE *fp = fopen(str1, "a");

    //  FILE *fp = fopen("sim.log","a");
    if (fp == NULL) 
    {
        cerr << "cant open sim logfile\n";
        exit(1);
    }

    fprintf(fp, "%f ", globalExpRew);
    fprintf(fp, "%f ", globalExpRew - confInterval);
    fprintf(fp, "%f ", globalExpRew + confInterval);
    fprintf(fp, "\n");
    fclose(fp);


}

std::pair<int, double> FindActionReward(std::vector<std::pair<int, double>> & preferredAction) // NATAN CHANGES
{
	int action = preferredAction[0].first;
	double avgReward = 0.0;

	for (auto v : preferredAction)
	{
		assert(action == v.first);
		avgReward += v.second;
	}

	avgReward /= preferredAction.size();

	return std::make_pair(action, avgReward);
}

void Avg(std::vector<std::vector<double>> rewardsVec, int numActions, std::vector<double> results)
{
	for (int a = 0; a < numActions; ++a)
	{
		double sum = 0.0;
		for (int sim = 0; sim < rewardsVec.size(); ++sim)
		{
			sum += rewardsVec[sim][a];
		}
	
		results[a] = sum / rewardsVec.size();
	}
}

int main(int argc, char **argv) 
{

    try
    {
        SolverParams* p = &GlobalResource::getInstance()->solverParams;

        bool parseCorrect = SolverParams::parseCommandLineOption(argc, argv, *p);
        if(!parseCorrect)
        {
            print_usage(p->cmdName); 
            exit(EXIT_FAILURE);
        }

        //check validity of options
        if (p->policyFile == "" || p->simLen == -1 || p->simNum == -1) 
        {
            print_usage(p->cmdName); 
            return 0;
        }


        bool enableFiling = false;
        if (p->outputFile.length() == 0) 
        {
            enableFiling = false;
        } 
        else 
        {
            enableFiling = true;
        }

	cout << "\nLoading the model ..." << endl << "  ";
        SharedPointer<MOMDP> problem = ParserSelector::loadProblem(p->problemName, *p);

        if(p->stateMapFile.length() > 0 )
        {
            // generate unobserved state to variable value map
            ofstream mapFile(p->stateMapFile.c_str());
            for(int i = 0 ; i < problem->YStates->size() ; i ++)
            {
                mapFile << "State : " << i <<  endl;
                map<string, string> obsState = problem->getFactoredUnobservedStatesSymbols(i);
                for(map<string, string>::iterator iter = obsState.begin() ; iter != obsState.end() ; iter ++)
                {
                    mapFile << iter->first << " : " << iter->second << endl ;
                }
            }
            mapFile.close();
        }

        SharedPointer<AlphaVectorPolicy> policy = new AlphaVectorPolicy(problem);

	cout << "\nLoading the policy ..." << endl;
	cout << "  input file   : " << p->policyFile << endl;
        bool policyRead = policy->readFromFile(p->policyFile);
        if(!policyRead)
        {
            return 0;
        }


	cout << "\nSimulating ..." << endl;
	
        if(p->useLookahead)
        {
            cout << "  action selection :  one-step look ahead" << endl;
        }
        else
        {
        }

        ofstream * foutStream = NULL;
        srand(p->seed);//Seed for random number.  Xan
        //cout << p->seed << endl;



        //MOVED SYLTAG
        // if the bvec field of problem.initialBeliefStval is not specified
        SharedPointer<BeliefWithState> startBeliefStval (new BeliefWithState());

        copy(*startBeliefStval->bvec, *problem->initialBeliefStval->bvec);
        startBeliefStval->sval = problem->initialBeliefStval->sval;

        belief_vector startBel;
        copy(startBel, *startBeliefStval->bvec);

        //belief_vector startBel = problem.initialBelief;
        if (startBel.filled() == 0) 
        {
            throw runtime_error("startBel.filled() == 0 !?");
            int numStates = problem->getBeliefSize();
            startBel.resize(numStates);
            for (int i = 0; i < numStates; i++) 
            {
                startBel.push_back(i, ((double) 1) / (double(numStates)));
            }
            copy(*startBeliefStval->bvec, startBel);
        }

        //ADD SYLTAG
        belief_vector startBeliefX;
        // check if startBeliefStval->sval is specified or is it random start value for X
        if (startBeliefStval->sval == -1) 
        { 
            // random start value for X
            copy(startBeliefX, *problem->initialBeliefX);
        } 
        else 
        { // for completeness we have a valid startBeliefX
            startBeliefX.resize(problem->XStates->size());
            startBeliefX.push_back(startBeliefStval->sval, 1.0);
        }

		// natan changes numStates
		int numStates = problem->isPOMDPTerminalState[0].size();
		int numActions = problem->getNumActions();

        //CPTimer simTimer;
        if (enableFiling) 
        {
            foutStream = new ofstream(p->outputFile.c_str());
        }
		std::vector<std::vector<double>> stateActionReward(numStates - 2);
		//std::vector<std::pair<int, double>> stateActionReward;
		for (int s = 0; s < numStates - 2; ++s)
		{
			//SimulationRewardCollector rewardCollector;
			//rewardCollector.setup(*p);

			startBeliefStval->bvec->data[0].index = s;
			belief_vector startBeliefTest;
			startBeliefTest.resize(problem->XStates->size());
			startBeliefTest.push_back(startBeliefStval->sval, 1.0);

			stateActionReward[s].resize(numActions);

			SimulationEngine engine;
			engine.setup(problem, policy, p);

			//double reward = 0, expReward = 0;

			// get reward vec for all actions
			// normal run - call runFor
			engine.GetActionRewardVec(p->simLen, *startBeliefStval, startBeliefTest, stateActionReward[s]);

			//rewardCollector.addEntry(currSim, reward, expReward);

			std::cout << "finished simulating state " << s << "\n";
			//rewardCollector.printFinalReward();
		}

		// NATAN CHANGES - save to file
		std::string lutName(p->problemName);
		// pop ".pomdp"
		lutName.pop_back();
		lutName.pop_back();
		lutName.pop_back();
		lutName.pop_back();
		lutName.pop_back();
		lutName.pop_back();

		lutName += LUT_END_NAME;
		std::ofstream lut(lutName, std::ios::out | std::ios::binary);
		int size = stateActionReward.size();
		lut.write((char *)&size, sizeof(int));
		lut.write((char *)&numActions, sizeof(int));
		for (int s = 0; s < stateActionReward.size(); ++s)
		{
			for (int a = 0; a < numActions; ++a)
			{
				lut.write((char *)&stateActionReward[s][a], sizeof(double));
			}
		}
    }

    catch(bad_alloc &e)
    {
        if(GlobalResource::getInstance()->solverParams.memoryLimit == 0)
        {
            cout << "Memory allocation failed. Exit." << endl;
        }
        else
        {
            cout << "Memory limit reached. Please try increase memory limit" << endl;
        }
    }
    catch(exception &e)
    {
        cout << "Exception: " << e.what() << endl ;
    }
}

