// ****************************************************************************************************
// *   DataReader
// *   Alexis Fagot
// *   23/01/2015
// ****************************************************************************************************

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <unistd.h>

#include "TH1I.h"
#include "TH1D.h"

#include "../include/DataReader.h"
#include "../include/MsgSvc.h"
#include "../include/utils.h"

using namespace std;

// ****************************************************************************************************

DataReader::DataReader(){
    //Initialisation of the RAWData vectors
    TDCData.EventList = new vector<int>;
    TDCData.NHitsList = new vector<int>;
    TDCData.ChannelList = new vector< vector<int> >;
    TDCData.TimeStampList = new vector< vector<float> >;

    //Cleaning all the vectors
    TDCData.EventList->clear();
    TDCData.NHitsList->clear();
    TDCData.ChannelList->clear();
    TDCData.TimeStampList->clear();

    StopFlag = false;
}

// ****************************************************************************************************

DataReader::~DataReader(){

}

// ****************************************************************************************************

void DataReader::SetIniFile(string inifilename){
    iniFile = new IniFile(inifilename);
    iniFile->Read();
}

// ****************************************************************************************************

void DataReader::SetMaxTriggers(){
    MaxTriggers = iniFile->intType("General","MaxTriggers",MAXTRIGGERS_V1190A);
}

// ****************************************************************************************************

Data32 DataReader::GetMaxTriggers(){
    return MaxTriggers;
}

// ****************************************************************************************************

void DataReader::SetVME(){
    VME = new v1718(iniFile);
}

// ****************************************************************************************************

void DataReader::SetTDC(){
    nTDCs = iniFile->intType("General","Tdcs",MINNTDC);
    TDCs = new v1190a(VME->GetHandle(),iniFile,nTDCs);

    /*********** initialize the TDC 1190a ***************************/
    TDCs->Set(iniFile,nTDCs);
}

// ****************************************************************************************************

void DataReader::Init(string inifilename){
    SetIniFile(inifilename);
    SetMaxTriggers();
    SetVME();
    SetTDC();
}

// ****************************************************************************************************

void DataReader::Update(){
    iniFile->Read();
    SetMaxTriggers();
}

// ****************************************************************************************************

string DataReader::GetFileName(){
    //Get the scan number from the ini file
    int ScanNumber = iniFile->intType("General","ScanID",999999);

    //Create the folder to contain the data file is it doesn't exist yet
    string datafolder = __datapath + iniFile->stringType("General","ScanID","") + "/";
    string mkdirScanFolder = "mkdir -p " + datafolder;
    system(mkdirScanFolder.c_str());

    //Get the run number (start time of the run)
    int RunNumber = GetTimeStamp();

    //use a stream to construct the name with the different variable types
    stringstream fNameStream;

    fNameStream << datafolder                   //destination
                << "Scan"
                << ScanNumber                   //scan ID
                << "_Run"
                << RunNumber                    //run number
                << ".root";                     //extension

    string outputfName;
    fNameStream >> outputfName;

    return outputfName;
}

// ****************************************************************************************************

