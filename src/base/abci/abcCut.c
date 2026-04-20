/**CFile****************************************************************

  FileName    [abcCut.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [Network and node package.]

  Synopsis    [Interface to cut computation.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: abcCut.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "base/abc/abc.h"
#include "opt/cut/cut.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

static void Abc_NtkPrintCuts( void * p, Abc_Ntk_t * pNtk, int fSeq );
static void Abc_NtkPrintCutsCsv( void * p, Abc_Ntk_t * pNtk, int fSeq );
static int  Abc_NtkComputeCutVolume( Abc_Ntk_t * pNtk, int iRoot, int * pLeaves, int nLeaves );
static int  Abc_NtkComputeCutHeight( Abc_Ntk_t * pNtk, int iRoot, int * pLeaves, int nLeaves );
static void Abc_NtkPrintCuts_( void * p, Abc_Ntk_t * pNtk, int fSeq );

extern int nTotal, nGood, nEqual;

static Vec_Int_t * Abc_NtkGetNodeAttributes( Abc_Ntk_t * pNtk );
static int Abc_NtkComputeArea( Abc_Ntk_t * pNtk, Cut_Man_t * p );

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkCutsSubtractFanunt( Abc_Ntk_t * pNtk )
{
    Abc_Obj_t * pObj, * pFan0, * pFan1, * pFanC;
    int i, Counter = 0;
    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        if ( !Abc_NodeIsMuxType(pObj) )
            continue;
        pFanC = Abc_NodeRecognizeMux( pObj, &pFan1, &pFan0 );
        pFanC = Abc_ObjRegular(pFanC);
        pFan0 = Abc_ObjRegular(pFan0);
        assert( pFanC->vFanouts.nSize > 1 );
        pFanC->vFanouts.nSize--;
        Counter++;
        if ( Abc_NodeIsExorType(pObj) )
        {
            assert( pFan0->vFanouts.nSize > 1 );
            pFan0->vFanouts.nSize--;
            Counter++;
        }
    }
    printf("Subtracted %d fanouts\n", Counter );
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkCutsAddFanunt( Abc_Ntk_t * pNtk )
{
    Abc_Obj_t * pObj, * pFan0, * pFan1, * pFanC;
    int i, Counter = 0;
    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        if ( !Abc_NodeIsMuxType(pObj) )
            continue;
        pFanC = Abc_NodeRecognizeMux( pObj, &pFan1, &pFan0 );
        pFanC = Abc_ObjRegular(pFanC);
        pFan0 = Abc_ObjRegular(pFan0);
        pFanC->vFanouts.nSize++;
        Counter++;
        if ( Abc_NodeIsExorType(pObj) )
        {
            pFan0->vFanouts.nSize++;
            Counter++;
        }
    }
    printf("Added %d fanouts\n", Counter );
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Cut_Man_t * Abc_NtkCuts( Abc_Ntk_t * pNtk, Cut_Params_t * pParams )
{
    ProgressBar * pProgress;
    Cut_Man_t * p;
    Cut_Cut_t * pList;
    Abc_Obj_t * pObj, * pNode;
    Vec_Ptr_t * vNodes;
    Vec_Int_t * vChoices;
    int i;
    abctime clk = Abc_Clock();

    extern void Abc_NtkBalanceAttach( Abc_Ntk_t * pNtk );
    extern void Abc_NtkBalanceDetach( Abc_Ntk_t * pNtk );

    if ( pParams->fAdjust )
    Abc_NtkCutsSubtractFanunt( pNtk );

    nTotal = nGood = nEqual = 0;

    assert( Abc_NtkIsStrash(pNtk) );
    // start the manager
    pParams->nIdsMax = Abc_NtkObjNumMax( pNtk );
    p = Cut_ManStart( pParams );
    // compute node attributes if local or global cuts are requested
    if ( pParams->fGlobal || pParams->fLocal )
    {
        extern Vec_Int_t * Abc_NtkGetNodeAttributes( Abc_Ntk_t * pNtk );
        Cut_ManSetNodeAttrs( p, Abc_NtkGetNodeAttributes(pNtk) );
    }
    // prepare for cut dropping
    if ( pParams->fDrop )
        Cut_ManSetFanoutCounts( p, Abc_NtkFanoutCounts(pNtk) );
    // set cuts for PIs
    Abc_NtkForEachCi( pNtk, pObj, i )
        if ( Abc_ObjFanoutNum(pObj) > 0 )
            Cut_NodeSetTriv( p, pObj->Id );
    // compute cuts for internal nodes
    vNodes = Abc_AigDfs( pNtk, 0, 1 ); // collects POs
    vChoices = Vec_IntAlloc( 100 );
    pProgress = Extra_ProgressBarStart( stdout, Vec_PtrSize(vNodes) );
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
    {
        // when we reached a CO, it is time to deallocate the cuts
        if ( Abc_ObjIsCo(pObj) )
        {
            if ( pParams->fDrop )
                Cut_NodeTryDroppingCuts( p, Abc_ObjFaninId0(pObj) );
            continue;
        }
        // skip constant node, it has no cuts
//        if ( Abc_NodeIsConst(pObj) )
//            continue;
        Extra_ProgressBarUpdate( pProgress, i, NULL );
        // compute the cuts to the internal node
        pList = (Cut_Cut_t *)Abc_NodeGetCuts( p, pObj, pParams->fDag, pParams->fTree );
        if ( pParams->fNpnSave && pList )
        {
            extern void Npn_ManSaveOne( unsigned * puTruth, int nVars );
            Cut_Cut_t * pCut;
            for ( pCut = pList; pCut; pCut = pCut->pNext )
                if ( pCut->nLeaves >= 4 )
                    Npn_ManSaveOne( Cut_CutReadTruth(pCut), pCut->nLeaves );
        }
        // consider dropping the fanins cuts
        if ( pParams->fDrop )
        {
            Cut_NodeTryDroppingCuts( p, Abc_ObjFaninId0(pObj) );
            Cut_NodeTryDroppingCuts( p, Abc_ObjFaninId1(pObj) );
        }
        // add cuts due to choices
        if ( Abc_AigNodeIsChoice(pObj) )
        {
            Vec_IntClear( vChoices );
            for ( pNode = pObj; pNode; pNode = (Abc_Obj_t *)pNode->pData )
                Vec_IntPush( vChoices, pNode->Id );
            Cut_NodeUnionCuts( p, vChoices );
        }
    }
    Extra_ProgressBarStop( pProgress );
    Vec_PtrFree( vNodes );
    Vec_IntFree( vChoices );
    if ( !pParams->fCsv )
    {
        Cut_ManPrintStats( p );
        ABC_PRT( "TOTAL", Abc_Clock() - clk );
    }
    else
    {
        // For CSV mode: show brief stats
        Cut_ManPrintStats( p );
    }
//    printf( "Area = %d.\n", Abc_NtkComputeArea( pNtk, p ) );
    Abc_NtkPrintCuts( p, pNtk, 0 );
//    Cut_ManPrintStatsToFile( p, pNtk->pSpec, Abc_Clock() - clk );

    // temporary printout of stats
    if ( !pParams->fCsv && nTotal )
        printf( "Total cuts = %d. Good cuts = %d.  Ratio = %5.2f\n", nTotal, nGood, ((double)nGood)/nTotal );
    if ( pParams->fAdjust )
        Abc_NtkCutsAddFanunt( pNtk );
    return p;
}

/**Function*************************************************************

  Synopsis    [Cut computation using the oracle.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkCutsOracle( Abc_Ntk_t * pNtk, Cut_Oracle_t * p )
{
    Abc_Obj_t * pObj;
    Vec_Ptr_t * vNodes;
    int i; //, clk = Abc_Clock();
    int fDrop = Cut_OracleReadDrop(p);

    assert( Abc_NtkIsStrash(pNtk) );

    // prepare cut droppping
    if ( fDrop )
        Cut_OracleSetFanoutCounts( p, Abc_NtkFanoutCounts(pNtk) );

    // set cuts for PIs
    Abc_NtkForEachCi( pNtk, pObj, i )
        if ( Abc_ObjFanoutNum(pObj) > 0 )
            Cut_OracleNodeSetTriv( p, pObj->Id );

    // compute cuts for internal nodes
    vNodes = Abc_AigDfs( pNtk, 0, 1 ); // collects POs
    Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pObj, i )
    {
        // when we reached a CO, it is time to deallocate the cuts
        if ( Abc_ObjIsCo(pObj) )
        {
            if ( fDrop )
                Cut_OracleTryDroppingCuts( p, Abc_ObjFaninId0(pObj) );
            continue;
        }
        // skip constant node, it has no cuts
//        if ( Abc_NodeIsConst(pObj) )
//            continue;
        // compute the cuts to the internal node
        Cut_OracleComputeCuts( p, pObj->Id, Abc_ObjFaninId0(pObj), Abc_ObjFaninId1(pObj),  
                Abc_ObjFaninC0(pObj), Abc_ObjFaninC1(pObj) );
        // consider dropping the fanins cuts
        if ( fDrop )
        {
            Cut_OracleTryDroppingCuts( p, Abc_ObjFaninId0(pObj) );
            Cut_OracleTryDroppingCuts( p, Abc_ObjFaninId1(pObj) );
        }
    }
    Vec_PtrFree( vNodes );
//ABC_PRT( "Total", Abc_Clock() - clk );
//Abc_NtkPrintCuts_( p, pNtk, 0 );
}


/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Cut_Man_t * Abc_NtkSeqCuts( Abc_Ntk_t * pNtk, Cut_Params_t * pParams )
{
/*
    Cut_Man_t *  p;
    Abc_Obj_t * pObj, * pNode;
    int i, nIters, fStatus;
    Vec_Int_t * vChoices;
    abctime clk = Abc_Clock();

    assert( Abc_NtkIsSeq(pNtk) );
    assert( pParams->fSeq );
//    assert( Abc_NtkIsDfsOrdered(pNtk) );

    // start the manager
    pParams->nIdsMax = Abc_NtkObjNumMax( pNtk );
    pParams->nCutSet = Abc_NtkCutSetNodeNum( pNtk );
    p = Cut_ManStart( pParams );

    // set cuts for the constant node and the PIs
    pObj = Abc_AigConst1(pNtk);
    if ( Abc_ObjFanoutNum(pObj) > 0 )
        Cut_NodeSetTriv( p, pObj->Id );
    Abc_NtkForEachPi( pNtk, pObj, i )
    {
//printf( "Setting trivial cut %d.\n", pObj->Id );
        Cut_NodeSetTriv( p, pObj->Id );
    }
    // label the cutset nodes and set their number in the array
    // assign the elementary cuts to the cutset nodes
    Abc_SeqForEachCutsetNode( pNtk, pObj, i )
    {
        assert( pObj->fMarkC == 0 );
        pObj->fMarkC = 1;
        pObj->pCopy = (Abc_Obj_t *)i;
        Cut_NodeSetTriv( p, pObj->Id );
//printf( "Setting trivial cut %d.\n", pObj->Id );
    }

    // process the nodes
    vChoices = Vec_IntAlloc( 100 );
    for ( nIters = 0; nIters < 10; nIters++ )
    {
//printf( "ITERATION %d:\n", nIters );
        // compute the cuts for the internal nodes
        Abc_AigForEachAnd( pNtk, pObj, i )
        {
            Abc_NodeGetCutsSeq( p, pObj, nIters==0 );
            // add cuts due to choices
            if ( Abc_AigNodeIsChoice(pObj) )
            {
                Vec_IntClear( vChoices );
                for ( pNode = pObj; pNode; pNode = pNode->pData )
                    Vec_IntPush( vChoices, pNode->Id );
                Cut_NodeUnionCutsSeq( p, vChoices, (pObj->fMarkC ? (int)pObj->pCopy : -1), nIters==0 );
            }
        }
        // merge the new cuts with the old cuts
        Abc_NtkForEachPi( pNtk, pObj, i )
            Cut_NodeNewMergeWithOld( p, pObj->Id );
        Abc_AigForEachAnd( pNtk, pObj, i )
            Cut_NodeNewMergeWithOld( p, pObj->Id );
        // for the cutset, transfer temp cuts to new cuts
        fStatus = 0;
        Abc_SeqForEachCutsetNode( pNtk, pObj, i )
            fStatus |= Cut_NodeTempTransferToNew( p, pObj->Id, i );
        if ( fStatus == 0 )
            break;
    }
    Vec_IntFree( vChoices );

    // if the status is not finished, transfer new to old for the cutset
    Abc_SeqForEachCutsetNode( pNtk, pObj, i )
        Cut_NodeNewMergeWithOld( p, pObj->Id );

    // transfer the old cuts to the new positions
    Abc_NtkForEachObj( pNtk, pObj, i )
        Cut_NodeOldTransferToNew( p, pObj->Id );

    // unlabel the cutset nodes
    Abc_SeqForEachCutsetNode( pNtk, pObj, i )
        pObj->fMarkC = 0;
if ( pParams->fVerbose )
{
    Cut_ManPrintStats( p );
ABC_PRT( "TOTAL ", Abc_Clock() - clk );
printf( "Converged after %d iterations.\n", nIters );
}
//Abc_NtkPrintCuts( p, pNtk, 1 );
    return p;
*/
    return NULL;
}

