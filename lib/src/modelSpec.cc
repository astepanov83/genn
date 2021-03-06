/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
              Falmer, Brighton BN1 9QJ, UK
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
   
   This file contains neuron model definitions.
  
--------------------------------------------------------------------------*/

#ifndef MODELSPEC_CC
#define MODELSPEC_CC

// Standard C++ includes
#include <algorithm>
#include <numeric>
#include <typeinfo>

// Standard C includes
#include <cstdio>
#include <cmath>
#include <cassert>

// GeNN includes
#include "codeGenUtils.h"
#include "global.h"
#include "modelSpec.h"
#include "standardSubstitutions.h"
#include "utils.h"

unsigned int GeNNReady = 0;

//! \brief Method for GeNN initialisation (by preparing standard models)
    
void initGeNN()
{
    prepareStandardModels();
    preparePostSynModels();
    prepareWeightUpdateModels();
    GeNNReady= 1;
}

// ------------------------------------------------------------------------
// Anonymous namespace
// ------------------------------------------------------------------------
namespace
{
void createVarInitialiserFromLegacyVars(const std::vector<double> &ini, std::vector<NewModels::VarInit> &varInitialisers)
{
    varInitialisers.reserve(ini.size());
    std::transform(ini.cbegin(), ini.cend(), std::back_inserter(varInitialisers),
                   [](double v)
                   {
                       return NewModels::VarInit(v);
                   });
}
}

// ------------------------------------------------------------------------
// NNmodel
// ------------------------------------------------------------------------
// class NNmodel for specifying a neuronal network model

NNmodel::NNmodel()
{
    final= false;
    setDT(0.5);
    setPrecision(GENN_FLOAT);
    setTiming(false);
    RNtype= "uint64_t";
#ifndef CPU_ONLY
    setGPUDevice(AUTODEVICE);
#endif
    setSeed(0);
}

NNmodel::~NNmodel() 
{
}

void NNmodel::setName(const string &inname)
{
    if (final) {
        gennError("Trying to set the name of a finalized model.");
    }
    name= inname;
}

bool NNmodel::zeroCopyInUse() const
{
    // If any neuron groups use zero copy return true
    if(any_of(begin(m_LocalNeuronGroups), end(m_LocalNeuronGroups),
        [](const NeuronGroupValueType &n){ return n.second.isZeroCopyEnabled(); }))
    {
        return true;
    }

    // If any synapse groups use zero copy return true
    if(any_of(begin(m_LocalSynapseGroups), end(m_LocalSynapseGroups),
        [](const SynapseGroupValueType &s){ return s.second.isZeroCopyEnabled(); }))
    {
        return true;
    }

    return false;
}

unsigned int NNmodel::getNumPreSynapseResetRequiredGroups() const
{
    return std::count_if(getLocalSynapseGroups().cbegin(), getLocalSynapseGroups().cend(),
                         [](const SynapseGroupValueType &s){ return s.second.isDendriticDelayRequired(); });
}


bool NNmodel::isHostRNGRequired() const
{
    // Cache whether the model can run on the CPU
    const bool cpu = canRunOnCPU();

    // If model can run on the CPU and any neuron groups require simulation RNGs
    // or if any require host RNG for initialisation, return true
    if(any_of(begin(m_LocalNeuronGroups), end(m_LocalNeuronGroups),
        [cpu](const NeuronGroupValueType &n)
        {
            return ((cpu && n.second.isSimRNGRequired()) || n.second.isInitRNGRequired(VarInit::HOST));
        }))
    {
        return true;
    }

    // If any synapse groups require a host RNG return true
    if(any_of(begin(m_LocalSynapseGroups), end(m_LocalSynapseGroups),
        [](const SynapseGroupValueType &s)
        {
            return s.second.isWUInitRNGRequired(VarInit::HOST);
        }))
    {
        return true;
    }

    return false;
}

bool NNmodel::isDeviceRNGRequired() const
{
    // If any neuron groupsrequire device RNG for initialisation, return true
    if(any_of(begin(m_LocalNeuronGroups), end(m_LocalNeuronGroups),
        [](const NeuronGroupValueType &n)
        {
            return n.second.isInitRNGRequired(VarInit::DEVICE);
        }))
    {
        return true;
    }

    // If any synapse groups require a device RNG for initialisation, return true
    if(any_of(begin(m_LocalSynapseGroups), end(m_LocalSynapseGroups),
        [](const SynapseGroupValueType &s)
        {
            return s.second.isWUInitRNGRequired(VarInit::DEVICE);
        }))
    {
        return true;
    }

    return false;
}

bool NNmodel::canRunOnCPU() const
{
#ifndef CPU_ONLY
    // If any of the neuron groups can't run on the CPU, return false
    if(any_of(begin(m_LocalNeuronGroups), end(m_LocalNeuronGroups),
        [](const NeuronGroupValueType &n)
        {
            return !n.second.canRunOnCPU();
        }))
    {
        return false;
    }

    // If any of the synapse groups can't run on the CPU, return false
    if(any_of(begin(m_LocalSynapseGroups), end(m_LocalSynapseGroups),
        [](const SynapseGroupValueType &s)
        {
            return !s.second.canRunOnCPU();
        }))
    {
        return false;
    }
#endif
    return true;
}

