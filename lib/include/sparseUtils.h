#pragma once

// Standard C++ includes
#include <string>

// Standard C includes
#include <cmath>
#include <cstdlib>
#include <cstdio>

// GeNN includes
#include "global.h"
#include "sparseProjection.h"

using namespace std;


//--------------------------------------------------------------------------
/*!
  \brief Utility to count how many entries above a specified value exist in a float array
*/
//--------------------------------------------------------------------------

template <class DATATYPE>
unsigned int countEntriesAbove(DATATYPE *Array, int sz, double includeAbove)
{
    int count = 0;
    for (int i = 0; i < sz; i++) {
        if (abs(Array[i]) > includeAbove) count++;
    }
    fprintf(stdout, "\nCounted %u nonzero entries\n\n", count);
    return count;

}


//--------------------------------------------------------------------------
/*!
  \brief DEPRECATED Utility to get a synapse weight from a SPARSE structure by x,y coordinates
 NB: as the SparseProjection struct doesnt hold the preN size (it should!) it is not possible
 to check the parameter validity. This fn may therefore crash unless user knows max poss X
*/
//--------------------------------------------------------------------------

template <class DATATYPE>
DATATYPE getG(DATATYPE *wuvar, SparseProjection *sparseStruct, int x, int y)
{
    fprintf(stderr,"WARNING: This function is deprecated, and if you are still using it \n\
  you are probably trying to use the old sparse structures containing the g array.  \n\
  Conductance structures have changed: conductance values should be defined as synapse variables now; \n\
  the structure is renamed as \"SparseProjection\" and contains only indexing arrays. \n\n\
  The replacement function for getG is \n\
  getSparseVar(DATATYPE * wuvar, SparseProjection  * sparseStruct, int x, int y).\n\n\
  calling getSparseVar...");
    getSparseVar(wuvar, &sparseStruct, x, y);
}

template <class DATATYPE>
float getSparseVar(DATATYPE *wuvar, SparseProjection *sparseStruct, unsigned int x, unsigned int y)
{
    DATATYPE g = 0.0; //default return value implies zero weighted for x,y
    int startSynapse = sparseStruct->indInG[x];
    int endSynapse = sparseStruct->indInG[x+1];
    for (int syn = startSynapse; syn < endSynapse; syn++) {
        if (sparseStruct->ind[syn]==y) {//look for index y
            g = wuvar[syn]; //set the real g
            break; //stop looking
        }
    }
    return g;
}


//--------------------------------------------------------------------------
/*!
  \brief Function for setting the values of SPARSE connectivity matrix
*/
//--------------------------------------------------------------------------

template <class DATATYPE>
void setSparseConnectivityFromDense(DATATYPE *wuvar, int preN, int postN, DATATYPE *tmp_gRNPN, SparseProjection *sparseStruct)
{
    int synapse = 0;
    sparseStruct->indInG[0] = 0; //first neuron always gets first synapse listed.
    for (int pre = 0; pre < preN; ++pre) {
        for (int post = 0; post < postN; ++post) {
            DATATYPE g = tmp_gRNPN[pre * postN + post];
            if (g > GENN_PREFERENCES::asGoodAsZero) {
                sparseStruct->ind[synapse] = post;
                wuvar[synapse] = g;
                synapse ++;
            }
        }
        sparseStruct->indInG[pre + 1] = synapse; //write start of next group
    }
}



//--------------------------------------------------------------------------
/*!
  \brief Utility to generate the SPARSE connectivity structure from a simple all-to-all array
*/
//--------------------------------------------------------------------------

template <class DATATYPE>
void createSparseConnectivityFromDense(DATATYPE *wuvar, int preN, int postN, DATATYPE *tmp_gRNPN, SparseProjection *sparseStruct, bool runTest) {
    sparseStruct->connN = countEntriesAbove(tmp_gRNPN, preN * postN, GENN_PREFERENCES::asGoodAsZero);
    //sorry -- this is not functional anymore 

    //allocateSparseArray(sparseStruct, sparseStruct.connN, preN, false);
    int synapse = 0;
    sparseStruct->indInG[0] = 0; //first neuron always gets first synapse listed.
    for (int pre = 0; pre < preN; ++pre) {
        for (int post = 0; post < postN; ++post) {
            DATATYPE g = tmp_gRNPN[pre * postN + post];
            if (g > GENN_PREFERENCES::asGoodAsZero) {
                sparseStruct->ind[synapse] = post;
                wuvar[synapse] = g;
                synapse ++;
            }
        }
        sparseStruct->indInG[pre + 1] = synapse; //write start of next group
    }
    if (!runTest) return;

    //test correct
    int fails = 0;
    for (int test = 0; test < 10; ++test) {
        int randX = rand() % preN;
        int randY = rand() % postN;
        float denseResult = tmp_gRNPN[randX * postN + randY];
        float sparseResult = getG(wuvar, sparseStruct,randX,randY);
        if (abs(denseResult-sparseResult) > GENN_PREFERENCES::asGoodAsZero) fails++;
    }
    if (fails > 0 ) {
        fprintf(stderr, "ERROR: Sparse connectivity generator failed for %u out of 10 random checks.\n", fails);
        exit(1);
    }
    fprintf(stdout, "Sparse connectivity generator passed %u out of 10 random checks.\n", fails);
}


//---------------------------------------------------------------------
/*! \brief  Utility to generate the YALE array structure with post-to-pre arrangement from the original pre-to-post arrangement where postsynaptic feedback is necessary (learning etc)
 */
