#include "charm++.h"
//#include "util.h"
#include "ENL_EKE_Collector.h"

// this stuff is in a C file because multiple includes of .def will
// give you too many implementations of this tiny thing for linktime joy
extern FILE *openScreenfWrite(const char *dirnameBase, const char *fname, int temper, int bead, bool beadfile);
ENL_EKE_Collector::ENL_EKE_Collector(int _numEnergyInputs, int _numInstance, int _beads, std::string output_directory)
{
  beads=_beads;
  if(0 && (_numInstance>1))
    temperScreenFile=openScreenfWrite(output_directory.c_str(), "screen", thisIndex, 0, false);
  else
    temperScreenFile=stdout;
  enlIteration=0; // having two of these is silly, but simple
  ekeIteration=0;
  energyExpected=_numEnergyInputs;
  ENL=EKE=0.0;
  countENL=countEKE=0;
}
void ENL_EKE_Collector::printENL()
{   

  fprintf(temperScreenFile,"[%d] ENL         = %5.8lf\n", enlIteration++, ENL/(double) energyExpected);   // tell the world
  ENL=0;
  countENL=0;
}
void ENL_EKE_Collector::printEKE()
{
  fprintf(temperScreenFile,"Iter [%d] EKE         = %5.8lf\n", ekeIteration++, EKE/(double) beads);
  countEKE=0;
  EKE=0;
}

void ENL_EKE_Collector::acceptENL(double _enl)
{
  countENL++; 
  ENL+=_enl;

  if(countENL==energyExpected) printENL();
}
void ENL_EKE_Collector::acceptEKE(double _eke)
{
  countEKE++; 
  EKE+=_eke;
  fprintf(temperScreenFile,"Iter [%d] this EKE         = %5.8lf contribution %d\n", ekeIteration++, _eke, countEKE);
  if(countEKE==energyExpected) printEKE();
}


#include "ENL_EKE_Collector.def.h"