std::string NNmodel::getGeneratedCodePath(const std::string &path, const std::string &filename) const{
#ifdef MPI_ENABLE
    int localHostID = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &localHostID);
    return path + "/" + getName() + "_" + std::to_string(localHostID) + "_CODE/" + filename;
#else
    return path + "/" + getName() + "_CODE/" + filename;
#endif
    }

bool NNmodel::isDeviceInitRequired(int localHostID) const
{
    // If device RNG is required, device init is required to initialise it
    if(isDeviceRNGRequired()) {
        return true;
    }

    // If any local neuron groups require device initialisation, return true
    if(std::any_of(std::begin(m_LocalNeuronGroups), std::end(m_LocalNeuronGroups),
        [](const NNmodel::NeuronGroupValueType &n){ return n.second.isDeviceInitRequired(); }))
    {
        return true;
    }

    // If any remote neuron groups with local outputs require their spike variables to be initialised on device
    if(std::any_of(std::begin(m_RemoteNeuronGroups), std::end(m_RemoteNeuronGroups),
        [localHostID](const NNmodel::NeuronGroupValueType &n)
        {
            return (n.second.hasOutputToHost(localHostID) && (n.second.getSpikeVarMode() & VarInit::DEVICE));
        }))
    {
        return true;
    }


    // If any local synapse groups require device initialisation, return true
    if(std::any_of(std::begin(m_LocalSynapseGroups), std::end(m_LocalSynapseGroups),
        [](const NNmodel::SynapseGroupValueType &s){ return s.second.isDeviceInitRequired(); }))
    {
        return true;
    }

    return false;
}

bool NNmodel::isDeviceSparseInitRequired() const
{
    // If automatic initialisation of sparse variables isn't enabled, return false
    if(!GENN_PREFERENCES::autoInitSparseVars) {
        return false;
    }

    // Return true if any of the synapse groups require device sparse initialisation
    return std::any_of(std::begin(m_LocalSynapseGroups), std::end(m_LocalSynapseGroups),
        [](const NNmodel::SynapseGroupValueType &s) { return s.second.isDeviceSparseInitRequired(); });
}

//--------------------------------------------------------------------------
/*! \brief This function is for setting which host and which device a neuron group will be simulated on
 */
//--------------------------------------------------------------------------

void NNmodel::setNeuronClusterIndex(const string &, int, int)
{
    gennError("This function has been deprecated since GeNN 3.1.0. Specify neuron cluster index in call to addNeuronPopulation instead.");
}

unsigned int NNmodel::getNeuronGridSize() const
{
    if(m_LocalNeuronGroups.empty()) {
        return 0;
    }
    else {
        return m_LocalNeuronGroups.rbegin()->second.getPaddedIDRange().second;
    }

}

unsigned int NNmodel::getNumLocalNeurons() const
{
    // Return sum of local neuron group sizes
    return std::accumulate(m_LocalNeuronGroups.cbegin(), m_LocalNeuronGroups.cend(), 0,
                           [](unsigned int total, const NeuronGroupValueType &n)
                           {
                               return total + n.second.getNumNeurons();
                           });
}

unsigned int NNmodel::getNumRemoteNeurons() const
{
    // Return sum of local remote neuron group sizes
    return std::accumulate(m_RemoteNeuronGroups.cbegin(), m_RemoteNeuronGroups.cend(), 0,
                           [](unsigned int total, const NeuronGroupValueType &n)
                           {
                               return total + n.second.getNumNeurons();
                           });
}

const NeuronGroup *NNmodel::findNeuronGroup(const std::string &name) const
{
    // If a matching local neuron group is found, return it
    auto localNeuronGroup = m_LocalNeuronGroups.find(name);
    if(localNeuronGroup != m_LocalNeuronGroups.cend()) {
        return &localNeuronGroup->second;
    }

    // Otherwise, if a matching remote neuron group is found, return it
    auto remoteNeuronGroup = m_RemoteNeuronGroups.find(name);
    if(remoteNeuronGroup != m_RemoteNeuronGroups.cend()) {
        return &remoteNeuronGroup->second;

    }
    // Otherwise, error
    else {
        gennError("neuron group " + name + " not found, aborting ...");
        return NULL;
    }
}

NeuronGroup *NNmodel::findNeuronGroup(const std::string &name)
{
    // If a matching local neuron group is found, return it
    auto localNeuronGroup = m_LocalNeuronGroups.find(name);
    if(localNeuronGroup != m_LocalNeuronGroups.cend()) {
        return &localNeuronGroup->second;
    }

    // Otherwise, if a matching remote neuron group is found, return it
    auto remoteNeuronGroup = m_RemoteNeuronGroups.find(name);
    if(remoteNeuronGroup != m_RemoteNeuronGroups.cend()) {
        return &remoteNeuronGroup->second;
    }
    // Otherwise, error
    else {
        gennError("neuron group " + name + " not found, aborting ...");
        return NULL;
    }
}

//--------------------------------------------------------------------------
/*! \overload

  This function adds a neuron population to a neuronal network models, assigning the name, the number of neurons in the group, the neuron type, parameters and initial values, the latter two defined as double *
 */
//--------------------------------------------------------------------------

