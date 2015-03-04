// ****************************************************************************************************
// *   DataReader
// *   Alexis Fagot
// *   23/01/2015
// ****************************************************************************************************

#include <iostream>
#include <cstdlib>
#include <fstream>
#include <ostream>
#include <sstream>
#include <cmath>
#include <vector>
#include <string>
#include <iomanip>
#include <map>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/io.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "../include/CAENVMElib.h"
#include "../include/CAENVMEoslib.h"
#include "../include/CAENVMEtypes.h"

#include "../include/v1190a.h"
#include "../include/v1718.h"
#include "../include/DataReader.h"
#include "../include/IniFile.h"

using namespace std;

// ****************************************************************************************************

DataReader::DataReader()
{
    this->StopFlag = false;
}

// ****************************************************************************************************

DataReader::~DataReader()
{

}

// ****************************************************************************************************

void DataReader::SetIniFile(string inifilename)
{
    this->iniFile = new IniFile(inifilename);
    this->iniFile->Read();
}

// ****************************************************************************************************

void DataReader::SetMaxTriggers()
{
    this->MaxTriggers = this->iniFile->intType("General","MaxTriggers",MAXTRIGGERS_V1190A);
}

// ****************************************************************************************************

Data32 DataReader::GetMaxTriggers()
{
    return this->MaxTriggers;
}

// ****************************************************************************************************

void DataReader::SetVME()
{
    this->VME = new v1718(this->iniFile);
}

// ****************************************************************************************************

void DataReader::SetTDC()
{
    this->TDC = new v1190a(this->VME->GetHandle(),this->iniFile);

    /*********** initialize the TDC 1190a ***************************/
    this->TDC->Set(this->iniFile);
}

// ****************************************************************************************************

void DataReader::Init(string inifilename)
{
    this->SetIniFile(inifilename);
    this->SetMaxTriggers();
    this->SetVME();
    this->SetTDC();
}

// ****************************************************************************************************

string DataReader::GetFileName(){
    string fNameParts[7];
    fNameParts[0] = this->iniFile->stringType("General","RunType","");
    fNameParts[1] = this->iniFile->stringType("General","ChamberType","");
    fNameParts[2] = this->iniFile->stringType("General","MaxTrigger","");
    fNameParts[3] = this->iniFile->stringType("General","TriggerType","");
    fNameParts[4] = this->iniFile->stringType("General","ElectronicsType","");
    fNameParts[5] = this->iniFile->stringType("General","Threshold","");
    fNameParts[6] = this->iniFile->stringType("General","ChamberType","");

    for(int i=0; i<7;i++)
        if(fNameParts[i] != "")
            fNameParts[i] += "_";

    time_t t = time(0);
    struct tm *Time = localtime(&t);
    int Y = Time->tm_year + 1900;
    int M = Time->tm_mon + 1;
    int D = Time->tm_mday;
    int h = Time->tm_hour;
    int m = Time->tm_min;
    int s = Time->tm_sec;

    stringstream fNameStream;
    fNameStream << "datarun/";                      //destination
    for(int i=0; i<7;i++)
        fNameStream << fNameParts[i];               //informations about chamber, trigger and electronics
    fNameStream << "run"
                << setfill('0') << setw(4) << Y     //run number
                << setfill('0') << setw(2) << M
                << setfill('0') << setw(2) << D
                << setfill('0') << setw(2) << h
                << setfill('0') << setw(2) << m
                << setfill('0') << setw(2) << s
                << ".dat";                          //file type

    string outputfName;
    fNameStream >> outputfName;

    return outputfName;
}

// ****************************************************************************************************

void DataReader::Run()
{
    cout << "Starting data acquisition." << endl;

    Uint TriggerCount = 0;
    string outputFileName = this->GetFileName();

    while(TriggerCount < this->GetMaxTriggers()){
        TriggerCount += this->TDC->Read(this->VME,outputFileName);
    }
}