/**Function*************************************************************

  Synopsis    [Computes area.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NtkComputeArea( Abc_Ntk_t * pNtk, Cut_Man_t * p )
{
    Abc_Obj_t * pObj;
    int Counter, i;
    Counter = 0;
    Abc_NtkForEachCo( pNtk, pObj, i )
        Counter += Cut_ManMappingArea_rec( p, Abc_ObjFaninId0(pObj) );
    return Counter;
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void * Abc_NodeGetCutsRecursive( void * p, Abc_Obj_t * pObj, int fDag, int fTree )
{
    void * pList;
    if ( (pList = Abc_NodeReadCuts( p, pObj )) )
        return pList;
    Abc_NodeGetCutsRecursive( p, Abc_ObjFanin0(pObj), fDag, fTree );
    Abc_NodeGetCutsRecursive( p, Abc_ObjFanin1(pObj), fDag, fTree );
    return Abc_NodeGetCuts( p, pObj, fDag, fTree );
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void * Abc_NodeGetCuts( void * p, Abc_Obj_t * pObj, int fDag, int fTree )
{
    Abc_Obj_t * pFanin;
    int fDagNode, fTriv, TreeCode = 0;
//    assert( Abc_NtkIsStrash(pObj->pNtk) );
    assert( Abc_ObjFaninNum(pObj) == 2 );

    // check if the node is a DAG node
    fDagNode = (Abc_ObjFanoutNum(pObj) > 1 && !Abc_NodeIsMuxControlType(pObj));
    // increment the counter of DAG nodes
    if ( fDagNode ) Cut_ManIncrementDagNodes( (Cut_Man_t *)p );
    // add the trivial cut if the node is a DAG node, or if we compute all cuts
    fTriv = fDagNode || !fDag;
    // check if fanins are DAG nodes
    if ( fTree )
    {
        pFanin = Abc_ObjFanin0(pObj);
        TreeCode |=  (Abc_ObjFanoutNum(pFanin) > 1 && !Abc_NodeIsMuxControlType(pFanin));
        pFanin = Abc_ObjFanin1(pObj);
        TreeCode |= ((Abc_ObjFanoutNum(pFanin) > 1 && !Abc_NodeIsMuxControlType(pFanin)) << 1);
    }

    // changes due to the global/local cut computation
    {
        Cut_Params_t * pParams = Cut_ManReadParams((Cut_Man_t *)p);
        if ( pParams->fLocal )
        {
            Vec_Int_t * vNodeAttrs = Cut_ManReadNodeAttrs((Cut_Man_t *)p);
            fDagNode = Vec_IntEntry( vNodeAttrs, pObj->Id );
            if ( fDagNode ) Cut_ManIncrementDagNodes( (Cut_Man_t *)p );
//            fTriv = fDagNode || !pParams->fGlobal;
            fTriv = !Vec_IntEntry( vNodeAttrs, pObj->Id );
            TreeCode = 0;
            pFanin = Abc_ObjFanin0(pObj);
            TreeCode |=  Vec_IntEntry( vNodeAttrs, pFanin->Id );
            pFanin = Abc_ObjFanin1(pObj);
            TreeCode |= (Vec_IntEntry( vNodeAttrs, pFanin->Id ) << 1);
        }
    }
    return Cut_NodeComputeCuts( (Cut_Man_t *)p, pObj->Id, Abc_ObjFaninId0(pObj), Abc_ObjFaninId1(pObj),  
        Abc_ObjFaninC0(pObj), Abc_ObjFaninC1(pObj), fTriv, TreeCode );  
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodeGetCutsSeq( void * p, Abc_Obj_t * pObj, int fTriv )
{
/*
    int CutSetNum;
    assert( Abc_NtkIsSeq(pObj->pNtk) );
    assert( Abc_ObjFaninNum(pObj) == 2 );
    fTriv     = pObj->fMarkC ? 0 : fTriv;
    CutSetNum = pObj->fMarkC ? (int)pObj->pCopy : -1;
    Cut_NodeComputeCutsSeq( p, pObj->Id, Abc_ObjFaninId0(pObj), Abc_ObjFaninId1(pObj),  
        Abc_ObjFaninC0(pObj), Abc_ObjFaninC1(pObj), Seq_ObjFaninL0(pObj), Seq_ObjFaninL1(pObj), fTriv, CutSetNum );  
*/
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void * Abc_NodeReadCuts( void * p, Abc_Obj_t * pObj )
{
    return Cut_NodeReadCutsNew( (Cut_Man_t *)p, pObj->Id );  
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NodeFreeCuts( void * p, Abc_Obj_t * pObj )
{
    Cut_NodeFreeCuts( (Cut_Man_t *)p, pObj->Id );
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkPrintCuts( void * p, Abc_Ntk_t * pNtk, int fSeq )
{
    Cut_Params_t * pParams = Cut_ManReadParams( (Cut_Man_t *)p );
    if ( pParams && pParams->fCsv )
    {
        Abc_NtkPrintCutsCsv( p, pNtk, fSeq );
        return;
    }

    Cut_Cut_t * pList, * pCut;
    Abc_Obj_t * pObj, * pLeaf;
    int i, k;
    printf( "Cuts of the network:\n" );
    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        pList = (Cut_Cut_t *)Abc_NodeReadCuts( (Cut_Man_t *)p, pObj );
        if ( pList == NULL )
            continue;
        printf( "Node %s:\n", Abc_ObjName(pObj) );
        for ( pCut = pList; pCut; pCut = pCut->pNext )
        {
            printf( "  %d : {", pCut->nLeaves );
            for ( k = 0; k < (int)pCut->nLeaves; k++ )
            {
                pLeaf = Abc_NtkObj( pNtk, fSeq ? (pCut->pLeaves[k] >> CUT_SHIFT) : pCut->pLeaves[k] );
                printf( " %s", Abc_ObjName(pLeaf) );
                if ( fSeq && (pCut->pLeaves[k] & CUT_MASK) )
                    printf( "(%d)", pCut->pLeaves[k] & CUT_MASK );
            }
            printf( " }\n" );
        }
    }
}

static int Abc_NtkComputeCutVolume_rec( Abc_Ntk_t * pNtk, int iNode, int * pLeaves, int nLeaves, Vec_Int_t * vVisited )
{
    int i;
    if ( Vec_IntFind( vVisited, iNode ) >= 0 )
        return 0;
    for ( i = 0; i < nLeaves; i++ )
        if ( pLeaves[i] == iNode )
            return 0;
    Vec_IntPush( vVisited, iNode );
    Abc_Obj_t * pObj = Abc_NtkObj( pNtk, iNode );
    if ( !Abc_ObjIsNode(pObj) )
        return 0;
    return 1 + Abc_NtkComputeCutVolume_rec( pNtk, Abc_ObjFaninId0(pObj), pLeaves, nLeaves, vVisited )
             + Abc_NtkComputeCutVolume_rec( pNtk, Abc_ObjFaninId1(pObj), pLeaves, nLeaves, vVisited );
}

static int Abc_NtkComputeCutVolume( Abc_Ntk_t * pNtk, int iRoot, int * pLeaves, int nLeaves )
{
    Vec_Int_t * vVisited = Vec_IntAlloc( 100 );
    int volume = Abc_NtkComputeCutVolume_rec( pNtk, iRoot, pLeaves, nLeaves, vVisited );
    Vec_IntFree( vVisited );
    return volume;
}

static int Abc_NtkComputeCutHeight_rec( Abc_Ntk_t * pNtk, int iNode, int * pLeaves, int nLeaves, Vec_Int_t * vMemo )
{
    int i;
    for ( i = 0; i < nLeaves; i++ )
        if ( pLeaves[i] == iNode )
            return 0;
    int h = Vec_IntEntry( vMemo, iNode );
    if ( h >= 0 )
        return h;
    Abc_Obj_t * pObj = Abc_NtkObj( pNtk, iNode );
    if ( !Abc_ObjIsNode(pObj) )
    {
        Vec_IntWriteEntry( vMemo, iNode, 0 );
        return 0;
    }
    int h0 = Abc_NtkComputeCutHeight_rec( pNtk, Abc_ObjFaninId0(pObj), pLeaves, nLeaves, vMemo );
    int h1 = Abc_NtkComputeCutHeight_rec( pNtk, Abc_ObjFaninId1(pObj), pLeaves, nLeaves, vMemo );
    h = 1 + (h0 > h1 ? h0 : h1);
    Vec_IntWriteEntry( vMemo, iNode, h );
    return h;
}

static int Abc_NtkComputeCutHeight( Abc_Ntk_t * pNtk, int iRoot, int * pLeaves, int nLeaves )
{
    Vec_Int_t * vMemo = Vec_IntStart( Abc_NtkObjNumMax(pNtk) + 1 );
    int height = Abc_NtkComputeCutHeight_rec( pNtk, iRoot, pLeaves, nLeaves, vMemo );
    Vec_IntFree( vMemo );
    return height;
}

static void Abc_NtkPrintCutsCsv( void * p, Abc_Ntk_t * pNtk, int fSeq )
{
    Cut_Cut_t * pList, * pCut;
    Abc_Obj_t * pObj;
    FILE * pFile;
    int i, k, cutId = 0, nCutsPerNode, nTotalCuts = 0, nNodesWithMultipleCuts = 0;
    int nObjsWithCuts = 0;

    pFile = fopen( "cuts.csv", "w" );
    if ( pFile == NULL )
    {
        printf( "Cannot open file \"cuts.csv\" for writing.\n" );
        return;
    }

    fprintf( pFile, "root_idx,cut_idx,l1_idx,l2_idx,l3_idx,l4_idx,l5_idx,vol_cut,cut_height,canon_tt_0,cannon_tt_1\n" );
    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        pList = (Cut_Cut_t *)Abc_NodeReadCuts( (Cut_Man_t *)p, pObj );
        if ( pList == NULL )
            continue;
        
        nObjsWithCuts++;
        nCutsPerNode = 0;
        for ( pCut = pList; pCut; pCut = pCut->pNext )
        {
            nCutsPerNode++;
            nTotalCuts++;
            int leaves[5] = { -1, -1, -1, -1, -1 };
            int nLeaves = pCut->nLeaves;
            int rootId = Abc_ObjId(pObj);
            for ( k = 0; k < nLeaves && k < 5; k++ )
            {
                leaves[k] = fSeq ? (pCut->pLeaves[k] >> CUT_SHIFT) : pCut->pLeaves[k];
            }
            int vol = Abc_NtkComputeCutVolume( pNtk, rootId, leaves, nLeaves );
            int height = Abc_NtkComputeCutHeight( pNtk, rootId, leaves, nLeaves );
            
            // Get truth table words
            unsigned * puTruth = Cut_CutReadTruth(pCut);
            unsigned tt0 = puTruth ? puTruth[0] : 0;
            unsigned tt1 = puTruth ? (pCut->nLeaves > 5 ? puTruth[1] : 0) : 0;

            fprintf( pFile, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u\n",
                rootId,
                ++cutId,
                leaves[0], leaves[1], leaves[2], leaves[3], leaves[4],
                vol,
                height,
                tt0,
                tt1 );
        }
        if ( nCutsPerNode > 1 )
            nNodesWithMultipleCuts++;
    }
    // Print diagnostic info as a comment (lines starting with #)
    fprintf( pFile, "# Total cuts: %d, Nodes with cuts: %d, Nodes with multiple cuts: %d\n", nTotalCuts, nObjsWithCuts, nNodesWithMultipleCuts );
    fclose( pFile );
    printf( "Cut data written to \"cuts.csv\".\n" );
}