NeuronGroup *NNmodel::addNeuronPopulation(
  const string &name, /**<  The name of the neuron population*/
  unsigned int nNo, /**<  Number of neurons in the population */
  unsigned int type, /**<  Type of the neurons, refers to either a standard type or user-defined type*/
  const double *p, /**< Parameters of this neuron type */
  const double *ini, /**< Initial values for variables of this neuron type */
  int hostID, /**< host ID for neuron group*/
  int deviceID /*device ID for neuron group*/)
{
  vector<double> vp;
  vector<double> vini;
  for (size_t i= 0; i < nModels[type].pNames.size(); i++) {
    vp.push_back(p[i]);
  }
  for (size_t i= 0; i < nModels[type].varNames.size(); i++) {
    vini.push_back(ini[i]);
  }
  return addNeuronPopulation(name, nNo, type, vp, vini, hostID, deviceID);
}
  

//--------------------------------------------------------------------------
/*! \brief This function adds a neuron population to a neuronal network models, assigning the name, the number of neurons in the group, the neuron type, parameters and initial values. The latter two defined as STL vectors of double.
 */
//--------------------------------------------------------------------------

NeuronGroup *NNmodel::addNeuronPopulation(
  const string &name, /**<  The name of the neuron population*/
  unsigned int nNo, /**<  Number of neurons in the population */
  unsigned int type, /**<  Type of the neurons, refers to either a standard type or user-defined type*/
  const vector<double> &p, /**< Parameters of this neuron type */
  const vector<double> &ini, /**< Initial values for variables of this neuron type */
  int hostID,
  int deviceID)
{
    if (!GeNNReady) {
        gennError("You need to call initGeNN first.");
    }
    if (final) {
        gennError("Trying to add a neuron population to a finalized model.");
    }
    if (p.size() != nModels[type].pNames.size()) {
        gennError("The number of parameter values for neuron group " + name + " does not match that of their neuron type, " + to_string(p.size()) + " != " + to_string(nModels[type].pNames.size()));
    }
    if (ini.size() != nModels[type].varNames.size()) {
        gennError("The number of variable initial values for neuron group " + name + " does not match that of their neuron type, " + to_string(ini.size()) + " != " + to_string(nModels[type].varNames.size()));
    }

    // Create variable initialisers from old-style values
    std::vector<NewModels::VarInit> varInitialisers;
    createVarInitialiserFromLegacyVars(ini, varInitialisers);

#ifdef MPI_ENABLE
    // Determine the host ID
    int mpiHostID = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

    // Pick map to add group to appropriately
    auto &groupMap = (hostID == mpiHostID) ? m_LocalNeuronGroups : m_RemoteNeuronGroups;
#else
    // If MPI is disabled always add to local neuron groups and zero host id
    auto &groupMap = m_LocalNeuronGroups;
    hostID = 0;
#endif

    // Add neuron group to map
    auto result = groupMap.emplace(std::piecewise_construct,
        std::forward_as_tuple(name),
        std::forward_as_tuple(name, nNo, new NeuronModels::LegacyWrapper(type), p, varInitialisers, hostID, deviceID));

    if(!result.second)
    {
        gennError("Cannot add a neuron population with duplicate name:" + name);
        return NULL;
    }
    else
    {
        return &result.first->second;
    }
}


//--------------------------------------------------------------------------
/*! \brief This function defines the type of the explicit input to the neuron model. Current options are common constant input to all neurons, input  from a file and input defines as a rule.
*/ 
//--------------------------------------------------------------------------
void NNmodel::activateDirectInput(
  const string &, /**< Name of the neuron population */
  unsigned int /**< Type of input: 1 if common input, 2 if custom input from file, 3 if custom input as a rule*/)
{
    gennError("This function has been deprecated since GeNN 2.2. Use neuron variables, extraGlobalNeuronKernelParameters, or parameters instead.");
}

unsigned int NNmodel::getSynapseKernelGridSize() const
{
    if(m_LocalSynapseGroups.empty()) {
        return 0;
    }
    else {
        return m_LocalSynapseGroups.rbegin()->second.getPaddedKernelIDRange().second;
    }

}

unsigned int NNmodel::getSynapsePostLearnGridSize() const
{
    if(m_SynapsePostLearnGroups.empty()) {
        return 0;
    }
    else {
        return m_SynapsePostLearnGroups.rbegin()->second.second;
    }
}

unsigned int NNmodel::getSynapseDynamicsGridSize() const
{
    if(m_SynapseDynamicsGroups.empty()) {
        return 0;
    }
    else {
        return m_SynapseDynamicsGroups.rbegin()->second.second;
    }
}

const SynapseGroup *NNmodel::findSynapseGroup(const std::string &name) const
{
    // If a matching local synapse group is found, return it
    auto localSynapseGroup = m_LocalSynapseGroups.find(name);
    if(localSynapseGroup != m_LocalSynapseGroups.cend()) {
        return &localSynapseGroup->second;
    }

    // Otherwise, if a matching remote synapse group is found, return it
    auto remoteSynapseGroup = m_RemoteSynapseGroups.find(name);
    if(remoteSynapseGroup != m_RemoteSynapseGroups.cend()) {
        return &remoteSynapseGroup->second;

    }
    // Otherwise, error
    else {
        gennError("synapse group " + name + " not found, aborting ...");
        return NULL;
    }
}