void DataReader::Run(){
    //Get the output file name and create the ROOT file
    Uint TriggerCount = 0;
    string outputFileName = GetFileName();
    int startstamp = GetTimeStamp();

    TFile *outputFile = new TFile(outputFileName.c_str(), "recreate");

    //Create the data tree where the data will be saved
    //For each entry will be saved the event tag, the number of hits recorded
    //in the TDCs, the list of fired TDC channels and the time stamps of the
    //hits.
    TTree *RAWDataTree = new TTree("RAWData","RAWData");

    int               EventCount = -9;  //Event tag
    int               nHits = -8;       //Number of fired TDC channels in event
    vector<int>       TDCCh;            //List of fired TDC channels in event
    vector<float>     TDCTS;            //list of fired TDC channels time stamps

    TDCCh.clear();
    TDCTS.clear();

    //Set the branches that will contain the previously defined variables
    RAWDataTree->Branch("EventNumber",    &EventCount,  "EventNumber/I");
    RAWDataTree->Branch("number_of_hits", &nHits,       "number_of_hits/I");
    RAWDataTree->Branch("TDC_channel",    &TDCCh);
    RAWDataTree->Branch("TDC_TimeStamp",  &TDCTS);

    //Cleaning all the vectors that wi9ll contain the data
    TDCData.EventList->clear();
    TDCData.NHitsList->clear();
    TDCData.ChannelList->clear();
    TDCData.TimeStampList->clear();

    //Clear all the buffers and start data taking
    VME->SendBUSY(ON);
    TDCs->Clear(nTDCs);
    VME->SendBUSY(OFF);

    //Print log message
    MSG_INFO("[DAQ] Run "+outputFileName+" started");
    MSG_INFO("[DAQ] Run "+outputFileName+" 0%");

    int percentage = 0;     // percentage of the run done
    int last_print = 0;     // keep track of the last percentage printed

    //Every 20 seconds read the run file to check for a KILL command
    Uint checkKill = 0;

    //Read the output buffer until the min number of trigger is achieved
    while(TriggerCount < GetMaxTriggers()){
        usleep(200000);

        if(VME->CheckIRQ()){
            //Stop data acquisition with BUSY as VETO
            VME->SendBUSY(ON);
            usleep(1000);

            //Read the data
            TriggerCount = TDCs->Read(&TDCData,nTDCs);

            //percentage update
            percentage = (100*TriggerCount) / GetMaxTriggers();

            //dump the status in the logfile every 10%
            if(percentage % 10 == 0 && percentage != last_print){
                string log_percent = intTostring(percentage);

                MSG_INFO("[DAQ] "+log_percent+"%");
                last_print = percentage;
            }

            //Resume data taking
            VME->SendBUSY(OFF);
        }

        checkKill++;
        string runStatus = "";

        //check inside the run file for a KILL command every 10s
        if(checkKill == 50){
            runStatus = GetRunStatus();
            if(CtrlRunStatus(runStatus) == FATAL){
                MSG_FATAL("[DAQ-FATAL] KILL command received");
                MSG_FATAL("[DAQ-FATAL] Safely close current data file and exit");

                //Write the data from the RAWData structure to the TTree
                for(Uint i=0; i<TDCData.EventList->size(); i++){
                    EventCount  = TDCData.EventList->at(i);
                    nHits       = TDCData.NHitsList->at(i);
                    TDCCh       = TDCData.ChannelList->at(i);
                    TDCTS       = TDCData.TimeStampList->at(i);

                    RAWDataTree->Fill();
                }

                RAWDataTree->Print();
                outputFile = RAWDataTree->GetCurrentFile();

                delete RAWDataTree;

                outputFile->Close();

                delete outputFile;

                exit(EXIT_FAILURE);
            }
            checkKill = 0;
        }
    }

    //Write the data from the RAWData structure to the TTree
    for(Uint i=0; i<TDCData.EventList->size(); i++){
        EventCount  = TDCData.EventList->at(i);
        nHits       = TDCData.NHitsList->at(i);
        TDCCh       = TDCData.ChannelList->at(i);
        TDCTS       = TDCData.TimeStampList->at(i);

        RAWDataTree->Fill();
    }

    RAWDataTree->Print();
    outputFile = RAWDataTree->GetCurrentFile();

    delete RAWDataTree;

    //Create the parameters TTree
    //For each run, the HVeff, Threshold, mean HVmon, Imon and environmental
    //parameters values, beam and source status, Attenuator settings, Run type,
    //Scan ID, Number of triggers, start and stop time stamps are saved
    TTree *RunParameters = new TTree("RunParameters","RunParameters");

    //First read the parameters inside the config file
    TH1D *ID        = new TH1D("ID","Identifiers of this run",3,0,3);
    TH1I *Att       = new TH1I("Attenuators","Attenuators settings for this run",2,0,2);
    TH1I *Trig      = new TH1I("Triggers","Number of triggers for this run",1,0,1);

    TH1I *HVeffs    = new TH1I("HVeffs","List of HVeff applied per chamber during this run",3,0,3);
    HVeffs->SetCanExtend(TH1::kAllAxes);
    TH1I *Thrs      = new TH1I("HVeffs","List of thresholds used per chamber during this run",3,0,3);
    Thrs->SetCanExtend(TH1::kAllAxes);

    string group;
    string Parameter;
    string RPClabel;
    int value = 0;

    TString runtype;
    TString beamstatus;
    TString sourcestatus;
    TString electronics;

    //Branches from the config file
    RunParameters->Branch("ScanID", &ID);
    RunParameters->Branch("RunType", &runtype);
    RunParameters->Branch("MaxTriggers", &Trig);
    RunParameters->Branch("Beam", &beamstatus);
    RunParameters->Branch("Source", &sourcestatus);
    RunParameters->Branch("Attenuators", &Att);
    RunParameters->Branch("ElectronicsType", &electronics);
    RunParameters->Branch("HVeff", &HVeffs);
    RunParameters->Branch("Threshold", &Thrs);

    //Now fill the configuration parameters
    IniFileData inidata = iniFile->GetFileData();

    for(IniFileDataIter Iter = inidata.begin(); Iter!= inidata.end(); Iter++){
        size_t separator_pos = Iter->first.find_first_of('.');
        if (separator_pos == string::npos)
            continue;

        group = Iter->first.substr(0, separator_pos);

        if(group == "General"){
            Parameter = Iter->first.substr(separator_pos+1);
            if(Parameter == "ScanID"){
                value = iniFile->intType(group,Parameter,0);
                ID->Fill(Parameter.c_str(),value);
                ID->Fill("Start stamp",startstamp);
                ID->Fill("Stop stamp", GetTimeStamp());
            } if(Parameter == "RunType"){
                runtype = iniFile->stringType(group,Parameter,"");
            } else if(Parameter == "MaxTriggers"){
                value = iniFile->intType(group,Parameter,0);
                Trig->Fill(Parameter.c_str(),value);
            } else if(Parameter == "Beam"){
                beamstatus = iniFile->stringType(group,Parameter,"");
            } else if(Parameter == "Source"){
                sourcestatus = iniFile->stringType(group,Parameter,"");
            } else if(Parameter == "AttU" || Parameter == "AttD"){
                value = iniFile->intType(group,Parameter,0);
                Att->Fill(Parameter.c_str(),value);
            } else if(Parameter == "ElectronicsType"){
                electronics = iniFile->stringType(group,Parameter,"");
            }
        } else if(group == "HighVoltage"){
            RPClabel = Iter->first.substr(separator_pos+1);
            value = iniFile->intType(group,RPClabel,0);
            HVeffs->Fill(RPClabel.c_str(),value);
        } else if (group == "Threshold"){
            RPClabel = Iter->first.substr(separator_pos+1);
            value = iniFile->intType(group,RPClabel,0);
            Thrs->Fill(RPClabel.c_str(),value);
        }
    }

    //Then make histograms for the monitored parameters
    //These parameters are continuously stored into a file every 10 seconds
    //The first line is the number of parameters saved in the file
    //The second line is the header of each column corresponding to a parameter
    //Then the rest of the file are the monitored parameters
    //The number of parameters can change from 1 run to another
    //It is needed to create a vector that will be filled with histograms for each parameters
    ifstream MonFile(__parampath.c_str(), ios::in);

    if(MonFile){
        //vector that will contain the parameter histograms
        vector<TH1D *> Monitor;
        Monitor.clear();

        //read the number of monitored parameters in the file
        int nParam = 0;
        MonFile >> nParam;

        for(int p = 0; p < nParam; p++){
            //Save the names od the parameters and use them to initialise
            //a histogram that will fill the previously created vector
            string nameParam;
            MonFile >> nameParam;

            TH1D* Histo = new TH1D(nameParam.c_str(),nameParam.c_str(),1,0,1);
            Histo->SetCanExtend(TH1::kAllAxes);

            Monitor.push_back(Histo);

            //Branches for the monitored parameters
            RunParameters->Branch(nameParam.c_str(), &Monitor[p]);

            delete Histo;
        }

        //Start the loop over the values of the monitored parameters
        double paramValue = 0.;

        while(MonFile.good()){
            for(int p = 0; p < nParam; p++){
                MonFile >> paramValue;
                Monitor[p]->Fill(paramValue);
            }
        }
    }

    RunParameters->Fill();

    RunParameters->Print();
    outputFile = RunParameters->GetCurrentFile();

    outputFile->Write(0, TObject::kWriteDelete);
    outputFile->Close();

    delete outputFile;
}
