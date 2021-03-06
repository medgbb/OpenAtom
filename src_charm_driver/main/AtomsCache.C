//==============================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==============================================================================
/** \file AtomsCache.C
 *          Atom coordinate, forces, and iteration
 **/
//==============================================================================

#include "AtomsCache.h"
#include "energyGroup.h"
#include "eesCache.h"
#include "cp_state_ctrl/CP_State_GSpacePlane.h"
#include "utility/util.h"
#include "CPcharmParaInfoGrp.h"
#include "load_balance/IntMap.h"
#include "charm++.h"

#include <cmath>

//----------------------------------------------------------------------------
#define CHARM_ON
#include "src_piny_physics_v1.0/include/class_defs/Interface_ctrl.h"
#include "src_piny_physics_v1.0/include/class_defs/piny_constants.h"
#include "src_piny_physics_v1.0/include/class_defs/ATOM_OPERATIONS/class_atomintegrate.h"
#include "src_piny_physics_v1.0/include/class_defs/ATOM_OPERATIONS/class_atomoutput.h"
#include "src_piny_physics_v1.0/include/class_defs/CP_OPERATIONS/class_cprspaceion.h"

//----------------------------------------------------------------------------

extern CkVec <IntMap2on2> GSImaptable;
extern Config                      config;
extern CkVec <CProxy_CP_State_GSpacePlane> UgSpacePlaneProxy;
extern CkVec <CProxy_GSpaceDriver>         UgSpaceDriverProxy;
extern CkVec <CProxy_AtomsCache>             UatomsCacheProxy;
extern CkVec <CProxy_AtomsCompute>             UatomsComputeProxy;
extern CkVec <CProxy_EnergyGroup>          UegroupProxy;
extern CkVec <CProxy_StructFactCache>      UsfCacheProxy;
extern CkVec <CProxy_eesCache>             UeesCacheProxy;
extern CProxy_TemperController temperControllerProxy;
extern CProxy_InstanceController instControllerProxy;
extern CProxy_CPcharmParaInfoGrp   scProxy;

//#define _CP_DEBUG_ATMS_

//==============================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==============================================================================
/**
 *
 * @addtogroup Atoms
 * @{ 
 */
//==============================================================================
AtomsCache::AtomsCache( int _natm, int n_nl, Atom *a, UberCollection _thisInstance, std::string dirname) : natm(_natm), natm_nl(n_nl), thisInstance(_thisInstance), output_directory(dirname)
                                                                                      //==============================================================================
{// begin routine
  //==============================================================================

  pimdchaincount=0;
  fastAtoms.natm = natm;
  iteration=0;
  temperScreenFile = NULL;
  fastAtoms.q    = new double[natm];
  fastAtoms.x    = new double[natm];
  fastAtoms.y    = new double[natm];
  fastAtoms.z    = new double[natm];
  fastAtoms.fx   = new double[natm];
  fastAtoms.fy   = new double[natm];
  fastAtoms.fz   = new double[natm];
  fastAtoms.fxu   = new double[natm];
  fastAtoms.fyu   = new double[natm];
  fastAtoms.fzu   = new double[natm];
  for(int i=0;i<natm;i++){
    fastAtoms.q[i]  = a[i].q;
    fastAtoms.x[i]  = a[i].x;
    fastAtoms.y[i]  = a[i].y;
    fastAtoms.z[i]  = a[i].z;
    fastAtoms.fx[i] = a[i].fx;
    fastAtoms.fy[i] = a[i].fy;
    fastAtoms.fz[i] = a[i].fz;
#ifdef _NAN_CHECK_
    CkAssert(finite(fastAtoms.x[i]));
    CkAssert(finite(fastAtoms.y[i]));
    CkAssert(finite(fastAtoms.z[i]));
#endif
  }//endfor
  if(0 && (config.UberKmax>1 || config.UberImax>1 ))
  {
    // we will do the file output
    temperScreenFile = openScreenfWrite(output_directory.c_str(), "screen", thisInstance.idxU.z,thisInstance.idxU.x, true);
  }
  else
  {
    temperScreenFile = stdout;
  }

}