SynapseGroup *NNmodel::findSynapseGroup(const std::string &name)
{
    // If a matching local synapse group is found, return it
    auto localSynapseGroup = m_LocalSynapseGroups.find(name);
    if(localSynapseGroup != m_LocalSynapseGroups.cend()) {
        return &localSynapseGroup->second;
    }

    // Otherwise, if a matching remote synapse group is found, return it
    auto remoteSynapseGroup = m_RemoteSynapseGroups.find(name);
    if(remoteSynapseGroup != m_RemoteSynapseGroups.cend()) {
        return &remoteSynapseGroup->second;

    }
    // Otherwise, error
    else {
        gennError("synapse group " + name + " not found, aborting ...");
        return NULL;
    }
}

bool NNmodel::isSynapseGroupDynamicsRequired(const std::string &name) const
{
    return (m_SynapseDynamicsGroups.find(name) != end(m_SynapseDynamicsGroups));
}

bool NNmodel::isSynapseGroupPostLearningRequired(const std::string &name) const
{
    return (m_SynapsePostLearnGroups.find(name) != end(m_SynapsePostLearnGroups));
}

//--------------------------------------------------------------------------
/*! \overload

  This deprecated function is provided for compatibility with the previous release of GeNN.
 * Default values are provide for new parameters, it is strongly recommended these be selected explicity via the new version othe function
 */
//--------------------------------------------------------------------------

SynapseGroup *NNmodel::addSynapsePopulation(
  const string &, /**<  The name of the synapse population*/
  unsigned int, /**< The type of synapse to be added (i.e. learning mode) */
  SynapseConnType, /**< The type of synaptic connectivity*/
  SynapseGType, /**< The way how the synaptic conductivity g will be defined*/
  const string &, /**< Name of the (existing!) pre-synaptic neuron population*/
  const string &, /**< Name of the (existing!) post-synaptic neuron population*/
  const double*) /**< A C-type array of doubles that contains synapse parameter values (common to all synapses of the population) which will be used for the defined synapses.*/
{
  gennError("This version of addSynapsePopulation() has been deprecated since GeNN 2.2. Please use the newer addSynapsePopulation functions instead.");
  return NULL;
}


//--------------------------------------------------------------------------
/*! \brief Overloaded old version (deprecated)
*/
//--------------------------------------------------------------------------

SynapseGroup *NNmodel::addSynapsePopulation(
  const string &name, /**<  The name of the synapse population*/
  unsigned int syntype, /**< The type of synapse to be added (i.e. learning mode) */
  SynapseConnType conntype, /**< The type of synaptic connectivity*/
  SynapseGType gtype, /**< The way how the synaptic conductivity g will be defined*/
  unsigned int delaySteps, /**< Number of delay slots*/
  unsigned int postsyn, /**< Postsynaptic integration method*/
  const string &src, /**< Name of the (existing!) pre-synaptic neuron population*/
  const string &trg, /**< Name of the (existing!) post-synaptic neuron population*/
  const double *p, /**< A C-type array of doubles that contains synapse parameter values (common to all synapses of the population) which will be used for the defined synapses.*/
  const double* PSVini, /**< A C-type array of doubles that contains the initial values for postsynaptic mechanism variables (common to all synapses of the population) which will be used for the defined synapses.*/
  const double *ps /**< A C-type array of doubles that contains postsynaptic mechanism parameter values (common to all synapses of the population) which will be used for the defined synapses.*/)
{
    cerr << "!!!!!!GeNN WARNING: This function has been deprecated since GeNN 2.2, and will be removed in a future release. You use the overloaded method which passes a null pointer for the initial values of weight update variables. If you use a method that uses synapse variables, please add a pointer to this vector in the function call, like:\n          addSynapsePopulation(name, syntype, conntype, gtype, NO_DELAY, EXPDECAY, src, target, double * SYNVARINI, params, postSynV,postExpSynapsePopn);" << endl;
    const double *iniv = NULL;
    return addSynapsePopulation(name, syntype, conntype, gtype, delaySteps, postsyn, src, trg, iniv, p, PSVini, ps);
}


//--------------------------------------------------------------------------
/*! \brief This function adds a synapse population to a neuronal network model, assigning the name, the synapse type, the connectivity type, the type of conductance specification, the source and destination neuron populations, and the synaptic parameters.
 */
//--------------------------------------------------------------------------

SynapseGroup *NNmodel::addSynapsePopulation(
  const string &name, /**<  The name of the synapse population*/
  unsigned int syntype, /**< The type of synapse to be added (i.e. learning mode) */
  SynapseConnType conntype, /**< The type of synaptic connectivity*/
  SynapseGType gtype, /**< The way how the synaptic conductivity g will be defined*/
  unsigned int delaySteps, /**< Number of delay slots*/
  unsigned int postsyn, /**< Postsynaptic integration method*/
  const string &src, /**< Name of the (existing!) pre-synaptic neuron population*/
  const string &trg, /**< Name of the (existing!) post-synaptic neuron population*/
  const double* synini, /**< A C-type array of doubles that contains the initial values for synapse variables (common to all synapses of the population) which will be used for the defined synapses.*/
  const double *p, /**< A C-type array of doubles that contains synapse parameter values (common to all synapses of the population) which will be used for the defined synapses.*/
  const double* PSVini, /**< A C-type array of doubles that contains the initial values for postsynaptic mechanism variables (common to all synapses of the population) which will be used for the defined synapses.*/
  const double *ps /**< A C-type array of doubles that contains postsynaptic mechanism parameter values (common to all synapses of the population) which will be used for the defined synapses.*/)
{
  vector<double> vsynini;
  for (size_t j= 0; j < weightUpdateModels[syntype].varNames.size(); j++) {
    vsynini.push_back(synini[j]);
  }
  vector<double> vp;
  for (size_t j= 0; j < weightUpdateModels[syntype].pNames.size(); j++) {
    vp.push_back(p[j]);
  }
  vector<double> vpsini;
  for (size_t j= 0; j < postSynModels[postsyn].varNames.size(); j++) {
    vpsini.push_back(PSVini[j]);
  }
  vector<double> vps;
  for (size_t j= 0; j <  postSynModels[postsyn].pNames.size(); j++) {
    vps.push_back(ps[j]);
  }
  return addSynapsePopulation(name, syntype, conntype, gtype, delaySteps, postsyn, src, trg, vsynini, vp, vpsini, vps);
}


