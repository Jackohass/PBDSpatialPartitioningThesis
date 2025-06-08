#include<stdlib.h>
#include<iostream>
#include<string>

using namespace std;

void runTests(string version, string input)
{
    string defaultCommand =  ".\\FluidParticleSpatialTest" + version + ".exe";
    string command = defaultCommand + " d" + " DefaultNum"+version+input;
    //cout << "Currently doing: default" << endl;
    //system(command.c_str());

    //for(int i = -2; i < 3; i++)
    //{
    //    string num = to_string(i);
    //    cout << "Currently doing: " << num << endl;
    //    command = defaultCommand + " " + num + " " + num+ "Num"+version+input;
    //    system(command.c_str());
    //}
    cout << "Currently doing: 1 million particles" << endl;
    command = defaultCommand + " m" + " MillionNum"+version+input;
    system(command.c_str());
}

int main()
{
    cout << "Write filename addition: ";
    string input;
    cin >> input;

    //runTests("DefaultTiming", input);
    runTests("FSPHTiming", input);
	/*runTests("FSPHCPUOPTTiming", input);
	runTests("HipNSearchTiming", input);
	runTests("HipNSearchStaticGridTiming", input);*/
	/*runTests("Default", input);
    runTests("FSPH", input);
	runTests("FSPHCPUOPT", input);
    runTests("HipNSearch", input);
	runTests("HipNSearchStaticGrid", input);*/

    return 0;
}