//==========================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==========================================================================
/** Contributeforces
 * Every proc assigned an electronic structure chare of the bead=i temper=j 
 * (i,j) uber instance that generates an atom force MUST be in (i,j) atom group 
 * and so contribute to the reduction below. Otherwise, all the pieces of the 
 * atom forces of (i,j) will not be collected and the user will be very sad!
 **/ 
//==========================================================================
void AtomsCache::contributeforces(){
  //==========================================================================

  int i,j;
  // collate all the forces that RS RHO NL deposited on the atoms
  double *ftot           = new double[(3*natm)];
  for(i=0,j=0; i<natm; i++,j+=3){
    ftot[j]   = fastAtoms.fx[i];
    ftot[j+1] = fastAtoms.fy[i];
    ftot[j+2] = fastAtoms.fz[i];
  }//endfor
#ifdef _CP_DEBUG_ATMS_
  int myid     = CkMyPe();
  CkPrintf("GJM_DBG: inside contribute forces %d : %d\n",myid,natm);
#endif
  CkCallback cb(CkIndex_AtomsCompute::recvContributeForces(NULL), UatomsComputeProxy[thisInstance.proxyOffset]);
  contribute((3*natm)*sizeof(double),ftot,CkReduction::sum_double,cb);
  delete [] ftot;
  zeroforces();
  //==========================================================================
}//end routine
//==========================================================================


//==========================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==========================================================================
/** acceptAtoms

 * AtomCompute gives us a coordinate update after force integration
 */
//==========================================================================
void AtomsCache::acceptAtoms(AtomMsg *msg)
{
  for(int atomI = msg->natmStr, j=0; atomI < msg->natmEnd ; atomI++ ,j+=9){
#ifdef _CP_DEBUG_ATMS_
    if(CkMyPe()==0)
    {
      CkPrintf("{%d} AtomPos[%d] updated to %.5g,%.5g,%.5g from %.5g,%.5g,%.5g\n",thisInstance.proxyOffset, atomI, msg->data[j],  msg->data[j+1],  msg->data[j+2], fastAtoms.x[atomI],  fastAtoms.y[atomI],  fastAtoms.z[atomI]);
    }
#endif
    fastAtoms.x[atomI]=msg->data[j];
    fastAtoms.y[atomI]=msg->data[j+1];
    fastAtoms.z[atomI]=msg->data[j+2];
#ifdef _NAN_CHECK_
    CkAssert(finite(fastAtoms.x[atomI]));
    CkAssert(finite(fastAtoms.y[atomI]));
    CkAssert(finite(fastAtoms.z[atomI]));
#endif

  } 
  zeroforces();
  int i=0;
  CkCallback cb(CkIndex_AtomsCache::atomsDone(NULL),UatomsCacheProxy[thisInstance.proxyOffset]);
  contribute(sizeof(int),&i,CkReduction::sum_int,cb);
  EnergyGroup *eg             = UegroupProxy[thisInstance.proxyOffset].ckLocalBranch();
  eg->estruct.potPIMDChain    = msg->data[(msg->nsize-4)];
  eg->estruct.eKinetic_atm    = msg->data[(msg->nsize-2)];
  eg->estruct.eKineticNhc_atm = msg->data[(msg->nsize-2)];
  eg->estruct.potNhc_atm      = msg->data[(msg->nsize-2)];

  delete msg;
}

//==========================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==========================================================================
/** AtomsDone
 *
 * everyone has the new coordinates in dynamics
 */
//==========================================================================
void AtomsCache::atomsDone(CkReductionMsg *msg)
{
  //    CkPrintf("{%d}[%d] AtomsCache::atomsDone(msg)\n ", thisInstance.proxyOffset, CkMyPe());     
  //==========================================================================
  delete msg;
  atomsDone();
}