//--------------------------------------------------------------------------
/*! \brief This function adds a synapse population to a neuronal network model, assigning the name, the synapse type, the connectivity type, the type of conductance specification, the source and destination neuron populations, and the synaptic parameters.
 */
//--------------------------------------------------------------------------

SynapseGroup *NNmodel::addSynapsePopulation(
  const string &name, /**<  The name of the synapse population*/
  unsigned int syntype, /**< The type of synapse to be added (i.e. learning mode) */
  SynapseConnType conntype, /**< The type of synaptic connectivity*/
  SynapseGType gtype, /**< The way how the synaptic conductivity g will be defined*/
  unsigned int delaySteps, /**< Number of delay slots*/
  unsigned int postsyn, /**< Postsynaptic integration method*/
  const string &src, /**< Name of the (existing!) pre-synaptic neuron population*/
  const string &trg, /**< Name of the (existing!) post-synaptic neuron population*/
  const vector<double> &synini, /**< A C-type array of doubles that contains the initial values for synapse variables (common to all synapses of the population) which will be used for the defined synapses.*/
  const vector<double> &p, /**< A C-type array of doubles that contains synapse parameter values (common to all synapses of the population) which will be used for the defined synapses.*/
  const vector<double> &PSVini, /**< A C-type array of doubles that contains the initial values for postsynaptic mechanism variables (common to all synapses of the population) which will be used for the defined synapses.*/
  const vector<double> &ps /**< A C-type array of doubles that contains postsynaptic mechanism parameter values (common to all synapses of the population) which will be used for the defined synapses.*/)
{
    if (!GeNNReady) {
        gennError("You need to call initGeNN first.");
    }
    if (final) {
        gennError("Trying to add a synapse population to a finalized model.");
    }
    if (p.size() != weightUpdateModels[syntype].pNames.size()) {
        gennError("The number of presynaptic parameter values for synapse group " + name + " does not match that of their synapse type, " + to_string(p.size()) + " != " + to_string(weightUpdateModels[syntype].pNames.size()));
    }
    if (synini.size() != weightUpdateModels[syntype].varNames.size()) {
        gennError("The number of presynaptic variable initial values for synapse group " + name + " does not match that of their synapse type, " + to_string(synini.size()) + " != " + to_string(weightUpdateModels[syntype].varNames.size()));
    }
    if (ps.size() != postSynModels[postsyn].pNames.size()) {
        gennError("The number of presynaptic parameter values for synapse group " + name + " does not match that of their synapse type, " + to_string(ps.size()) + " != " + to_string(postSynModels[postsyn].pNames.size()));
    }
    if (PSVini.size() != postSynModels[postsyn].varNames.size()) {
        gennError("The number of presynaptic variable initial values for synapse group " + name + " does not match that of their synapse type, " + to_string(PSVini.size()) + " != " + to_string(postSynModels[postsyn].varNames.size()));
    }

    SynapseMatrixType mtype;
    if(conntype == SPARSE && gtype == GLOBALG)
    {
        mtype = SynapseMatrixType::SPARSE_GLOBALG;
    }
    else if(conntype == SPARSE && gtype == INDIVIDUALG)
    {
        mtype = SynapseMatrixType::SPARSE_INDIVIDUALG;
    }
    else if((conntype == DENSE || conntype == ALLTOALL) && gtype == INDIVIDUALG)
    {
        mtype = SynapseMatrixType::DENSE_INDIVIDUALG;
    }
    else if((conntype == DENSE || conntype == ALLTOALL) && gtype == GLOBALG)
    {
        mtype = SynapseMatrixType::DENSE_GLOBALG;
    }
    else if(gtype == INDIVIDUALID)
    {
        mtype = SynapseMatrixType::BITMASK_GLOBALG;
    }
    else
    {
        gennError("Combination of connection type " + to_string(conntype) + " and weight type " + to_string(gtype) + " not supported");
        return NULL;
    }

    // Get source and target neuron groups
    auto srcNeuronGrp = findNeuronGroup(src);
    auto trgNeuronGrp = findNeuronGroup(trg);

    // Create variable initialisers from old-style values
    std::vector<NewModels::VarInit> psVarInitialisers;
    createVarInitialiserFromLegacyVars(PSVini, psVarInitialisers);

    std::vector<NewModels::VarInit> wuVarInitialisers;
    createVarInitialiserFromLegacyVars(synini, wuVarInitialisers);

#ifdef MPI_ENABLE
    // Get host ID of target neuron group
    const int hostID = trgNeuronGrp->getClusterHostID();

    // Determine the host ID
    int mpiHostID = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

    // Pick map to add group to appropriately
    auto &groupMap = (hostID == mpiHostID) ? m_LocalSynapseGroups : m_RemoteSynapseGroups;
#else
    // If MPI is disabled always add to local synapse groups
    auto &groupMap = m_LocalSynapseGroups;
#endif

    // Add synapse group to map
    auto result = groupMap.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(name),
        std::forward_as_tuple(name, mtype, delaySteps,
                              new WeightUpdateModels::LegacyWrapper(syntype), p, wuVarInitialisers,
                              new PostsynapticModels::LegacyWrapper(postsyn), ps, psVarInitialisers,
                              srcNeuronGrp, trgNeuronGrp,
                              uninitialisedConnectivity()));

    if(!result.second)
    {
        gennError("Cannot add a synapse population with duplicate name:" + name);
        return NULL;
    }
    else
    {
        return &result.first->second;
    }
}

