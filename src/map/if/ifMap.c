/**CFile****************************************************************

  FileName    [ifMap.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [FPGA mapping based on priority cuts.]

  Synopsis    [Mapping procedures.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - November 21, 2006.]

  Revision    [$Id: ifMap.c,v 1.00 2006/11/21 00:00:00 alanmi Exp $]

***********************************************************************/

#include "if.h"
#include "misc/extra/extra.h"

ABC_NAMESPACE_IMPL_START

////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

extern char * Dau_DsdMerge( char * pDsd0i, int * pPerm0, char * pDsd1i, int * pPerm1, int fCompl0, int fCompl1, int nVars );
extern int    If_CutDelayRecCost3( If_Man_t* p, If_Cut_t* pCut, If_Obj_t * pObj );
extern int    Abc_ExactDelayCost( word * pTruth, int nVars, int * pArrTimeProfile, char * pPerm, int * Cost, int AigLevel );

//my code
static int g_CutId = 0;
static FILE * g_CutDelayFile = NULL;
static FILE * g_NodeFile = NULL;
static int * g_FanoutCounts = NULL;
static Vec_Int_t ** g_Fanouts = NULL;
static int * g_RevLevels = NULL;

// ML filtering statistics
static int useML = 0; // Flag to track if we should run ML
static int g_TotalCands = 0;     // Total candidate cuts
static int g_MLFiltered = 0;     // Cuts filtered by ML (class >= 3)
static int g_MLAdjusted = 0;     // Cuts adjusted by ML penalty
static int g_ClassDist[5] = {0, 0, 0, 0, 0};  // Class distribution for debugging

#include <math.h> // Required for sqrt() in BatchNorm

// #define INPUT_SIZE  11
// #define HIDDEN1 128
// #define HIDDEN2 64
// #define HIDDEN3 32
// #define OUTPUT_SIZE 5

// // --- Linear Layer Weights & Biases ---
// float W1[HIDDEN1][INPUT_SIZE], B1[HIDDEN1];
// float W2[HIDDEN2][HIDDEN1], B2[HIDDEN2];
// float W3[HIDDEN3][HIDDEN2], B3[HIDDEN3];
// float W4[OUTPUT_SIZE][HIDDEN3], B4[OUTPUT_SIZE];

// // --- BatchNorm 1 (Layer 2 in Sequential) ---
// float BN1_W[HIDDEN1], BN1_B[HIDDEN1], BN1_M[HIDDEN1], BN1_V[HIDDEN1];

// // --- BatchNorm 2 (Layer 5 in Sequential) ---
// float BN2_W[HIDDEN2], BN2_B[HIDDEN2], BN2_M[HIDDEN2], BN2_V[HIDDEN2];

// // --- Scaler Data ---
// float scaler_mean[INPUT_SIZE];
// float scaler_scale[INPUT_SIZE];

// void Normalize(float *x)
// {
//     for (int i = 0; i < INPUT_SIZE; i++) {
//         if (scaler_scale[i] != 0)
//             x[i] = (x[i] - scaler_mean[i]) / scaler_scale[i];
//         else
//             x[i] = 0;
//     }
// }

// float relu(float x) { return x > 0 ? x : 0; }

// int argmax(float *arr, int n)
// {
//     int idx = 0;
//     for (int i = 1; i < n; i++)
//         if (arr[i] > arr[idx]) idx = i;
//     return idx;
// }

// int PredictClass(float *input)
// {
//     float h1[HIDDEN1], h2[HIDDEN2], h3[HIDDEN3], out[OUTPUT_SIZE];

//     // 1. Standard Scaler
//     Normalize(input);

//     // 2. Block 1: Linear(13, 128) -> ReLU -> BatchNorm1d(128)
//     for (int i = 0; i < HIDDEN1; i++) {
//         float sum = B1[i];
//         for (int j = 0; j < INPUT_SIZE; j++)
//             sum += W1[i][j] * input[j];
            
//         float relu_val = relu(sum);
        
//         // BatchNorm math: y = ((x - mean) / sqrt(var + eps)) * weight + bias
//         // PyTorch default eps is 1e-5
//         h1[i] = ((relu_val - BN1_M[i]) / sqrt(BN1_V[i] + 1e-5f)) * BN1_W[i] + BN1_B[i];
//     }

//     // 3. Block 2: Linear(128, 64) -> ReLU -> BatchNorm1d(64)
//     for (int i = 0; i < HIDDEN2; i++) {
//         float sum = B2[i];
//         for (int j = 0; j < HIDDEN1; j++)
//             sum += W2[i][j] * h1[j];
            
//         float relu_val = relu(sum);
        
//         h2[i] = ((relu_val - BN2_M[i]) / sqrt(BN2_V[i] + 1e-5f)) * BN2_W[i] + BN2_B[i];
//     }

//     // 4. Block 3: Linear(64, 32) -> ReLU (No BatchNorm here in your model)
//     for (int i = 0; i < HIDDEN3; i++) {
//         float sum = B3[i];
//         for (int j = 0; j < HIDDEN2; j++)
//             sum += W3[i][j] * h2[j];
            
//         h3[i] = relu(sum);
//     }

//     // 5. Output Layer: Linear(32, 5)
//     for (int i = 0; i < OUTPUT_SIZE; i++) {
//         float sum = B4[i];
//         for (int j = 0; j < HIDDEN3; j++)
//             sum += W4[i][j] * h3[j];
            
//         out[i] = sum;
//     }

//     return argmax(out, OUTPUT_SIZE);
// }

// void LoadModelWeights()
// {
//     FILE *f = fopen("model_weights.txt", "r");
//     if (!f) {
//         printf("ERROR: Cannot open model_weights.txt\n");
//         useML=0;
//         return;
//     }
//     useML=1;

//     char name[256];

//     while (fscanf(f, "%s", name) != EOF)
//     {
//         // --- Layer 0: Linear(13, 128) ---
//         if (strcmp(name, "model.0.weight") == 0) {
//             for (int i = 0; i < HIDDEN1; i++)
//                 for (int j = 0; j < INPUT_SIZE; j++) fscanf(f, "%f", &W1[i][j]);
//         }
//         else if (strcmp(name, "model.0.bias") == 0) {
//             for (int i = 0; i < HIDDEN1; i++) fscanf(f, "%f", &B1[i]);
//         }
        
//         // --- Layer 2: BatchNorm1d(128) ---
//         else if (strcmp(name, "model.2.weight") == 0) { // gamma
//             for (int i = 0; i < HIDDEN1; i++) fscanf(f, "%f", &BN1_W[i]);
//         }
//         else if (strcmp(name, "model.2.bias") == 0) { // beta
//             for (int i = 0; i < HIDDEN1; i++) fscanf(f, "%f", &BN1_B[i]);
//         }
//         else if (strcmp(name, "model.2.running_mean") == 0) {
//             for (int i = 0; i < HIDDEN1; i++) fscanf(f, "%f", &BN1_M[i]);
//         }
//         else if (strcmp(name, "model.2.running_var") == 0) {
//             for (int i = 0; i < HIDDEN1; i++) fscanf(f, "%f", &BN1_V[i]);
//         }

//         // --- Layer 3: Linear(128, 64) ---
//         else if (strcmp(name, "model.3.weight") == 0) {
//             for (int i = 0; i < HIDDEN2; i++)
//                 for (int j = 0; j < HIDDEN1; j++) fscanf(f, "%f", &W2[i][j]);
//         }
//         else if (strcmp(name, "model.3.bias") == 0) {
//             for (int i = 0; i < HIDDEN2; i++) fscanf(f, "%f", &B2[i]);
//         }
        