/**Function*************************************************************

  Synopsis    [Computes the cuts for the network.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Abc_NtkPrintCuts_( void * p, Abc_Ntk_t * pNtk, int fSeq )
{
    Cut_Cut_t * pList;
    Abc_Obj_t * pObj;
    pObj = Abc_NtkObj( pNtk, 2 * Abc_NtkObjNum(pNtk) / 3 );
    pList = (Cut_Cut_t *)Abc_NodeReadCuts( (Cut_Man_t *)p, pObj );
    printf( "Node %s:\n", Abc_ObjName(pObj) );
    Cut_CutPrintList( pList, fSeq );
}

/**Function*************************************************************

  Synopsis    [Assigns global attributes randomly.]

  Description [Old code.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Int_t * Abc_NtkGetNodeAttributes( Abc_Ntk_t * pNtk ) 
{
    Vec_Int_t * vAttrs;
//    Vec_Ptr_t * vNodes;
    Abc_Obj_t * pObj;//, * pTemp;
    int i;//, k;
    int nNodesTotal = 0, nMffcsTotal = 0;
    extern Vec_Ptr_t * Abc_NodeMffcInsideCollect( Abc_Obj_t * pNode );

    vAttrs = Vec_IntStart( Abc_NtkObjNumMax(pNtk) + 1 );
//    Abc_NtkForEachCi( pNtk, pObj, i )
//        Vec_IntWriteEntry( vAttrs, pObj->Id, 1 );

    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        if ( Abc_ObjIsNode(pObj) )
            nNodesTotal++;
        if ( Abc_ObjIsCo(pObj) && Abc_ObjIsNode(Abc_ObjFanin0(pObj)) )
            nMffcsTotal += Abc_NodeMffcSize( Abc_ObjFanin0(pObj) );
//        if ( Abc_ObjIsNode(pObj) && (rand() % 4 == 0) )
//        if ( Abc_ObjIsNode(pObj) && Abc_ObjFanoutNum(pObj) > 1 && !Abc_NodeIsMuxControlType(pObj) && (rand() % 3 == 0) )
        if ( Abc_ObjIsNode(pObj) && Abc_ObjFanoutNum(pObj) > 1 && !Abc_NodeIsMuxControlType(pObj) )
        {
            int nMffc = Abc_NodeMffcSize(pObj);
            nMffcsTotal += Abc_NodeMffcSize(pObj);
//            printf( "%d ", nMffc );

            if ( nMffc > 2 || Abc_ObjFanoutNum(pObj) > 8 )
                Vec_IntWriteEntry( vAttrs, pObj->Id, 1 );
        }
    }
/*
    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        if ( Vec_IntEntry( vAttrs, pObj->Id ) )
        {
            vNodes = Abc_NodeMffcInsideCollect( pObj );
            Vec_PtrForEachEntry( Abc_Obj_t *, vNodes, pTemp, k )
                if ( pTemp != pObj )
                    Vec_IntWriteEntry( vAttrs, pTemp->Id, 0 );
            Vec_PtrFree( vNodes );
        }
    }
*/
    printf( "Total nodes = %d. Total MFFC nodes = %d.\n", nNodesTotal, nMffcsTotal );
    return vAttrs; 
}