//--------------------------------------------------------------------------
/*! \brief This function attempts to find an existing current source */
//--------------------------------------------------------------------------

const CurrentSource *NNmodel::findCurrentSource(const std::string &name) const
{
    // If a matching local current source is found, return it
    auto localCurrentSource = m_LocalCurrentSources.find(name);
    if(localCurrentSource != m_LocalCurrentSources.cend()) {
        return &localCurrentSource->second;
    }

    // Otherwise, if a matching remote current source is found, return it
    auto remoteCurrentSource = m_RemoteCurrentSources.find(name);
    if(remoteCurrentSource != m_RemoteCurrentSources.cend()) {
        return &remoteCurrentSource->second;

    }
    // Otherwise, error
    else {
        gennError("current source " + name + " not found, aborting ...");
        return NULL;
    }
}

CurrentSource *NNmodel::findCurrentSource(const std::string &name)
{
    // If a matching local current source is found, return it
    auto localCurrentSource = m_LocalCurrentSources.find(name);
    if(localCurrentSource != m_LocalCurrentSources.cend()) {
        return &localCurrentSource->second;
    }

    // Otherwise, if a matching remote current source is found, return it
    auto remoteCurrentSource = m_RemoteCurrentSources.find(name);
    if(remoteCurrentSource != m_RemoteCurrentSources.cend()) {
        return &remoteCurrentSource->second;

    }
    // Otherwise, error
    else {
        gennError("current source " + name + " not found, aborting ...");
        return NULL;
    }
}


//--------------------------------------------------------------------------
/*! \brief This function defines the maximum number of connections for a neuron in the population
*/ 
//--------------------------------------------------------------------------

void NNmodel::setMaxConn(const string &sname, /**<  */
                         unsigned int maxConnP /**<  */)
{
    if (final) {
        gennError("Trying to set MaxConn in a finalized model.");
    }
    findSynapseGroup(sname)->setMaxConnections(maxConnP);

}


//--------------------------------------------------------------------------
/*! \brief This function defines the execution order of the synapses in the kernels
  (0 : execute for every postsynaptic neuron 1: execute for every presynaptic neuron)
 */ 
//--------------------------------------------------------------------------

void NNmodel::setSpanTypeToPre(const string &sname /**< name of the synapse group to which to apply the pre-synaptic span type */)
{
    if (final) {
        gennError("Trying to set spanType in a finalized model.");
    }
    findSynapseGroup(sname)->setSpanType(SynapseGroup::SpanType::PRESYNAPTIC);

}


//--------------------------------------------------------------------------
/*! \brief This functions sets the global value of the maximal synaptic conductance for a synapse population that was idfentified as conductance specifcation method "GLOBALG" 
 */
//--------------------------------------------------------------------------

void NNmodel::setSynapseG(const string &, /**<  */
                          double /**<  */)
{
    gennError("NOTE: This function has been deprecated as of GeNN 2.2. Please provide the correct initial values in \"addSynapsePopulation\" for all your variables and they will be the constant values in the GLOBALG mode.");
}


//--------------------------------------------------------------------------
/*! \brief This function sets a global input value to the specified neuron group.
 */
//--------------------------------------------------------------------------

void NNmodel::setConstInp(const string &, /**<  */
                          double /**<  */)
{
    gennError("This function has been deprecated as of GeNN 2.2. Use parameters in the neuron model instead.");
}


//--------------------------------------------------------------------------
/*! \brief This function sets the integration time step DT of the model
 */
//--------------------------------------------------------------------------

void NNmodel::setDT(double newDT /**<  */)
{
    if (final) {
        gennError("Trying to set DT in a finalized model.");
    }
    dt = newDT;
}


//--------------------------------------------------------------------------
/*! \brief This function sets the numerical precision of floating type variables. By default, it is GENN_GENN_FLOAT.
 */
//--------------------------------------------------------------------------

void NNmodel::setPrecision(FloatType floattype /**<  */)
{
    if (final) {
        gennError("Trying to set the precision of a finalized model.");
    }
    switch (floattype) {
    case GENN_FLOAT:
        ftype = "float";
        break;
    case GENN_DOUBLE:
        ftype = "double"; // not supported by compute capability < 1.3
        break;
    case GENN_LONG_DOUBLE:
        ftype = "long double"; // not supported by CUDA at the moment.
        break;
    default:
        gennError("Unrecognised floating-point type.");
    }
}