//---------------------------------------------------------------------
void createPosttoPreArray(unsigned int preN, unsigned int postN, SparseProjection *C);

//---------------------------------------------------------------------
/*! \brief  Utility to generate the RAGGED array structure with post-to-pre arrangement from the original pre-to-post arrangement where postsynaptic feedback is necessary (learning etc)
 */
//---------------------------------------------------------------------
template<typename PostIndexType>
void createPosttoPreArray(unsigned int preN, unsigned int postN, RaggedProjection<PostIndexType> * C)
{
    // Zero column lengths
    std::fill_n(C->colLength, postN, 0);
    
    // Loop through presynaptic neurons
    for (unsigned int i = 0; i < preN; i++) {
        // Loop through synapses in corresponding matrix row
        for(unsigned int j = 0; j < C->rowLength[i]; j++) {
            // Calculate index of this synapse in the row-major matrix
            const unsigned int rowMajorIndex = (i * C->maxRowLength) + j;
            
            // Using this, lookup postsynaptic target
            const unsigned int postIndex = C->ind[rowMajorIndex];

            // From this calculate index of this synapse in the column-major matrix
            const unsigned int colMajorIndex = (postIndex * C->maxColLength) + C->colLength[postIndex];
            
            // Increment column length corresponding to this postsynaptic neuron
            C->colLength[postIndex]++;
            
            // Add remapping entry
            C->remap[colMajorIndex] = rowMajorIndex;
        }
    }
}
//--------------------------------------------------------------------------
/*! \brief Function to create the mapping from the normal index array "ind" to the "reverse" array revInd, i.e. the inverse mapping of remap. 
This is needed if SynapseDynamics accesses pre-synaptic variables.
 */
//--------------------------------------------------------------------------

void createPreIndices(unsigned int preN, unsigned int postN, SparseProjection *C);

template<typename PostIndexType>
void createPreIndices(unsigned int preN, unsigned int, RaggedProjection<PostIndexType> * C)
{
    unsigned int &synRemapCount = C->synRemap[0];
    unsigned int *synRemap = &C->synRemap[1];
    // Loop through presynaptic neurons
    synRemapCount  = 0;
    for (unsigned int i = 0; i < preN; i++) {
        // Loop through synapses in corresponding matrix row
        for(unsigned int j = 0; j < C->rowLength[i]; j++) {
            synRemap[synRemapCount++] = (i * C->maxRowLength) + j;
        }
    }
}

#ifndef CPU_ONLY
//--------------------------------------------------------------------------
/*! \brief Function for initializing conductance array indices for sparse matrices on the GPU
(by copying the values from the host)
 */
//--------------------------------------------------------------------------

void initializeSparseArray(const SparseProjection &C,  unsigned int *dInd, unsigned int *dIndInG, unsigned int preN);


//--------------------------------------------------------------------------
/*! \brief Function for initializing conductance array indices for sparse matrices on the GPU
(by copying the values from the host)
 */
//--------------------------------------------------------------------------
template<typename PostIndexType>
void initializeRaggedArray(const RaggedProjection<PostIndexType> &C, PostIndexType *dInd, unsigned int *dRowLength, unsigned int preN)
{
    CHECK_CUDA_ERRORS(cudaMemcpy(dInd, C.ind, C.maxRowLength * preN * sizeof(PostIndexType), cudaMemcpyHostToDevice));
    CHECK_CUDA_ERRORS(cudaMemcpy(dRowLength, C.rowLength, preN * sizeof(unsigned int), cudaMemcpyHostToDevice));
}

//--------------------------------------------------------------------------
/*! \brief Function for initializing reversed conductance array indices for sparse matrices on the GPU
(by copying the values from the host)
 */
//--------------------------------------------------------------------------

void initializeSparseArrayRev(const SparseProjection &C,  unsigned int *dRevInd, unsigned int *dRevIndInG, unsigned int *dRemap, unsigned int postN);

//--------------------------------------------------------------------------
/*! \brief Function for initializing reversed conductance arrays presynaptic indices for sparse matrices on  the GPU
(by copying the values from the host)
 */
//--------------------------------------------------------------------------

void initializeSparseArrayPreInd(const SparseProjection &C,  unsigned int * dPreInd);

//--------------------------------------------------------------------------
/*! \brief Function for initializing reversed conductance array indices for sparse matrices on the GPU
(by copying the values from the host)
 */
//--------------------------------------------------------------------------
template<typename PostIndexType>
void initializeRaggedArrayRev(const RaggedProjection<PostIndexType> &C, unsigned int *dColLength, unsigned int *dRemap, unsigned int postN)
{
    CHECK_CUDA_ERRORS(cudaMemcpy(dColLength, C.colLength, postN * sizeof(unsigned int), cudaMemcpyHostToDevice));
    CHECK_CUDA_ERRORS(cudaMemcpy(dRemap, C.remap, C.maxColLength * postN * sizeof(unsigned int), cudaMemcpyHostToDevice));
}

//--------------------------------------------------------------------------
/*! \brief Function for initializing reversed conductance arrays presynaptic indices for sparse matrices on  the GPU
(by copying the values from the host)
 */
//--------------------------------------------------------------------------
template<typename PostIndexType>
void initializeRaggedArraySynRemap(const RaggedProjection<PostIndexType> &C,  unsigned int *dSynRemap)
{
    CHECK_CUDA_ERRORS(cudaMemcpy(dSynRemap, C.synRemap, (C.synRemap[0] + 1) * sizeof(unsigned int), cudaMemcpyHostToDevice));
}
#endif  // CPU_ONLY