//==========================================================================
// Needs to have each proc invoke directly doneMovingAtoms method of the
// GSpaceDrivers which are mapped to it. Without migration, we have that map
// at startup. With migration, one must write an enroll/dropout routine.
// All 
//==========================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==========================================================================
void AtomsCache::atomsDone() {
  //==========================================================================
  // Increment iteration counters
  //  CkPrintf("{%d}[%d] AtomsCache::atomsDone()\n ", thisInstance.proxyOffset, CkMyPe());     
  int myid = CkMyPe();

  EnergyGroup *eg             = UegroupProxy[thisInstance.proxyOffset].ckLocalBranch();
  iteration++;
  eg->estruct.iteration_atm   = iteration;
  eg->iteration_atm           = iteration;
  // CkPrintf("{%d}[%d] atomsDone now at eg->iteration_atm %d \n",thisInstance.proxyOffset,myid,eg->iteration_atm);

#ifdef _DEBUG_CHECK_ATOM_COMM_
  char fname[100];
  sprintf(fname,"atoms.out.%d.%d",iteration,myid);
  FILE *fp = fopen(fname,"w");
  for(int i=0;i<natm;i++){
    fprintf(fp,"%.10g %.10g %.10g\n",atoms[i].x,atoms[i].y,atoms[i].z);
  }//endfor
  fclose(fp);
#endif
  releaseGSP();

}


//==========================================================================
// Use eesCache's registered GSPs to 
//==========================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==========================================================================
void AtomsCache::releaseGSP() {
  int myid = CkMyPe();
  // CkPrintf("{%d}[%d] running release GSP\n",thisInstance.proxyOffset, myid);
  //==========================================================================
  // Use the cool new data caching system to say we're done.

  //each co-located k-point and spin needs to be handled
  eesCache *eesData = UeesCacheProxy[thisInstance.proxyOffset].ckLocalBranch ();
  int limit=eesData->gspKptSpinStatePlaneVec.length();
  for(int i=0; i<limit; i++)
    {
      UberCollection thisPoint=thisInstance;
      thisPoint.idxU.y=eesData->gspKptSpinStatePlaneVec[i].getKpoint();
      thisPoint.idxU.s=eesData->gspKptSpinStatePlaneVec[i].getSpin();
      thisPoint.setPO();
      int state=eesData->gspKptSpinStatePlaneVec[i].getState();
      int plane=eesData->gspKptSpinStatePlaneVec[i].getPlane();
      int iadd = UgSpacePlaneProxy[thisPoint.proxyOffset](state,plane).ckLocal()->registrationFlag;
      if(iadd!=1){
        CkPrintf("@@@@@@@@@@@@@@@@@@@@_error_@@@@@@@@@@@@@@@@@@@@\n");
        CkPrintf("atom : Bad registration cache flag on proc %d %d %d %d\n",
		 myid,iadd,state,plane);
        CkPrintf("@@@@@@@@@@@@@@@@@@@@_error_@@@@@@@@@@@@@@@@@@@@\n");
        CkExit();
      }//endif
      //       CkPrintf("{%d}[%d] AtomsCache::atomsDone() calling doneMovingAtoms\n ", thisInstance.proxyOffset, CkMyPe());     
      UgSpaceDriverProxy[thisPoint.proxyOffset](state,plane).doneMovingAtoms(iteration); 
    }//endfor
  //==============================================================================
}//end routine
//==============================================================================

//==============================================================================
//cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc
//==============================================================================
/** Destructor
 *
 *
 **/
//==============================================================================
AtomsCache::~AtomsCache(){ 
  delete [] fastAtoms.q;
  delete [] fastAtoms.x;
  delete [] fastAtoms.y;
  delete [] fastAtoms.z;
  delete [] fastAtoms.fx;
  delete [] fastAtoms.fy;
  delete [] fastAtoms.fz;
  delete [] fastAtoms.fxu;
  delete [] fastAtoms.fyu;
  delete [] fastAtoms.fzu;
}

void AtomsCache::acceptChainContribution(double PIMDChain){

  EnergyGroup *eg             = UegroupProxy[thisInstance.proxyOffset].ckLocalBranch();
  eg->estruct.totalpotPIMDChain+=PIMDChain;
  if(++pimdchaincount==config.UberImax-1)
    {
      fprintf(temperScreenFile,"Iter [%d] PIMD Chain  = %5.8lf\n",iteration, eg->estruct.totalpotPIMDChain);
      pimdchaincount=0;
    }
}


//=========================================
/*@}*/