//         // --- Layer 5: BatchNorm1d(64) ---
//         else if (strcmp(name, "model.5.weight") == 0) { 
//             for (int i = 0; i < HIDDEN2; i++) fscanf(f, "%f", &BN2_W[i]);
//         }
//         else if (strcmp(name, "model.5.bias") == 0) { 
//             for (int i = 0; i < HIDDEN2; i++) fscanf(f, "%f", &BN2_B[i]);
//         }
//         else if (strcmp(name, "model.5.running_mean") == 0) {
//             for (int i = 0; i < HIDDEN2; i++) fscanf(f, "%f", &BN2_M[i]);
//         }
//         else if (strcmp(name, "model.5.running_var") == 0) {
//             for (int i = 0; i < HIDDEN2; i++) fscanf(f, "%f", &BN2_V[i]);
//         }

//         // --- Layer 6: Linear(64, 32) ---
//         else if (strcmp(name, "model.6.weight") == 0) {
//             for (int i = 0; i < HIDDEN3; i++)
//                 for (int j = 0; j < HIDDEN2; j++) fscanf(f, "%f", &W3[i][j]);
//         }
//         else if (strcmp(name, "model.6.bias") == 0) {
//             for (int i = 0; i < HIDDEN3; i++) fscanf(f, "%f", &B3[i]);
//         }
        
//         // --- Layer 8: Linear(32, 5) ---
//         else if (strcmp(name, "model.8.weight") == 0) {
//             for (int i = 0; i < OUTPUT_SIZE; i++)
//                 for (int j = 0; j < HIDDEN3; j++) fscanf(f, "%f", &W4[i][j]);
//         }
//         else if (strcmp(name, "model.8.bias") == 0) {
//             for (int i = 0; i < OUTPUT_SIZE; i++) fscanf(f, "%f", &B4[i]);
//         }
//         // Safely ignore any unmapped strings to prevent infinite loops or crashes
//     }

//     fclose(f);
//     printf("Model weights loaded successfully.\n");
// }

// void LoadScaler()
// {
//     FILE *f = fopen("scaler.txt", "r");
//     if (!f) {
//         printf("ERROR: Cannot open scaler.txt\n");
//         return;
//     }

//     // mean
//     for (int i = 0; i < INPUT_SIZE; i++)
//         fscanf(f, "%f", &scaler_mean[i]);

//     // scale (std)
//     for (int i = 0; i < INPUT_SIZE; i++)
//         fscanf(f, "%f", &scaler_scale[i]);

//     fclose(f);
//     printf("Scaler loaded successfully.\n");
// }


#define INPUT_SIZE 14

extern If_Obj_t * If_ManObj( If_Man_t * p, int i );

// --- NEW GLOBAL VARIABLES FOR DECISION TREE ---
typedef struct {
    int feature_idx;
    float threshold;
    int left_child;
    int right_child;
    int class_val;
} DTNode;

static DTNode * g_TreeNodes = NULL;

// --- LOAD FUNCTION ---
void LoadModelWeights() {
    FILE *f = fopen("model_weights.txt", "r");
    if (!f) {
        printf("Vanilla Mode: model_weights.txt hidden. ML Disabled.\n");
        useML = 0;
        return;
    }
    
    int n_nodes;
    if (fscanf(f, "%d", &n_nodes) != 1) return;
    
    if (g_TreeNodes) free(g_TreeNodes); // Prevent memory leaks on reload
    g_TreeNodes = (DTNode*)malloc(n_nodes * sizeof(DTNode));
    
    for (int i = 0; i < n_nodes; i++) {
        fscanf(f, "%d %f %d %d %d", 
            &g_TreeNodes[i].feature_idx, 
            &g_TreeNodes[i].threshold, 
            &g_TreeNodes[i].left_child, 
            &g_TreeNodes[i].right_child, 
            &g_TreeNodes[i].class_val);
    }
    fclose(f);
    useML = 1;
}

// --- PREDICT FUNCTION ---
int PredictClass(float features[]) {
    int current_node = 0;
    
    // Traverse the tree until we hit a leaf node (left_child == -1)
    while (g_TreeNodes[current_node].left_child != -1) {
        int feat = g_TreeNodes[current_node].feature_idx;
        float thresh = g_TreeNodes[current_node].threshold;
        
        if (features[feat] <= thresh) {
            current_node = g_TreeNodes[current_node].left_child;
        } else {
            current_node = g_TreeNodes[current_node].right_child;
        }
    }
    return g_TreeNodes[current_node].class_val;
}

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Compute delay of the cut's output in terms of logic levels.]

  Description [Uses the best arrival time of the fanins of the cut
  to compute the arrival times of the output of the cut.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_ManCutAigDelay_rec( If_Man_t * p, If_Obj_t * pObj, Vec_Ptr_t * vVisited )
{
    int Delay0, Delay1;
    if ( pObj->fVisit )
        return pObj->iCopy;
    if ( If_ObjIsCi(pObj) || If_ObjIsConst1(pObj) )
        return -1;
    // store the node in the structure by level
    assert( If_ObjIsAnd(pObj) );
    pObj->fVisit = 1;
    Vec_PtrPush( vVisited, pObj );
    Delay0 = If_ManCutAigDelay_rec( p, pObj->pFanin0, vVisited );
    Delay1 = If_ManCutAigDelay_rec( p, pObj->pFanin1, vVisited );
    pObj->iCopy = (Delay0 >= 0 && Delay1 >= 0) ? 1 + Abc_MaxInt(Delay0, Delay1) : -1;
    return pObj->iCopy;
}
int If_ManCutAigDelay( If_Man_t * p, If_Obj_t * pObj, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    int i, Delay;
    Vec_PtrClear( p->vVisited );
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->fVisit == 0 );
        pLeaf->fVisit = 1;
        Vec_PtrPush( p->vVisited, pLeaf );
        pLeaf->iCopy = If_ObjCutBest(pLeaf)->Delay;
    }
    Delay = If_ManCutAigDelay_rec( p, pObj, p->vVisited );
    Vec_PtrForEachEntry( If_Obj_t *, p->vVisited, pLeaf, i )
        pLeaf->fVisit = 0;
//    assert( Delay <= (int)pObj->Level );
    return Delay;
}