/**Function*************************************************************

  Synopsis    [Assigns global attributes randomly.]

  Description [Old code.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Abc_NtkSubDagSize_rec( Abc_Obj_t * pObj, Vec_Int_t * vAttrs ) 
{
    if ( Abc_NodeIsTravIdCurrent(pObj) )
        return 0;
    Abc_NodeSetTravIdCurrent(pObj);
    if ( Vec_IntEntry( vAttrs, pObj->Id ) )
        return 0;
    if ( Abc_ObjIsCi(pObj) )
        return 1;
    assert( Abc_ObjFaninNum(pObj) == 2 );
    return 1 + Abc_NtkSubDagSize_rec(Abc_ObjFanin0(pObj), vAttrs) +
        Abc_NtkSubDagSize_rec(Abc_ObjFanin1(pObj), vAttrs);
}

/**Function*************************************************************

  Synopsis    [Assigns global attributes randomly.]

  Description [Old code.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Vec_Int_t * Abc_NtkGetNodeAttributes2( Abc_Ntk_t * pNtk ) 
{
    Vec_Int_t * vAttrs;
    Abc_Obj_t * pObj;
    int i, nSize;
    assert( Abc_NtkIsDfsOrdered(pNtk) );
    vAttrs = Vec_IntStart( Abc_NtkObjNumMax(pNtk) + 1 );
    Abc_NtkForEachObj( pNtk, pObj, i )
    {
        // skip no-nodes and nodes without fanouts
        if ( pObj->Id == 0 || !(Abc_ObjIsNode(pObj) && Abc_ObjFanoutNum(pObj) > 1 && !Abc_NodeIsMuxControlType(pObj)) )
            continue;
        // the node has more than one fanout - count its sub-DAG size
        Abc_NtkIncrementTravId( pNtk );
        nSize = Abc_NtkSubDagSize_rec( pObj, vAttrs );
        if ( nSize > 15 )
            Vec_IntWriteEntry( vAttrs, pObj->Id, 1 );
    }
    return vAttrs; 
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END