//--------------------------------------------------------------------------
/*! \brief This function sets a flag to determine whether timers and timing commands are to be included in generated code.
 */
//--------------------------------------------------------------------------

void NNmodel::setTiming(bool theTiming /**<  */)
{
    if (final) {
        gennError("Trying to set timing flag in a finalized model.");
    }
    timing= theTiming;
}


//--------------------------------------------------------------------------
/*! \brief This function sets the random seed. If the passed argument is > 0, automatic seeding is disabled. If the argument is 0, the underlying seed is obtained from the time() function.
 */
//--------------------------------------------------------------------------

void NNmodel::setSeed(unsigned int inseed /*!< the new seed  */)
{
    if (final) {
        gennError("Trying to set the random seed in a finalized model.");
    }
    seed= inseed;
}

//--------------------------------------------------------------------------
/*! \brief Sets the underlying type for random number generation (default: uint64_t)
 */
//--------------------------------------------------------------------------
void NNmodel::setRNType(const std::string &type)
{
    if (final) {
        gennError("Trying to set the random number type in a finalized model.");
    }
    RNtype= type;
}

#ifndef CPU_ONLY
//--------------------------------------------------------------------------
/*! \brief This function defines the way how the GPU is chosen. If "AUTODEVICE" (-1) is given as the argument, GeNN will use internal heuristics to choose the device. Otherwise the argument is the device number and the indicated device will be used.
*/ 
//--------------------------------------------------------------------------

void NNmodel::setGPUDevice(int device)
{
  int deviceCount;
  CHECK_CUDA_ERRORS(cudaGetDeviceCount(&deviceCount));
  assert(device >= -1);
  assert(device < deviceCount);
  if (device == -1) GENN_PREFERENCES::autoChooseDevice= 1;
  else {
      GENN_PREFERENCES::autoChooseDevice= 0;
      GENN_PREFERENCES::defaultDevice= device;
  }
}
#endif


string NNmodel::scalarExpr(const double val) const
{
    string tmp;
    float fval= (float) val;
    if (ftype == "float") {
        tmp= to_string(fval) + "f";
    }
    if (ftype == "double") {
        tmp= to_string(val);
    }
    return tmp;
}

//--------------------------------------------------------------------------
/*! \brief Accumulate the sums and block-size-padded sums of all simulation groups.

  This method saves the neuron numbers of the populations rounded to the next multiple of the block size as well as the sums s(i) = sum_{1...i} n_i of the rounded population sizes. These are later used to determine the branching structure for the generated neuron kernel code. 
*/
//--------------------------------------------------------------------------

void NNmodel::setPopulationSums()
{
    // NEURON GROUPS
    unsigned int neuronIDStart = 0;
    unsigned int paddedNeuronIDStart = 0;
    for(auto &n : m_LocalNeuronGroups) {
        n.second.calcSizes(neuronBlkSz, neuronIDStart, paddedNeuronIDStart);
    }

    // SYNAPSE groups
    unsigned int paddedSynapseKernelIDStart = 0;
    unsigned int paddedSynapseDynamicsIDStart = 0;
    unsigned int paddedSynapsePostLearnIDStart = 0;
    for(auto &s : m_LocalSynapseGroups) {
        // Calculate synapse kernel sizes
        s.second.calcKernelSizes(synapseBlkSz, paddedSynapseKernelIDStart);

        if (!s.second.getWUModel()->getLearnPostCode().empty()) {
            const unsigned int startID = paddedSynapsePostLearnIDStart;
            paddedSynapsePostLearnIDStart += s.second.getPaddedPostLearnKernelSize(learnBlkSz);

            // Add this synapse group to map of synapse groups with postsynaptic learning
            // or update the existing entry with the new block sizes
            m_SynapsePostLearnGroups[s.first] = std::make_pair(startID, paddedSynapsePostLearnIDStart);
        }

         if (!s.second.getWUModel()->getSynapseDynamicsCode().empty()) {
            const unsigned int startID = paddedSynapseDynamicsIDStart;
            paddedSynapseDynamicsIDStart += s.second.getPaddedDynKernelSize(synDynBlkSz);

            // Add this synapse group to map of synapse groups with dynamics
            // or update the existing entry with the new block sizes
            m_SynapseDynamicsGroups[s.first] = std::make_pair(startID, paddedSynapseDynamicsIDStart);
         }
    }
}