/**Function*************************************************************

  Synopsis    [Counts the number of 1s in the signature.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int If_WordCountOnes( unsigned uWord )
{
    uWord = (uWord & 0x55555555) + ((uWord>>1) & 0x55555555);
    uWord = (uWord & 0x33333333) + ((uWord>>2) & 0x33333333);
    uWord = (uWord & 0x0F0F0F0F) + ((uWord>>4) & 0x0F0F0F0F);
    uWord = (uWord & 0x00FF00FF) + ((uWord>>8) & 0x00FF00FF);
    return  (uWord & 0x0000FFFF) + (uWord>>16);
}

/**Function*************************************************************

  Synopsis    [Counts the number of 1s in the signature.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutDelaySpecial( If_Man_t * p, If_Cut_t * pCut, int fCarry )
{
    static float Pin2Pin[2][3] = { {1.0, 1.0, 1.0}, {1.0, 1.0, 0.0} };
    If_Obj_t * pLeaf;
    float DelayCur, Delay = -IF_FLOAT_LARGE;
    int i;
    assert( pCut->nLeaves <= 3 );
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        DelayCur = If_ObjCutBest(pLeaf)->Delay;
        Delay = IF_MAX( Delay, Pin2Pin[fCarry][i] + DelayCur );
    }
    return Delay;
}

/**Function*************************************************************

  Synopsis    [Returns arrival time profile of the cut.]

  Description [The procedure returns static storage, which should not be
  deallocated and is only valid until before the procedure is called again.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int * If_CutArrTimeProfile( If_Man_t * p, If_Cut_t * pCut )
{
    int i;
    for ( i = 0; i < If_CutLeaveNum(pCut); i++ )
        p->pArrTimeProfile[i] = (int)If_ObjCutBest(If_CutLeaf(p, pCut, i))->Delay;
    return p->pArrTimeProfile;
}

/**Function*************************************************************

  Synopsis    [Finds the best cut for the given node.]

  Description [Mapping modes: delay (0), area flow (1), area (2).]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_ObjPerformMappingAnd( If_Man_t * p, If_Obj_t * pObj, int Mode, int fPreprocess, int fFirst )
{
    If_Set_t * pCutSet;
    If_Cut_t * pCut0, * pCut1, * pCut;
    If_Cut_t * pCut0R, * pCut1R;
    int fFunc0R, fFunc1R;
    int i, k, v, iCutDsd, fChange;
    int fSave0 = p->pPars->fDelayOpt || p->pPars->fDelayOptLut || p->pPars->fDsdBalance || p->pPars->fUserRecLib || p->pPars->fUserSesLib || p->pPars->fUserLutDec || p->pPars->fUserLut2D ||
        p->pPars->fUseDsdTune || p->pPars->fUseCofVars || p->pPars->fUseAndVars || p->pPars->fUse34Spec || p->pPars->pLutStruct || p->pPars->pFuncCell2 || p->pPars->fUseCheck1 || p->pPars->fUseCheck2;
    int fUseAndCut = (p->pPars->nAndDelay > 0) || (p->pPars->nAndArea > 0);
    assert( !If_ObjIsAnd(pObj->pFanin0) || pObj->pFanin0->pCutSet->nCuts > 0 );
    assert( !If_ObjIsAnd(pObj->pFanin1) || pObj->pFanin1->pCutSet->nCuts > 0 );

    // prepare
    if ( Mode == 0 )
        pObj->EstRefs = (float)pObj->nRefs;
    else if ( Mode == 1 )
        pObj->EstRefs = (float)((2.0 * pObj->EstRefs + pObj->nRefs) / 3.0);
    // deref the selected cut
    if ( Mode && pObj->nRefs > 0 )
        If_CutAreaDeref( p, If_ObjCutBest(pObj) );

    // prepare the cutset
    pCutSet = If_ManSetupNodeCutSet( p, pObj );

    // get the current assigned best cut
    pCut = If_ObjCutBest(pObj);
    if ( !fFirst )
    {
        // recompute the parameters of the best cut
        if ( p->pPars->fDelayOpt )
            pCut->Delay = If_CutSopBalanceEval( p, pCut, NULL );
        else if ( p->pPars->fDsdBalance )
            pCut->Delay = If_CutDsdBalanceEval( p, pCut, NULL );
        else if ( p->pPars->fUserRecLib )
            pCut->Delay = If_CutDelayRecCost3( p, pCut, pObj ); 
        else if ( p->pPars->fUserSesLib )
        {
            int Cost = 0;
            pCut->fUser = 1;
            pCut->Delay = (float)Abc_ExactDelayCost( If_CutTruthW(p, pCut), If_CutLeaveNum(pCut), If_CutArrTimeProfile(p, pCut), If_CutPerm(pCut), &Cost, If_ManCutAigDelay(p, pObj, pCut) ); 
            if ( Cost == ABC_INFINITY )
            {
                for ( v = 0; v < If_CutLeaveNum(pCut); v++ )
                    If_CutPerm(pCut)[v] = IF_BIG_CHAR;
                pCut->Cost = IF_COST_MAX;
                pCut->fUseless = 1;
            }
        }
        else if ( p->pPars->fUserLutDec || p->pPars->fUserLut2D )
        {
            pCut->Delay = If_LutDecReEval( p, pCut ); 
        }
        else if ( p->pPars->fDelayOptLut )
            pCut->Delay = If_CutLutBalanceEval( p, pCut );
        else if( p->pPars->nGateSize > 0 )
            pCut->Delay = If_CutDelaySop( p, pCut );
        else
            pCut->Delay = If_CutDelay( p, pObj, pCut );

        assert( pCut->Delay != -1 );
        if ( pCut->Delay > pObj->Required + 2*p->fEpsilon )
            Abc_Print( 1, "If_ObjPerformMappingAnd(): Warning! Node with ID %d has delay (%f) exceeding the required times (%f).\n", 
                pObj->Id, pCut->Delay, pObj->Required + p->fEpsilon );
        pCut->Area = (Mode == 2)? If_CutAreaDerefed( p, pCut ) : If_CutAreaFlow( p, pCut );
        if ( p->pPars->fEdge )
            pCut->Edge = (Mode == 2)? If_CutEdgeDerefed( p, pCut ) : If_CutEdgeFlow( p, pCut );
        if ( p->pPars->fPower )
            pCut->Power = (Mode == 2)? If_CutPowerDerefed( p, pCut, pObj ) : If_CutPowerFlow( p, pCut, pObj );
        // save the best cut from the previous iteration
        if ( !fPreprocess || pCut->nLeaves <= 1 )
            If_CutCopy( p, pCutSet->ppCuts[pCutSet->nCuts++], pCut );
    }

    // generate cuts
    If_ObjForEachCut( pObj->pFanin0, pCut0, i )
    If_ObjForEachCut( pObj->pFanin1, pCut1, k )
    {
        // get the next free cut
        assert( pCutSet->nCuts <= pCutSet->nCutsMax );
        pCut = pCutSet->ppCuts[pCutSet->nCuts];
        // make sure K-feasible cut exists
        if ( If_WordCountOnes(pCut0->uSign | pCut1->uSign) > p->pPars->nLutSize )
            continue;

        pCut0R = pCut0;
        pCut1R = pCut1;
        fFunc0R = pCut0->iCutFunc ^ pCut0->fCompl ^ pObj->fCompl0;
        fFunc1R = pCut1->iCutFunc ^ pCut1->fCompl ^ pObj->fCompl1;
        if ( !p->pPars->fUseTtPerm || pCut0->nLeaves > pCut1->nLeaves || (pCut0->nLeaves == pCut1->nLeaves && fFunc0R > fFunc1R) )
        {
        }
        else
        {
            ABC_SWAP( If_Cut_t *, pCut0R, pCut1R );
            ABC_SWAP( int, fFunc0R, fFunc1R );
        }        

        // merge the cuts
        if ( p->pPars->fUseTtPerm )
        {
            if ( !If_CutMerge( p, pCut0R, pCut1R, pCut ) )
                continue;
        }
        else
        {
            if ( !If_CutMergeOrdered( p, pCut0, pCut1, pCut ) )
                continue;
        }
        if ( p->pPars->fUserLutDec && !fFirst && pCut->nLeaves > p->pPars->nLutDecSize )
            continue;
        if ( pObj->fSpec && pCut->nLeaves == (unsigned)p->pPars->nLutSize )
            continue;
        p->nCutsMerged++;
        p->nCutsTotal++;
        //check if this cut is contained in any of the available cuts
        if ( !p->pPars->fSkipCutFilter && If_CutFilter( pCutSet, pCut, fSave0 ) )
            continue;
        // check if the cut is a special AND-gate cut
        pCut->fAndCut = fUseAndCut && pCut->nLeaves == 2 && pCut->pLeaves[0] == pObj->pFanin0->Id && pCut->pLeaves[1] == pObj->pFanin1->Id;
        
        // compute the truth table
        pCut->iCutFunc = -1;
        pCut->fCompl = 0;
        if ( p->pPars->fTruth )
        {
            abctime clk = 0;
            if ( p->pPars->fVerbose )
                clk = Abc_Clock();
            if ( p->pPars->fUseTtPerm )
                fChange = If_CutComputeTruthPerm( p, pCut, pCut0R, pCut1R, fFunc0R, fFunc1R );
            else
                fChange = If_CutComputeTruth( p, pCut, pCut0, pCut1, pObj->fCompl0, pObj->fCompl1 );
            if ( p->pPars->fVerbose )
                p->timeCache[4] += Abc_Clock() - clk;
            if ( !p->pPars->fSkipCutFilter && fChange && If_CutFilter( pCutSet, pCut, fSave0 ) )
                continue;
            if ( p->pPars->fLut6Filter && pCut->nLeaves == 6 && !If_CutCheckTruth6(p, pCut) )
                continue;
            if ( p->pPars->fUseDsd )
            {
                extern void If_ManCacheRecord( If_Man_t * p, int iDsd0, int iDsd1, int nShared, int iDsd );
                int truthId = Abc_Lit2Var(pCut->iCutFunc);
                if ( truthId >= Vec_IntSize(p->vTtDsds[pCut->nLeaves]) || Vec_IntEntry(p->vTtDsds[pCut->nLeaves], truthId) == -1 )
                {
                    while ( truthId >= Vec_IntSize(p->vTtDsds[pCut->nLeaves]) )
                    {
                        Vec_IntPush( p->vTtDsds[pCut->nLeaves], -1 );
                        for ( v = 0; v < Abc_MaxInt(6, pCut->nLeaves); v++ )
                            Vec_StrPush( p->vTtPerms[pCut->nLeaves], IF_BIG_CHAR );
                    }
                    iCutDsd = If_DsdManCompute( p->pIfDsdMan, If_CutTruthWR(p, pCut), pCut->nLeaves, (unsigned char *)If_CutDsdPerm(p, pCut), p->pPars->pLutStruct );
                    Vec_IntWriteEntry( p->vTtDsds[pCut->nLeaves], truthId, iCutDsd );
                }
                assert( If_DsdManSuppSize(p->pIfDsdMan, If_CutDsdLit(p, pCut)) == (int)pCut->nLeaves );
            }
            // run user functions
            pCut->fUseless = 0;
            if ( p->pPars->pFuncCell || p->pPars->pFuncCell2 )
            {
                assert( p->pPars->fUseTtPerm == 0 );
                assert( pCut->nLimit >= 4 && pCut->nLimit <= 16 );
                if ( p->pPars->fUseDsd )
                    pCut->fUseless = If_DsdManCheckDec( p->pIfDsdMan, If_CutDsdLit(p, pCut) );
                else if ( p->pPars->pFuncCell2 )
                    pCut->fUseless = !p->pPars->pFuncCell2( p, (word *)If_CutTruthW(p, pCut), pCut->nLeaves, NULL, NULL );
                else
                    pCut->fUseless = !p->pPars->pFuncCell( p, If_CutTruth(p, pCut), Abc_MaxInt(6, pCut->nLeaves), pCut->nLeaves, p->pPars->pLutStruct );
                p->nCutsUselessAll += pCut->fUseless;
                p->nCutsUseless[pCut->nLeaves] += pCut->fUseless;
                p->nCutsCountAll++;
                p->nCutsCount[pCut->nLeaves]++;
                // skip 5-input cuts, which cannot be decomposed
                if ( (p->pPars->fEnableCheck75 || p->pPars->fEnableCheck75u) && pCut->nLeaves == 5 && pCut->nLimit == 5 )
                {
                    extern int If_CluCheckDecInAny( word t, int nVars );
                    extern int If_CluCheckDecOut( word t, int nVars );
                    unsigned TruthU = *If_CutTruth(p, pCut);
                    word Truth = (((word)TruthU << 32) | (word)TruthU);
                    p->nCuts5++;
                    if ( If_CluCheckDecInAny( Truth, 5 ) )
                        p->nCuts5a++;
                    else
                        continue;
                }
                else if ( p->pPars->fVerbose && pCut->nLeaves == 5 )
                {
                    extern int If_CluCheckDecInAny( word t, int nVars );
                    extern int If_CluCheckDecOut( word t, int nVars );
                    unsigned TruthU = *If_CutTruth(p, pCut);
                    word Truth = (((word)TruthU << 32) | (word)TruthU);
                    p->nCuts5++;
                    if ( If_CluCheckDecInAny( Truth, 5 ) || If_CluCheckDecOut( Truth, 5 ) )
                        p->nCuts5a++;
                }
            }
            else if ( p->pPars->fUseDsdTune )
            {
                pCut->fUseless = If_DsdManReadMark( p->pIfDsdMan, If_CutDsdLit(p, pCut) );
                p->nCutsUselessAll += pCut->fUseless;
                p->nCutsUseless[pCut->nLeaves] += pCut->fUseless;
                p->nCutsCountAll++;
                p->nCutsCount[pCut->nLeaves]++;
            }
            else if ( p->pPars->fUse34Spec )
            {
                assert( pCut->nLeaves <= 4 );
                if ( pCut->nLeaves == 4 && !Abc_Tt4Check( (int)(0xFFFF & *If_CutTruth(p, pCut)) ) )
                    pCut->fUseless = 1;
            }
            else 
            {
                if ( p->pPars->fUseAndVars )
                {
                    int iDecMask = -1, truthId = Abc_Lit2Var(pCut->iCutFunc);
                    assert( p->pPars->nLutSize <= 13 );
                    if ( truthId >= Vec_IntSize(p->vTtDecs[pCut->nLeaves]) || Vec_IntEntry(p->vTtDecs[pCut->nLeaves], truthId) == -1 )
                    {
                        while ( truthId >= Vec_IntSize(p->vTtDecs[pCut->nLeaves]) )
                            Vec_IntPush( p->vTtDecs[pCut->nLeaves], -1 );
                        if ( (int)pCut->nLeaves > p->pPars->nLutSize / 2 && (int)pCut->nLeaves <= 2 * (p->pPars->nLutSize / 2) )
                            iDecMask = Abc_TtProcessBiDec( If_CutTruthWR(p, pCut), (int)pCut->nLeaves, p->pPars->nLutSize / 2 );
                        else
                            iDecMask = 0;
                        Vec_IntWriteEntry( p->vTtDecs[pCut->nLeaves], truthId, iDecMask );
                    }
                    iDecMask = Vec_IntEntry(p->vTtDecs[pCut->nLeaves], truthId);
                    assert( iDecMask >= 0 );
                    pCut->fUseless = (int)(iDecMask == 0 && (int)pCut->nLeaves > p->pPars->nLutSize / 2);
                    p->nCutsUselessAll += pCut->fUseless;
                    p->nCutsUseless[pCut->nLeaves] += pCut->fUseless;
                    p->nCutsCountAll++;
                    p->nCutsCount[pCut->nLeaves]++;
                }
                if ( p->pPars->fUseCofVars && (!p->pPars->fUseAndVars || pCut->fUseless) )
                {
                    int iCofVar = -1, truthId = Abc_Lit2Var(pCut->iCutFunc);
                    if ( truthId >= Vec_StrSize(p->vTtVars[pCut->nLeaves]) || Vec_StrEntry(p->vTtVars[pCut->nLeaves], truthId) == (char)-1 )
                    {
                        while ( truthId >= Vec_StrSize(p->vTtVars[pCut->nLeaves]) )
                            Vec_StrPush( p->vTtVars[pCut->nLeaves], (char)-1 );
                        iCofVar = Abc_TtCheckCondDep( If_CutTruthWR(p, pCut), pCut->nLeaves, p->pPars->nLutSize / 2 );
                        Vec_StrWriteEntry( p->vTtVars[pCut->nLeaves], truthId, (char)iCofVar );
                    }
                    iCofVar = Vec_StrEntry(p->vTtVars[pCut->nLeaves], truthId);
                    assert( iCofVar >= 0 && iCofVar <= (int)pCut->nLeaves );
                    pCut->fUseless = (int)(iCofVar == (int)pCut->nLeaves && pCut->nLeaves > 0);
                    p->nCutsUselessAll += pCut->fUseless;
                    p->nCutsUseless[pCut->nLeaves] += pCut->fUseless;
                    p->nCutsCountAll++;
                    p->nCutsCount[pCut->nLeaves]++;
                }
            }
        }
        
        // compute the application-specific cost and depth
        pCut->fUser = (p->pPars->pFuncCost != NULL);
        pCut->Cost = p->pPars->pFuncCost? p->pPars->pFuncCost(p, pCut) : 0;
        if ( pCut->Cost == IF_COST_MAX )
            continue;
        // check if the cut satisfies the required times
        if ( p->pPars->fDelayOpt )
            pCut->Delay = If_CutSopBalanceEval( p, pCut, NULL );
        else if ( p->pPars->fDsdBalance )
            pCut->Delay = If_CutDsdBalanceEval( p, pCut, NULL );
        else if ( p->pPars->fUserRecLib )
            pCut->Delay = If_CutDelayRecCost3( p, pCut, pObj );
        else if ( p->pPars->fUserLutDec )
        {
            pCut->Delay = If_LutDecEval( p, pCut, pObj, Mode == 0, fFirst );
            pCut->fUseless = pCut->Delay == ABC_INFINITY;
        }
        else if ( p->pPars->fUserLut2D )
        {
            pCut->Delay = If_Lut2DecEval( p, pCut, pObj, Mode == 0, fFirst );
            pCut->fUseless = pCut->Delay == ABC_INFINITY;
        }
        else if ( p->pPars->fUserSesLib )
        {
            int Cost = 0;
            pCut->fUser = 1;
            pCut->Delay = (float)Abc_ExactDelayCost( If_CutTruthW(p, pCut), If_CutLeaveNum(pCut), If_CutArrTimeProfile(p, pCut), If_CutPerm(pCut), &Cost, If_ManCutAigDelay(p, pObj, pCut) ); 
            if ( Cost == ABC_INFINITY )
            {
                for ( v = 0; v < If_CutLeaveNum(pCut); v++ )
                    If_CutPerm(pCut)[v] = IF_BIG_CHAR;
                pCut->Cost = IF_COST_MAX;
                pCut->fUseless = 1;
            }
        }
        else if ( p->pPars->fDelayOptLut )
            pCut->Delay = If_CutLutBalanceEval( p, pCut );
        else if( p->pPars->nGateSize > 0 )
            pCut->Delay = If_CutDelaySop( p, pCut );
        else 
            pCut->Delay = If_CutDelay( p, pObj, pCut );
        if ( pCut->Delay == -1 )
            continue;
        if ( Mode && pCut->Delay > pObj->Required + p->fEpsilon && pCutSet->nCuts > 0 )
            continue;
        
        // ========================================
        // ML-BASED CUT FILTERING (NEW)
        // ========================================
        //If ML cannot find better cuts, what is its purpose? Runtime acceleration through pruning.

        // Instead of letting ABC evaluate 500,000 cuts using expensive C-code heuristics, the ML model acts as a smart bouncer. 
        // It looks at the structural features of a cut and says, "This cut is guaranteed to be terrible (Class 4). 
        // Throw it in the trash now before ABC wastes CPU cycles calculating its exact Area Flow."

        // When your ML model works perfectly, it achieves the exact same Delay and Area as Vanilla ABC, 
        // but in a fraction of the CPU time. (Right now, your C implementation is slower because 
        // running a 4-layer MLP via sequential for loops on millions of cuts is bottlenecking the CPU, 
        // which is why gating it behind nLeaves >= 4 is necessary).

        // ML-BASED CUT SCORING (SLAP APPROACH)
        // ========================================
        // pCut->ml_score = 0.0f; // Default baseline for non-ML runs

        // if ( p->pPars->fTruth && useML && Mode == 0 && pCut->nLeaves >= 4)
        // {
        //     g_TotalCands++;
            
        //     // Extract features
        //     int vol_cut = pCut->nLeaves;
        //     int cut_height = If_ManCutAigDelay(p, pObj, pCut);
            
        //     unsigned canon_tt_0 = 0;
        //     unsigned canon_tt_1 = 0;
        //     word * pTruth = If_CutTruthW(p, pCut);
        //     canon_tt_0 = (unsigned)(pTruth[0] & 0xFFFFFFFF);
        //     canon_tt_1 = (unsigned)((pTruth[0] >> 32) & 0xFFFFFFFF);
            
        //     int num_fo = g_FanoutCounts[pObj->Id];
        //     int lvl = pObj->Level;
        //     int rev_lvl = g_RevLevels[pObj->Id];
            
        //     int c1_lvl = 0, c1_fo = 0, c1_inv = 0;
        //     int c2_lvl = 0, c2_fo = 0, c2_inv = 0;
            
        //     if ( If_ObjIsAnd(pObj) )
        //     {
        //         If_Obj_t * c1 = pObj->pFanin0;
        //         If_Obj_t * c2 = pObj->pFanin1;
        //         c1_lvl = c1->Level;
        //         c1_fo  = g_FanoutCounts[c1->Id];
        //         c1_inv = pObj->fCompl0;
        //         c2_lvl = c2->Level;
        //         c2_fo  = g_FanoutCounts[c2->Id];
        //         c2_inv = pObj->fCompl1;
        //     }
            

        //     // Build feature vector
        //     float features[INPUT_SIZE];
        //     features[0]  = (float)vol_cut;
        //     features[1]  = (float)cut_height;
        //     features[2]  = (float)num_fo;      
        //     features[3]  = (float)lvl;         
        //     features[4]  = (float)rev_lvl;
        //     features[5]  = (float)c1_lvl;
        //     features[6]  = (float)c1_fo;
        //     features[7]  = (float)c1_inv;
        //     features[8]  = (float)c2_lvl;
        //     features[9]  = (float)c2_fo;
        //     features[10] = (float)c2_inv;
            
            
        //     // ML prediction
        //     int pred_class = PredictClass(features);
        //     g_ClassDist[pred_class]++;  // Track distribution
        //     if (g_TotalCands == 1) { 
        //         printf("\nDEBUG - Cut 1 Normalized Features:\n");
        //         for (int j = 0; j < INPUT_SIZE; j++) {
        //             printf("%f ", features[j]);
        //         }
        //         printf("\n\n");
        //     }            
            // OPTION 1: Hard filter (Uncomment to enable strict drop)
            // Only drop the cut if we ALREADY have at least one valid cut saved
            // if (pred_class >= 3 && pCutSet->nCuts > 0) { 
            //     g_MLFiltered++;
            //     continue; 
            // }

            // INSTEAD OF DROPPING THE CUT, WE SCORE IT
            // Class 0 (Best) -> Class 4 (Worst)
            // pCut->ml_score = (float)pred_class;

            // OPTION 2: Soft Area/Cost Penalty
            // g_MLAdjusted++;
            // float ml_penalty = (float)(pred_class * pred_class);
            // pCut->Cost += (int)(ml_penalty * 50.0);  
            // pCut->Area += (float)(ml_penalty * 5.0); // Adjust area to influence default ABC heuristics


            //========================================
            //ML-BASED CUT SCORING (SLAP APPROACH)
            //========================================
            pCut->ml_score = 0.0f; // Default priority for trivial cuts & Vanilla runs
            
            if ( p->pPars->fTruth && useML && Mode == 0 && pCut->nLeaves >= 4)
            {
                g_TotalCands++;
                
                // 1. Basic Features
                int vol_cut = pCut->nLeaves;
                int cut_height = If_ManCutAigDelay(p, pObj, pCut);
                int num_fo = g_FanoutCounts[pObj->Id];
                int lvl = pObj->Level;
                int rev_lvl = g_RevLevels[pObj->Id];
                
                // 2. Child Features
                int c1_lvl = 0, c1_fo = 0, c1_inv = 0;
                int c2_lvl = 0, c2_fo = 0, c2_inv = 0;
                
                if ( If_ObjIsAnd(pObj) )
                {
                    If_Obj_t * c1 = pObj->pFanin0;
                    If_Obj_t * c2 = pObj->pFanin1;
                    c1_lvl = c1->Level;
                    c1_fo  = g_FanoutCounts[c1->Id];
                    c1_inv = pObj->fCompl0;
                    c2_lvl = c2->Level;
                    c2_fo  = g_FanoutCounts[c2->Id];
                    c2_inv = pObj->fCompl1;
                }
                
                // 3. Aggregate Leaf Features
                int max_leaf_lvl = 0;
                int min_leaf_rev = 999999;
                int sum_leaf_fo = 0;
                
                for (int v = 0; v < pCut->nLeaves; v++) {
                    int leaf_id = pCut->pLeaves[v];
                    If_Obj_t * pLeaf = If_ManObj(p, leaf_id);
                    
                    int l_lvl = pLeaf->Level;
                    int l_rev = g_RevLevels[leaf_id];
                    int l_fo  = g_FanoutCounts[leaf_id];
                    
                    if (l_lvl > max_leaf_lvl) max_leaf_lvl = l_lvl;
                    if (l_rev < min_leaf_rev) min_leaf_rev = l_rev;
                    sum_leaf_fo += l_fo;
                }
                float mean_leaf_fo = ((float)sum_leaf_fo / (float)pCut->nLeaves);

                // 4. Build Feature Vector (Size 14)
                float features[INPUT_SIZE];
                features[0]  = (float)vol_cut;
                features[1]  = (float)cut_height;
                features[2]  = (float)num_fo;      
                features[3]  = (float)lvl;         
                features[4]  = (float)rev_lvl;
                features[5]  = (float)c1_lvl;
                features[6]  = (float)c1_fo;
                features[7]  = (float)c1_inv;
                features[8]  = (float)c2_lvl;
                features[9]  = (float)c2_fo;
                features[10] = (float)c2_inv;
                features[11] = (float)max_leaf_lvl;
                features[12] = (float)min_leaf_rev;
                features[13] = (float)mean_leaf_fo;
                
                // 5. ML Prediction (Class 0 to 4)
                int pred_class = PredictClass(features);
                g_ClassDist[pred_class]++;  
                
                pCut->ml_score = (float)pred_class; 
            }
            //========================================
        // ULTRA-FAST ML SCORING (O(1) Features Only)
        // ========================================
        // pCut->ml_score = 0.0f; 
        
        // if ( p->pPars->fTruth && useML && Mode == 0 && pCut->nLeaves >= 4)
        // {
        //     g_TotalCands++;
            
        //     // 1. FAST Features (Instant memory lookups)
        //     int vol_cut = pCut->nLeaves;
        //     int num_fo = g_FanoutCounts[pObj->Id];
        //     int lvl = pObj->Level;
        //     int rev_lvl = g_RevLevels[pObj->Id];
            
        //     int c1_lvl = 0, c1_fo = 0, c1_inv = 0;
        //     int c2_lvl = 0, c2_fo = 0, c2_inv = 0;
            
        //     if ( If_ObjIsAnd(pObj) )
        //     {
        //         If_Obj_t * c1 = pObj->pFanin0;
        //         If_Obj_t * c2 = pObj->pFanin1;
        //         c1_lvl = c1->Level;
        //         c1_fo  = g_FanoutCounts[c1->Id];
        //         c1_inv = pObj->fCompl0;
        //         c2_lvl = c2->Level;
        //         c2_fo  = g_FanoutCounts[c2->Id];
        //         c2_inv = pObj->fCompl1;
        //     }
            
        //     // NOTE: We deleted cut_height and the entire for-loop over the leaves!

        //     // 2. Build Feature Vector (Size 10)
        //     float features[INPUT_SIZE];
        //     features[0] = (float)vol_cut;
        //     features[1] = (float)num_fo;      
        //     features[2] = (float)lvl;         
        //     features[3] = (float)rev_lvl;
        //     features[4] = (float)c1_lvl;
        //     features[5] = (float)c1_fo;
        //     features[6] = (float)c1_inv;
        //     features[7] = (float)c2_lvl;
        //     features[8] = (float)c2_fo;
        //     features[9] = (float)c2_inv;
            
        //     // 3. Ultra-Fast Shallow DT Prediction
        //     int pred_class = PredictClass(features);
        //     g_ClassDist[pred_class]++;  
            
        //     pCut->ml_score = (float)pred_class; 
        // }
        // ========================================
        
        // compute area of the cut (this area may depend on the application specific cost)
        if (Mode == 2)
            pCut->Area += If_CutAreaDerefed( p, pCut );
        else
            pCut->Area += If_CutAreaFlow( p, pCut );

        if ( p->pPars->fEdge )
            pCut->Edge = (Mode == 2)? If_CutEdgeDerefed( p, pCut ) : If_CutEdgeFlow( p, pCut );
        if ( p->pPars->fPower )
            pCut->Power = (Mode == 2)? If_CutPowerDerefed( p, pCut, pObj ) : If_CutPowerFlow( p, pCut, pObj );

        // insert the cut into storage
        If_CutSort( p, pCutSet, pCut );
    } 
    assert( pCutSet->nCuts > 0 );

    // update the best cut
    if ( !fPreprocess || pCutSet->ppCuts[0]->Delay <= pObj->Required + p->fEpsilon )
    {
        If_CutCopy( p, If_ObjCutBest(pObj), pCutSet->ppCuts[0] );
        if ( p->pPars->fUserRecLib || p->pPars->fUserSesLib )
            assert(If_ObjCutBest(pObj)->Cost < IF_COST_MAX && If_ObjCutBest(pObj)->Delay < ABC_INFINITY);
    }
    if ( p->vCuts ) {
        extern void If_ManDumpCutsAndCost( If_Man_t * p, If_Obj_t * pObj, Vec_Int_t * vCuts, Vec_Int_t * vCutCosts );
        If_ManDumpCutsAndCost( p, pObj, p->vCuts, p->vCutCosts );
    }
    // add the trivial cut to the set
    if ( !pObj->fSkipCut && If_ObjCutBest(pObj)->nLeaves > 1 )
    {
        If_ManSetupCutTriv( p, pCutSet->ppCuts[pCutSet->nCuts++], pObj->Id );
        assert( pCutSet->nCuts <= pCutSet->nCutsMax+1 );
    }

    // ref the selected cut
    if ( Mode && pObj->nRefs > 0 )
        If_CutAreaRef( p, If_ObjCutBest(pObj) );
    if ( If_ObjCutBest(pObj)->fUseless )
        Abc_Print( 1, "The best cut is useless.\n" );
    
    // Dump cut metrics to CSV for analysis
    If_ObjForEachCut( pObj, pCut, i )
    {
        if (p->pPars->pFuncUser)
            p->pPars->pFuncUser( p, pObj, pCut );
        
        int root_idx = pObj->Id;
        int cut_idx = g_CutId++;
        
        int vol_cut = pCut->nLeaves;
        int cut_height = If_ManCutAigDelay(p, pObj, pCut);
        
        unsigned canon_tt_0 = 0;
        unsigned canon_tt_1 = 0;
        if (p->pPars->fTruth) {
            word * pTruth = If_CutTruthW(p, pCut);
            canon_tt_0 = (unsigned)(pTruth[0] & 0xFFFFFFFF);
            canon_tt_1 = (unsigned)((pTruth[0] >> 32) & 0xFFFFFFFF);
        }
        
        int leaves[5] = {-1, -1, -1, -1, -1};
        int v;
        for (v = 0; v < pCut->nLeaves && v < 5; v++)
            leaves[v] = pCut->pLeaves[v];
        
        fprintf(g_CutDelayFile,
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%f\n",
            root_idx,
            cut_idx,
            leaves[0], leaves[1], leaves[2], leaves[3], leaves[4],
            vol_cut,
            cut_height,
            canon_tt_0,
            canon_tt_1,
            pCut->Delay
        );
    }
    fflush(g_CutDelayFile);
    // free the cuts
    If_ManDerefNodeCutSet( p, pObj );
}

/**Function*************************************************************

  Synopsis    [Finds the best cut for the choice node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_ObjPerformMappingChoice( If_Man_t * p, If_Obj_t * pObj, int Mode, int fPreprocess )
{
    If_Set_t * pCutSet;
    If_Obj_t * pTemp;
    If_Cut_t * pCutTemp, * pCut;
    int i, fSave0 = p->pPars->fDelayOpt || p->pPars->fDelayOptLut || p->pPars->fDsdBalance || p->pPars->fUserRecLib || p->pPars->fUserSesLib || p->pPars->fUse34Spec || p->pPars->fUserLutDec || p->pPars->fUserLut2D;
    assert( pObj->pEquiv != NULL );

    // prepare
    if ( Mode && pObj->nRefs > 0 )
        If_CutAreaDeref( p, If_ObjCutBest(pObj) );

    // remove elementary cuts
    for ( pTemp = pObj; pTemp; pTemp = pTemp->pEquiv )
        if ( pTemp != pObj || pTemp->pCutSet->nCuts > 1 )
            pTemp->pCutSet->nCuts--;

    // update the cutset of the node
    pCutSet = pObj->pCutSet;

    // generate cuts
    for ( pTemp = pObj->pEquiv; pTemp; pTemp = pTemp->pEquiv )
    {
        if ( pTemp->pCutSet->nCuts == 0 )
            continue;
        // go through the cuts of this node
        If_ObjForEachCut( pTemp, pCutTemp, i )
        {
            if ( pCutTemp->fUseless )
                continue;
            // get the next free cut
            assert( pCutSet->nCuts <= pCutSet->nCutsMax );
            pCut = pCutSet->ppCuts[pCutSet->nCuts];
            // copy the cut into storage
            If_CutCopy( p, pCut, pCutTemp );
            // check if this cut is contained in any of the available cuts
            if ( If_CutFilter( pCutSet, pCut, fSave0 ) )
                continue;
            // check if the cut satisfies the required times
            if ( Mode && pCut->Delay > pObj->Required + p->fEpsilon && pCutSet->nCuts > 0 )
                continue;
            // set the phase attribute
            pCut->fCompl = pObj->fPhase ^ pTemp->fPhase;
            // compute area of the cut (this area may depend on the application specific cost)
            pCut->Area = (Mode == 2)? If_CutAreaDerefed( p, pCut ) : If_CutAreaFlow( p, pCut );
            if ( p->pPars->fEdge )
                pCut->Edge = (Mode == 2)? If_CutEdgeDerefed( p, pCut ) : If_CutEdgeFlow( p, pCut );
            if ( p->pPars->fPower )
                pCut->Power = (Mode == 2)? If_CutPowerDerefed( p, pCut, pObj ) : If_CutPowerFlow( p, pCut, pObj );
            // insert the cut into storage
            If_CutSort( p, pCutSet, pCut );
        }
    } 
    assert( pCutSet->nCuts > 0 );

    // update the best cut
    if ( !fPreprocess || pCutSet->ppCuts[0]->Delay <= pObj->Required + p->fEpsilon )
        If_CutCopy( p, If_ObjCutBest(pObj), pCutSet->ppCuts[0] );
    
    // add the trivial cut to the set
    if ( !pObj->fSkipCut && If_ObjCutBest(pObj)->nLeaves > 1 )
    {
        If_ManSetupCutTriv( p, pCutSet->ppCuts[pCutSet->nCuts++], pObj->Id );
        assert( pCutSet->nCuts <= pCutSet->nCutsMax+1 );
    }

    // ref the selected cut
    if ( Mode && pObj->nRefs > 0 )
        If_CutAreaRef( p, If_ObjCutBest(pObj) );
    // free the cuts
    If_ManDerefChoiceCutSet( p, pObj );
}

/**Function*************************************************************

  Synopsis    [Performs one mapping pass over all nodes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void ComputeFanouts( If_Man_t * p )
{
    If_Obj_t * pObj;
    int i;

    // allocate
    if (g_FanoutCounts)
        ABC_FREE(g_FanoutCounts);

    g_FanoutCounts = ABC_CALLOC(int, If_ManObjNum(p));

    // traverse all nodes
    If_ManForEachObj(p, pObj, i)
    {
        if ( If_ObjIsAnd(pObj) )
        {
            g_FanoutCounts[pObj->pFanin0->Id]++;
            g_FanoutCounts[pObj->pFanin1->Id]++;
        }
        else if ( If_ObjIsCo(pObj) )
        {
            g_FanoutCounts[If_ObjFanin0(pObj)->Id]++;
        }
    }
}
void BuildFanoutLists( If_Man_t * p )
{
    If_Obj_t * pObj;
    int i;

    int nObjs = If_ManObjNum(p);

    // free old
    if (g_Fanouts)
    {
        for (i = 0; i < nObjs; i++)
            if (g_Fanouts[i])
                Vec_IntFree(g_Fanouts[i]);
        ABC_FREE(g_Fanouts);
    }

    // allocate
    g_Fanouts = ABC_ALLOC(Vec_Int_t *, nObjs);
    for (i = 0; i < nObjs; i++)
        g_Fanouts[i] = Vec_IntAlloc(4);

    // fill
    If_ManForEachObj(p, pObj, i)
    {
        if ( If_ObjIsAnd(pObj) )
        {
            Vec_IntPush(g_Fanouts[If_ObjFanin0(pObj)->Id], pObj->Id);
            Vec_IntPush(g_Fanouts[If_ObjFanin1(pObj)->Id], pObj->Id);
        }
        else if ( If_ObjIsCo(pObj) )
        {
            Vec_IntPush(g_Fanouts[If_ObjFanin0(pObj)->Id], pObj->Id);
        }
    }
}
void ComputeReverseLevels( If_Man_t * p )
{
    If_Obj_t * pObj;
    int i, k;

    int nObjs = If_ManObjNum(p);

    if (g_RevLevels)
        ABC_FREE(g_RevLevels);

    g_RevLevels = ABC_ALLOC(int, nObjs);

    for (i = 0; i < nObjs; i++)
        g_RevLevels[i] = -1;

    // CO = 0
    If_ManForEachObj(p, pObj, i)
    {
        if ( If_ObjIsCo(pObj) )
            g_RevLevels[pObj->Id] = 0;
    }

    // reverse topo order
    for (i = nObjs - 1; i >= 0; i--)
    {
        pObj = If_ManObj(p, i);

        // skip CO (already set)
        if ( If_ObjIsCo(pObj) )
            continue;

        int max_lvl = -1;

        Vec_Int_t * vFanouts = g_Fanouts[pObj->Id];

        int fanout_id;
        Vec_IntForEachEntry(vFanouts, fanout_id, k)
        {
            if (g_RevLevels[fanout_id] != -1)
                max_lvl = Abc_MaxInt(max_lvl, g_RevLevels[fanout_id]);
        }

        if (max_lvl >= 0)
            g_RevLevels[pObj->Id] = max_lvl + 1;
    }

    // unreachable
    for (i = 0; i < nObjs; i++)
    {
        if (g_RevLevels[i] == -1)
            g_RevLevels[i] = 255;
    }
}
void DumpNodeEmbeddings( If_Man_t * p )
{
    If_Obj_t * pObj;
    int i;


    If_ManForEachObj(p, pObj, i)
    {
        int node_idx = pObj->Id;
        int num_fo = g_FanoutCounts[pObj->Id];
        int lvl      = pObj->Level;
        int rev_lvl = g_RevLevels[pObj->Id];

        int c1_lvl = 0, c1_fo = 0, c1_inv = 0;
        int c2_lvl = 0, c2_fo = 0, c2_inv = 0;

        if ( If_ObjIsAnd(pObj) )
        {
            If_Obj_t * c1 = pObj->pFanin0;
            If_Obj_t * c2 = pObj->pFanin1;

            c1_lvl = c1->Level;
            c1_fo  = g_FanoutCounts[c1->Id];
            c1_inv = pObj->fCompl0;

            c2_lvl = c2->Level;
            c2_fo  = g_FanoutCounts[c2->Id];
            c2_inv = pObj->fCompl1;
        }
        else if ( If_ObjIsCo(pObj) )
        {
            If_Obj_t * c1 = If_ObjFanin0(pObj);

            c1_lvl = c1->Level;
            c1_fo  = g_FanoutCounts[c1->Id];
            c1_inv = 0;  // CO edge usually not stored as complemented

            // c2 remains 0
        }

        fprintf(g_NodeFile,
            "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            node_idx,
            num_fo,
            lvl,
            rev_lvl,
            c1_lvl, c1_fo, c1_inv,
            c2_lvl, c2_fo, c2_inv
        );
    }

    fflush(g_NodeFile);
}

void FreeMLData(If_Man_t * p)
{
    int i;
    int nObjs = If_ManObjNum(p);

    if (g_FanoutCounts) {
        ABC_FREE(g_FanoutCounts);
        g_FanoutCounts = NULL;
    }

    if (g_RevLevels) {
        ABC_FREE(g_RevLevels);
        g_RevLevels = NULL;
    }

    if (g_Fanouts) {
        for (i = 0; i < nObjs; i++) {
            if (g_Fanouts[i]) {
                Vec_IntFree(g_Fanouts[i]);
            }
        }
        ABC_FREE(g_Fanouts);
        g_Fanouts = NULL;
    }
}

int If_ManPerformMappingRound( If_Man_t * p, int nCutsUsed, int Mode, int fPreprocess, int fFirst, char * pLabel )
{
    // ML model setup
    static int model_loaded = 0;
    if (!model_loaded) {
        LoadModelWeights();
        // LoadScaler();
        model_loaded = 1;
    }
    
    // Reset ML statistics for this round
    g_TotalCands = 0;
    g_MLFiltered = 0;
    g_MLAdjusted = 0;
    for (int i = 0; i < 5; i++)
        g_ClassDist[i] = 0;

    // Safely open CSV files in append mode so consecutive map runs don't overwrite
    if ( g_CutDelayFile == NULL ) {
        g_CutId = 0;
        g_CutDelayFile = fopen("cut_delay.csv", "a");
        fseek(g_CutDelayFile, 0, SEEK_END);
        if (ftell(g_CutDelayFile) == 0) {
            fprintf(g_CutDelayFile, "root_idx,cut_idx,l1_idx,l2_idx,l3_idx,l4_idx,l5_idx,vol_cut,cut_height,canon_tt_0,canon_tt_1,delay\n");
        }
    }
    if (g_NodeFile == NULL) {
        g_NodeFile = fopen("node_embeddings.csv", "a");
        fseek(g_NodeFile, 0, SEEK_END);
        if (ftell(g_NodeFile) == 0) {
            fprintf(g_NodeFile, "node_idx,num_fo,lvl,rev_lvl,c1_lvl,c1_fo,c1_inv,c2_lvl,c2_fo,c2_inv\n");
        }
    }

    ComputeFanouts(p);
    BuildFanoutLists(p);   
    ComputeReverseLevels(p);

    ProgressBar * pProgress = NULL;
    If_Obj_t * pObj;
    int i;
    abctime clk = Abc_Clock();
    float arrTime;
    assert( Mode >= 0 && Mode <= 2 );
    p->nBestCutSmall[0] = p->nBestCutSmall[1] = 0;
    
    // set the sorting function
    if ( Mode || p->pPars->fArea ) 
        p->SortMode = 1;
    else if ( p->pPars->fFancy )
        p->SortMode = 2;
    else
        p->SortMode = 0;
        
    p->nCutsUsed   = nCutsUsed;
    p->nCutsMerged = 0;
    
    If_ManForEachNode( p, pObj, i )
        assert( pObj->nVisits == pObj->nVisitsCopy );
        
    // map the internal nodes
    if ( p->pManTim != NULL )
    {
        Tim_ManIncrementTravId( p->pManTim );
        If_ManForEachObj( p, pObj, i )
        {
            if ( If_ObjIsAnd(pObj) )
            {
                If_ObjPerformMappingAnd( p, pObj, Mode, fPreprocess, fFirst );
                if ( pObj->fRepr )
                    If_ObjPerformMappingChoice( p, pObj, Mode, fPreprocess );
            }
            else if ( If_ObjIsCi(pObj) )
            {
                arrTime = Tim_ManGetCiArrival( p->pManTim, pObj->IdPio );
                If_ObjSetArrTime( pObj, arrTime );
            }
            else if ( If_ObjIsCo(pObj) )
            {
                arrTime = If_ObjArrTime( If_ObjFanin0(pObj) );
                Tim_ManSetCoArrival( p->pManTim, pObj->IdPio, arrTime );
            }
            else if ( If_ObjIsConst1(pObj) )
            {
                arrTime = -IF_INFINITY;
                If_ObjSetArrTime( pObj, arrTime );
            }
            else
                assert( 0 );
        }
    }
    else
    {
        pProgress = Extra_ProgressBarStart( stdout, If_ManObjNum(p) );
        If_ManForEachNode( p, pObj, i )
        {
            Extra_ProgressBarUpdate( pProgress, i, pLabel );
            If_ObjPerformMappingAnd( p, pObj, Mode, fPreprocess, fFirst );
            if ( pObj->fRepr )
                If_ObjPerformMappingChoice( p, pObj, Mode, fPreprocess );
        }
    }
    Extra_ProgressBarStop( pProgress );
    
    If_ManForEachNode( p, pObj, i )
        assert( pObj->nVisits == 0 );
        
    // compute required times and stats
    If_ManComputeRequired( p );

    if ( p->pPars->fVerbose )
    {
        char Symb = fPreprocess? 'P' : ((Mode == 0)? 'D' : ((Mode == 1)? 'F' : 'A'));
        Abc_Print( 1, "%c:  Del = %7.2f.  Ar = %9.1f.  Edge = %8d.  ", 
            Symb, p->RequiredGlo, p->AreaGlo, p->nNets );
        if ( p->dPower )
        Abc_Print( 1, "Switch = %7.2f.  ", p->dPower );
        Abc_Print( 1, "Cut = %8d.  ", p->nCutsMerged );
        Abc_PrintTime( 1, "T", Abc_Clock() - clk );
    }
    
    // Print ML filtering statistics
    if (g_TotalCands > 0) {
        float filter_rate = 100.0 * (float)g_MLFiltered / (float)g_TotalCands;
        Abc_Print( 1, "ML Filtering Stats: Total=%d, Filtered=%d (%.1f%%), Adjusted=%d\n",
            g_TotalCands, g_MLFiltered, filter_rate, g_MLAdjusted);
        Abc_Print( 1, "  Class Distribution: [%d, %d, %d, %d, %d]\n",
            g_ClassDist[0], g_ClassDist[1], g_ClassDist[2], g_ClassDist[3], g_ClassDist[4]);
    }
    
    DumpNodeEmbeddings(p);
    
    if (g_NodeFile) {
        fclose(g_NodeFile);
        g_NodeFile = NULL;
    }
    if (g_CutDelayFile) {
        fclose(g_CutDelayFile);
        g_CutDelayFile = NULL;
    }
    
    // Free ML structures at the end of mapping round to avoid leaks
    FreeMLData(p);
    
    return 1;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////

ABC_NAMESPACE_IMPL_END