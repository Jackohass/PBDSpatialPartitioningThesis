#include<stdlib.h>
#include<iostream>
#include<string>

using namespace std;

void runTests(string version, string input)
{
    string defaultCommand =  ".\\FluidParticleSpatialTest" + version + ".exe";
    string command = defaultCommand + " d" + " DefaultNum"+input;
    cout << "Currently doing: default" << endl;
    system(command.c_str());

    for(int i = -2; i < 3; i++)
    {
        string num = to_string(i);
        cout << "Currently doing: " << num << endl;
        command = defaultCommand + " " + num + " " + num+ "Num"+input;
        system(command.c_str());
    }
}

int main()
{
    cout << "Write filename addition: ";
    string input;
    cin >> input;

    runTests("Default", input);
    runTests("FSPH", input);
    runTests("hipNSearch", input);

    return 0;
}