void NNmodel::finalize()
{
    //initializing learning parameters to start
    if (final) {
        gennError("Your model has already been finalized");
    }
    final = true;

    // Loop through neuron populations and their outgoing synapse populations
    for(auto &n : m_LocalNeuronGroups) {
        for(auto *sg : n.second.getOutSyn()) {
            const auto *wu = sg->getWUModel();

            if (!wu->getEventCode().empty()) {
                sg->setSpikeEventRequired(true);
                n.second.setSpikeEventRequired(true);
                assert(!wu->getEventThresholdConditionCode().empty());

                 // Create iteration context to iterate over derived and extra global parameters
                ExtraGlobalParamNameIterCtx wuExtraGlobalParams(wu->getExtraGlobalParams());
                DerivedParamNameIterCtx wuDerivedParams(wu->getDerivedParams());

                // do an early replacement of parameters, derived parameters and extraglobalsynapse parameters
                string eCode = wu->getEventThresholdConditionCode();
                value_substitutions(eCode, wu->getParamNames(), sg->getWUParams());
                value_substitutions(eCode, wuDerivedParams.nameBegin, wuDerivedParams.nameEnd, sg->getWUDerivedParams());
                name_substitutions(eCode, "", wuExtraGlobalParams.nameBegin, wuExtraGlobalParams.nameEnd, sg->getName());

                // Add code and name of
                string supportCodeNamespaceName = wu->getSimSupportCode().empty() ?
                    "" : sg->getName() + "_weightupdate_simCode";

                // Add code and name of support code namespace to set
                n.second.addSpkEventCondition(eCode, supportCodeNamespaceName);

                // analyze which neuron variables need queues
                n.second.updateVarQueues(wu->getEventCode());
            }
        }
        if (n.second.getSpikeEventCondition().size() > 1) {
            for(auto *sg : n.second.getOutSyn()) {
                if (!sg->getWUModel()->getEventCode().empty()) {
                    sg->setEventThresholdReTestRequired(true);
                }
            }
        }
    }

    // NEURON GROUPS
    for(auto &n : m_LocalNeuronGroups) {
        // Initialize derived parameters
        n.second.initDerivedParams(dt);

        // Make extra global parameter lists
        n.second.addExtraGlobalParams(neuronKernelParameters);
    }

    // SYNAPSE groups
    for(auto &s : m_LocalSynapseGroups) {
        const auto *wu = s.second.getWUModel();

        // Initialize derived parameters
        s.second.initDerivedParams(dt);

        if (!wu->getSimCode().empty()) {
            s.second.setTrueSpikeRequired(true);
            s.second.getSrcNeuronGroup()->setTrueSpikeRequired(true);

            // analyze which neuron variables need queues
            s.second.getSrcNeuronGroup()->updateVarQueues(wu->getSimCode());
        }

        if (!wu->getLearnPostCode().empty()) {
            s.second.getSrcNeuronGroup()->updateVarQueues(wu->getLearnPostCode());
        }

        if (!wu->getSynapseDynamicsCode().empty()) {
            s.second.getSrcNeuronGroup()->updateVarQueues(wu->getSynapseDynamicsCode());
        }

        // Make extra global parameter lists
        s.second.addExtraGlobalConnectivityInitialiserParams(m_InitKernelParameters);
        s.second.addExtraGlobalNeuronParams(neuronKernelParameters);
        s.second.addExtraGlobalSynapseParams(synapseKernelParameters);
        s.second.addExtraGlobalPostLearnParams(simLearnPostKernelParameters);
        s.second.addExtraGlobalSynapseDynamicsParams(synapseDynamicsKernelParameters);

        // If this synapse group has either ragged or bitmask connectivity which is initialised
        // using a connectivity snippet AND has individual synaptic variables
        if(((s.second.getMatrixType() & SynapseMatrixConnectivity::RAGGED)
            || (s.second.getMatrixType() & SynapseMatrixConnectivity::BITMASK))
            && !s.second.getConnectivityInitialiser().getSnippet()->getRowBuildCode().empty()
            && s.second.getMatrixType() & SynapseMatrixWeight::INDIVIDUAL)
        {
            // Loop through variables and check that they are initialised in the same place as the sparse connectivity
            auto wuVars = s.second.getWUModel()->getVars();
            for (size_t k= 0, l= wuVars.size(); k < l; k++) {
                if((s.second.getSparseConnectivityVarMode() & VarInit::HOST) != (s.second.getWUVarMode(k) & VarInit::HOST)) {
                    gennError("Weight update mode variables must be initialised in same place as sparse connectivity variable '" + wuVars[k].first + "' in population '" + s.first + "' is not");
                }
            }
        }
    }

    // CURRENT SOURCES
    for(auto &cs : m_LocalCurrentSources) {
        // Initialize derived parameters
        cs.second.initDerivedParams(dt);

        // Make extra global parameter lists
        cs.second.addExtraGlobalParams(currentSourceKernelParameters);
    }

    // Merge incoming postsynaptic models
    for(auto &n : m_LocalNeuronGroups) {
        if(!n.second.getInSyn().empty()) {
            n.second.mergeIncomingPSM();
        }
    }

    // CURRENT SOURCES
    for(auto &cs : m_LocalCurrentSources) {
        // Initialize derived parameters
        cs.second.initDerivedParams(dt);

        // Make extra global parameter lists
        cs.second.addExtraGlobalParams(currentSourceKernelParameters);
    }

    setPopulationSums();

#ifndef CPU_ONLY
    // figure out where to reset the spike counters
    if (m_LocalSynapseGroups.empty()) { // no synapses -> reset in neuron kernel
        resetKernel= GENN_FLAGS::calcNeurons;
    }
    else { // there are synapses
        if (!m_SynapsePostLearnGroups.empty()) {
            resetKernel= GENN_FLAGS::learnSynapsesPost;
        }
        else {
            resetKernel= GENN_FLAGS::calcSynapses;
        }
    }
#endif
}



#endif // MODELSPEC_CC
