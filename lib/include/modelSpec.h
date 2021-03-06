/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
              Falmer, Brighton BN1 9QJ, UK
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
   
   This file contains neuron model declarations.
  
--------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file modelSpec.h

\brief Header file that contains the class (struct) definition of neuronModel for 
defining a neuron model and the class definition of NNmodel for defining a neuronal network model. 
Part of the code generation and generated code sections.
*/
//--------------------------------------------------------------------------

#ifndef _MODELSPEC_H_
#define _MODELSPEC_H_ //!< macro for avoiding multiple inclusion during compilation

#include "neuronGroup.h"
#include "synapseGroup.h"
#include "currentSource.h"
#include "utils.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef MPI_ENABLE
#include <mpi.h>
#endif

using namespace std;


void initGeNN();
extern unsigned int GeNNReady;

// connectivity of the network (synapseConnType)
enum SynapseConnType
{
    ALLTOALL,
    DENSE,
    SPARSE,
};

// conductance type (synapseGType)
enum SynapseGType
{
    INDIVIDUALG,
    GLOBALG,
    INDIVIDUALID,
};

#define NO_DELAY 0 //!< Macro used to indicate no synapse delay for the group (only one queue slot will be generated)

#define NOLEARNING 0 //!< Macro attaching the label "NOLEARNING" to flag 0 
#define LEARNING 1 //!< Macro attaching the label "LEARNING" to flag 1 

#define EXITSYN 0 //!< Macro attaching the label "EXITSYN" to flag 0 (excitatory synapse)
#define INHIBSYN 1 //!< Macro attaching the label "INHIBSYN" to flag 1 (inhibitory synapse)

#define CPU 0 //!< Macro attaching the label "CPU" to flag 0
#define GPU 1 //!< Macro attaching the label "GPU" to flag 1

// Floating point precision to use for models
enum FloatType
{
    GENN_FLOAT,
    GENN_DOUBLE,
    GENN_LONG_DOUBLE,
};

#define AUTODEVICE -1  //!< Macro attaching the label AUTODEVICE to flag -1. Used by setGPUDevice

// Wrappers to save typing when declaring VarInitialisers structures
template<typename Snippet>
inline NewModels::VarInit initVar(const typename Snippet::ParamValues &params)
{
    return NewModels::VarInit(Snippet::getInstance(), params.getValues());
}

inline NewModels::VarInit uninitialisedVar()
{
    return NewModels::VarInit(InitVarSnippet::Uninitialised::getInstance(), {});
}

template<typename Snippet>
inline InitSparseConnectivitySnippet::Init initConnectivity(const typename Snippet::ParamValues &params)
{
    return InitSparseConnectivitySnippet::Init(Snippet::getInstance(), params.getValues());
}

inline InitSparseConnectivitySnippet::Init uninitialisedConnectivity()
{
    return InitSparseConnectivitySnippet::Init(InitSparseConnectivitySnippet::Uninitialised::getInstance(), {});
}

/*===============================================================
//! \brief class NNmodel for specifying a neuronal network model.
//
================================================================*/

class NNmodel
{
public:
    // Typedefines
    //=======================
    typedef map<string, NeuronGroup>::value_type NeuronGroupValueType;
    typedef map<string, SynapseGroup>::value_type SynapseGroupValueType;


    NNmodel();
    ~NNmodel();

    // PUBLIC MODEL FUNCTIONS
    //=======================
    void setName(const std::string&); //!< Method to set the neuronal network model name

    void setPrecision(FloatType); //!< Set numerical precision for floating point
    void setDT(double); //!< Set the integration step size of the model
    void setTiming(bool); //!< Set whether timers and timing commands are to be included
    void setSeed(unsigned int); //!< Set the random seed (disables automatic seeding if argument not 0).
    void setRNType(const std::string &type); //! Sets the underlying type for random number generation (default: uint64_t)

#ifndef CPU_ONLY
    void setGPUDevice(int); //!< Method to choose the GPU to be used for the model. If "AUTODEVICE' (-1), GeNN will choose the device based on a heuristic rule.
#endif
    //! Get the string literal that should be used to represent a value in the model's floating-point type
    string scalarExpr(const double) const;

    void setPopulationSums(); //!< Set the accumulated sums of lowest multiple of kernel block size >= group sizes for all simulated groups.
    void finalize(); //!< Declare that the model specification is finalised in modelDefinition().

    //! Are any variables in any populations in this model using zero-copy memory?
    bool zeroCopyInUse() const;

    //! Return number of synapse groups which require a presynaptic reset kernel to be run
    unsigned int getNumPreSynapseResetRequiredGroups() const;

    //! Is there reset logic to be run before the synapse kernel i.e. for dendritic delays
    bool isPreSynapseResetRequired() const{ return getNumPreSynapseResetRequiredGroups() > 0; }

    //! Do any populations or initialisation code in this model require a host RNG?
    bool isHostRNGRequired() const;

    //! Do any populations or initialisation code in this model require a device RNG?
    /*! **NOTE** some model code will use per-neuron RNGs instead */
    bool isDeviceRNGRequired() const;

    //! Can this model run on the CPU?
    /*! If we are running in CPU_ONLY mode this is always true,
        but some GPU functionality will prevent models being run on both CPU and GPU. */
    bool canRunOnCPU() const;

    //! Gets the name of the neuronal network model
    const std::string &getName() const{ return name; }

    //! Gets the floating point numerical precision
    const std::string &getPrecision() const{ return ftype; }

    //! Which kernel should contain the reset logic? Specified in terms of GENN_FLAGS
    unsigned int getResetKernel() const{ return resetKernel; }

    //! Gets the model integration step size
    double getDT() const { return dt; }

    //! Get the random seed
    unsigned int getSeed() const { return seed; }

    //! Gets the underlying type for random number generation (default: uint64_t)
    const std::string &getRNType() const{ return RNtype; }

    //! Is the model specification finalized
    bool isFinalized() const{ return final; }

    //! Are timers and timing commands enabled
    bool isTimingEnabled() const{ return timing; }

    //! Generate path for generated code
    std::string getGeneratedCodePath(const std::string &path, const std::string &filename) const;

    // PUBLIC INITIALISATION FUNCTIONS
    //================================
    const map<string, string> &getInitKernelParameters() const{ return m_InitKernelParameters; }

    //! Does this model require device initialisation kernel
    /*! **NOTE** this is for neuron groups and densely connected synapse groups only */
    bool isDeviceInitRequired(int localHostID) const;

    //! Does this model require a device sparse initialisation kernel
    /*! **NOTE** this is for sparsely connected synapse groups only */
    bool isDeviceSparseInitRequired() const;

    // PUBLIC NEURON FUNCTIONS
    //========================
    //! Get std::map containing local named NeuronGroup objects in model
    const map<string, NeuronGroup> &getLocalNeuronGroups() const{ return m_LocalNeuronGroups; }

    //! Get std::map containing remote named NeuronGroup objects in model
    const map<string, NeuronGroup> &getRemoteNeuronGroups() const{ return m_RemoteNeuronGroups; }

    //! Gets std::map containing names and types of each parameter that should be passed through to the neuron kernel
    const map<string, string> &getNeuronKernelParameters() const{ return neuronKernelParameters; }

    //! Gets the size of the neuron kernel thread grid
    /*! This is calculated by adding together the number of threads required by
        each neuron population, padded to be a multiple of GPU's thread block size.*/
    unsigned int getNeuronGridSize() const;

    //! How many neurons are simulated locally in this model
    unsigned int getNumLocalNeurons() const;

    //! How many neurons are simulated remotely in this model
    unsigned int getNumRemoteNeurons() const;

    //! How many neurons make up the entire model
    unsigned int getNumNeurons() const{ return getNumLocalNeurons() + getNumRemoteNeurons(); }

    //! Find a neuron group by name
    const NeuronGroup *findNeuronGroup(const std::string &name) const;

    //! Find a neuron group by name
    NeuronGroup *findNeuronGroup(const std::string &name);

    NeuronGroup *addNeuronPopulation(const string&, unsigned int, unsigned int, const double *, const double *, int hostID = 0, int deviceID = 0); //!< Method for adding a neuron population to a neuronal network model, using C++ string for the name of the population
    NeuronGroup *addNeuronPopulation(const string&, unsigned int, unsigned int, const vector<double>&, const vector<double>&, int hostID = 0, int deviceID = 0); //!< Method for adding a neuron population to a neuronal network model, using C++ string for the name of the population

    //! Adds a new neuron group to the model using a neuron model managed by the user
    /*! \tparam NeuronModel type of neuron model (derived from NeuronModels::Base).
        \param name string containing unique name of neuron population.
        \param size integer specifying how many neurons are in the population.
        \param model neuron model to use for neuron group.
        \param paramValues parameters for model wrapped in NeuronModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \return pointer to newly created NeuronGroup */
    template<typename NeuronModel>
    NeuronGroup *addNeuronPopulation(const string &name, unsigned int size, const NeuronModel *model,
                                     const typename NeuronModel::ParamValues &paramValues,
                                     const typename NeuronModel::VarValues &varInitialisers,
                                     int hostID = 0, int deviceID = 0)
    {
        if (!GeNNReady) {
            gennError("You need to call initGeNN first.");
        }
        if (final) {
            gennError("Trying to add a neuron population to a finalized model.");
        }

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
            std::forward_as_tuple(name, size, model,
                                  paramValues.getValues(), varInitialisers.getInitialisers(), hostID, deviceID));

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

    //! Adds a new neuron group to the model using a singleton neuron model created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam NeuronModel type of neuron model (derived from NeuronModels::Base).
        \param name string containing unique name of neuron population.
        \param size integer specifying how many neurons are in the population.
        \param paramValues parameters for model wrapped in NeuronModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \return pointer to newly created NeuronGroup */
    template<typename NeuronModel>
    NeuronGroup *addNeuronPopulation(const string &name, unsigned int size,
                                     const typename NeuronModel::ParamValues &paramValues, const typename NeuronModel::VarValues &varInitialisers,
                                     int hostID = 0, int deviceID = 0)
    {
        return addNeuronPopulation<NeuronModel>(name, size, NeuronModel::getInstance(), paramValues, varInitialisers, hostID, deviceID);
    }

    void setNeuronClusterIndex(const string &neuronGroup, int hostID, int deviceID); //!< This function has been deprecated in GeNN 3.1.0

    void activateDirectInput(const string&, unsigned int type); //! This function has been deprecated in GeNN 2.2
    void setConstInp(const string&, double);

    // PUBLIC SYNAPSE FUNCTIONS
    //=========================
    //! Get std::map containing local named SynapseGroup objects in model
    const map<string, SynapseGroup> &getLocalSynapseGroups() const{ return m_LocalSynapseGroups; }

    //! Get std::map containing remote named SynapseGroup objects in model
    const map<string, SynapseGroup> &getRemoteSynapseGroups() const{ return m_RemoteSynapseGroups; }

    //! Get std::map containing names of synapse groups which require postsynaptic learning and their thread IDs within
    //! the postsynaptic learning kernel (padded to multiples of the GPU thread block size)
    const map<string, std::pair<unsigned int, unsigned int>> &getSynapsePostLearnGroups() const{ return m_SynapsePostLearnGroups; }

    //! Get std::map containing names of synapse groups which require synapse dynamics and their thread IDs within
    //! the synapse dynamics kernel (padded to multiples of the GPU thread block size)
    const map<string, std::pair<unsigned int, unsigned int>> &getSynapseDynamicsGroups() const{ return m_SynapseDynamicsGroups; }

    //! Gets std::map containing names and types of each parameter that should be passed through to the synapse kernel
    const map<string, string> &getSynapseKernelParameters() const{ return synapseKernelParameters; }

    //! Gets std::map containing names and types of each parameter that should be passed through to the postsynaptic learning kernel
    const map<string, string> &getSimLearnPostKernelParameters() const{ return simLearnPostKernelParameters; }

    //! Gets std::map containing names and types of each parameter that should be passed through to the synapse dynamics kernel
    const map<string, string> &getSynapseDynamicsKernelParameters() const{ return synapseDynamicsKernelParameters; }

    //! Gets the size of the synapse kernel thread grid
    /*! This is calculated by adding together the number of threads required by each
        synapse population's synapse kernel, padded to be a multiple of GPU's thread block size.*/
    unsigned int getSynapseKernelGridSize() const;

    //! Gets the size of the post-synaptic learning kernel thread grid
    /*! This is calculated by adding together the number of threads required by each
        synapse population's postsynaptic learning kernel, padded to be a multiple of GPU's thread block size.*/
    unsigned int getSynapsePostLearnGridSize() const;

    //! Gets the size of the synapse dynamics kernel thread grid
    /*! This is calculated by adding together the number of threads required by each
        synapse population's synapse dynamics kernel, padded to be a multiple of GPU's thread block size.*/
    unsigned int getSynapseDynamicsGridSize() const;

    //! Find a synapse group by name
    const SynapseGroup *findSynapseGroup(const std::string &name) const;

    //! Find a synapse group by name
    SynapseGroup *findSynapseGroup(const std::string &name);    

    //! Does named synapse group have synapse dynamics
    bool isSynapseGroupDynamicsRequired(const std::string &name) const;

    //! Does named synapse group have post-synaptic learning
    bool isSynapseGroupPostLearningRequired(const std::string &name) const;

    SynapseGroup *addSynapsePopulation(const string &name, unsigned int syntype, SynapseConnType conntype, SynapseGType gtype, const string& src, const string& trg, const double *p); //!< This function has been depreciated as of GeNN 2.2.
    SynapseGroup *addSynapsePopulation(const string&, unsigned int, SynapseConnType, SynapseGType, unsigned int, unsigned int, const string&, const string&, const double *, const double *, const double *); //!< Overloaded version without initial variables for synapses
    SynapseGroup *addSynapsePopulation(const string&, unsigned int, SynapseConnType, SynapseGType, unsigned int, unsigned int, const string&, const string&, const double *, const double *, const double *, const double *); //!< Method for adding a synapse population to a neuronal network model, using C++ string for the name of the population
    SynapseGroup *addSynapsePopulation(const string&, unsigned int, SynapseConnType, SynapseGType, unsigned int, unsigned int, const string&, const string&,
                              const vector<double>&, const vector<double>&, const vector<double>&, const vector<double>&); //!< Method for adding a synapse population to a neuronal network model, using C++ string for the name of the population

    //! Adds a synapse population to the model using weight update and postsynaptic models managed by the user
    /*! \tparam WeightUpdateModel type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name string containing unique name of neuron population.
        \param mtype how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps integer specifying number of timesteps delay this synaptic connection should incur (or NO_DELAY for none)
        \param src string specifying name of presynaptic (source) population
        \param trg string specifying name of postsynaptic (target) population
        \param wum weight update model to use for synapse group.
        \param weightParamValues parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers weight update model state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param psm postsynaptic model to use for synapse group.
        \param postsynapticParamValues parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const string &name, SynapseMatrixType mtype, unsigned int delaySteps, const string& src, const string& trg,
                                       const WeightUpdateModel *wum, const typename WeightUpdateModel::ParamValues &weightParamValues, const typename WeightUpdateModel::VarValues &weightVarInitialisers,
                                       const PostsynapticModel *psm, const typename PostsynapticModel::ParamValues &postsynapticParamValues, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        if (!GeNNReady) {
            gennError("You need to call initGeNN first.");
        }
        if (final) {
            gennError("Trying to add a synapse population to a finalized model.");
        }

        // Get source and target neuron groups
        auto srcNeuronGrp = findNeuronGroup(src);
        auto trgNeuronGrp = findNeuronGroup(trg);

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
                                  wum, weightParamValues.getValues(), weightVarInitialisers.getInitialisers(),
                                  psm, postsynapticParamValues.getValues(), postsynapticVarInitialisers.getInitialisers(),
                                  srcNeuronGrp, trgNeuronGrp,
                                  connectivityInitialiser));

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

    //! Adds a synapse population to the model using singleton weight update and postsynaptic models created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam WeightUpdateModel type of weight update model (derived from WeightUpdateModels::Base).
        \tparam PostsynapticModel type of postsynaptic model (derived from PostsynapticModels::Base).
        \param name string containing unique name of neuron population.
        \param mtype how the synaptic matrix associated with this synapse population should be represented.
        \param delaySteps integer specifying number of timesteps delay this synaptic connection should incur (or NO_DELAY for none)
        \param src string specifying name of presynaptic (source) population
        \param trg string specifying name of postsynaptic (target) population
        \param weightParamValues parameters for weight update model wrapped in WeightUpdateModel::ParamValues object.
        \param weightVarInitialisers weight update model state variable initialiser snippets and parameters wrapped in WeightUpdateModel::VarValues object.
        \param postsynapticParamValues parameters for postsynaptic model wrapped in PostsynapticModel::ParamValues object.
        \param postsynapticVarInitialisers postsynaptic model state variable initialiser snippets and parameters wrapped in NeuronModel::VarValues object.
        \return pointer to newly created SynapseGroup */
    template<typename WeightUpdateModel, typename PostsynapticModel>
    SynapseGroup *addSynapsePopulation(const string &name, SynapseMatrixType mtype, unsigned int delaySteps, const string& src, const string& trg,
                                       const typename WeightUpdateModel::ParamValues &weightParamValues, const typename WeightUpdateModel::VarValues &weightVarInitialisers,
                                       const typename PostsynapticModel::ParamValues &postsynapticParamValues, const typename PostsynapticModel::VarValues &postsynapticVarInitialisers,
                                       const InitSparseConnectivitySnippet::Init &connectivityInitialiser = uninitialisedConnectivity())
    {
        return addSynapsePopulation(name, mtype, delaySteps, src, trg,
                                    WeightUpdateModel::getInstance(), weightParamValues, weightVarInitialisers,
                                    PostsynapticModel::getInstance(), postsynapticParamValues, postsynapticVarInitialisers,
                                    connectivityInitialiser);

    }

    void setSynapseG(const string&, double); //!< This function has been depreciated as of GeNN 2.2.
    void setMaxConn(const string&, unsigned int); //< Set maximum connections per neuron for the given group (needed for optimization by sparse connectivity)
    void setSpanTypeToPre(const string&); //!< Method for switching the execution order of synapses to pre-to-post
    void setSynapseClusterIndex(const string &synapseGroup, int hostID, int deviceID); //!< Function for setting which host and which device a synapse group will be simulated on


    // PUBLIC CURRENT SOURCE FUNCTIONS
    //================================

    //! Get std::map containing local named CurrentSource objects in model
    const map<string, CurrentSource> &getLocalCurrentSources() const{ return m_LocalCurrentSources; }

    //! Get std::map containing remote named CurrentSource objects in model
    const map<string, CurrentSource> &getRemoteCurrentSources() const{ return m_RemoteCurrentSources; }

    //! Gets std::map containing names and types of each parameter that should be passed through to the current source kernel
    const map<string, string> &getCurrentSourceKernelParameters() const{ return currentSourceKernelParameters; }

    //! Find a current source by name
    const CurrentSource *findCurrentSource(const std::string &name) const;

    //! Find a current source by name
    CurrentSource *findCurrentSource(const std::string &name);

    //! Adds a new current source to the model using a current source model managed by the user
    /*! \tparam CurrentSourceModel type of current source model (derived from CurrentSourceModels::Base).
        \param name string containing unique name of current source.
        \param model current source model to use for current source.
        \param targetNeuronGroupName string name of the target neuron group
        \param paramValues parameters for model wrapped in CurrentSourceModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in CurrentSource::VarValues object.
        \return pointer to newly created CurrentSource */
    template<typename CurrentSourceModel>
    CurrentSource *addCurrentSource(const string &currentSourceName, const CurrentSourceModel *model,
                                    const string &targetNeuronGroupName,
                                    const typename CurrentSourceModel::ParamValues &paramValues,
                                    const typename CurrentSourceModel::VarValues &varInitialisers)
    {
        if (!GeNNReady) {
            gennError("You need to call initGeNN first.");
        }
        if (final) {
            gennError("Trying to add a current source to a finalized model.");
        }
        auto targetGroup = findNeuronGroup(targetNeuronGroupName);

#ifdef MPI_ENABLE
        // Get host ID of target neuron group
        const int hostID = targetGroup->getClusterHostID();

        // Determine the host ID
        int mpiHostID = 0;
        MPI_Comm_rank(MPI_COMM_WORLD, &mpiHostID);

        // Pick map to add group to appropriately
        auto &groupMap = (hostID == mpiHostID) ? m_LocalCurrentSources : m_RemoteCurrentSources;
#else
        // If MPI is disabled always add to local current sources
        auto &groupMap = m_LocalCurrentSources;
#endif

        // Add current source to map
        auto result = groupMap.emplace(std::piecewise_construct,
            std::forward_as_tuple(currentSourceName),
            std::forward_as_tuple(currentSourceName, model,
                                  paramValues.getValues(), varInitialisers.getInitialisers()));

        if(!result.second)
        {
            gennError("Cannot add a current source with duplicate name:" + currentSourceName);
            return NULL;
        }
        else
        {
            targetGroup->injectCurrent(&result.first->second);
            return &result.first->second;
        }
    }

    //! Adds a new current source to the model using a singleton current source model created using standard DECLARE_MODEL and IMPLEMENT_MODEL macros
    /*! \tparam CurrentSourceModel type of neuron model (derived from CurrentSourceModel::Base).
        \param currentSourceName string containing unique name of current source.
        \param targetNeuronGroupName string name of the target neuron group
        \param paramValues parameters for model wrapped in CurrentSourceModel::ParamValues object.
        \param varInitialisers state variable initialiser snippets and parameters wrapped in CurrentSourceModel::VarValues object.
        \return pointer to newly created CurrentSource */
    template<typename CurrentSourceModel>
    CurrentSource *addCurrentSource(const string &currentSourceName, const string &targetNeuronGroupName,
                                    const typename CurrentSourceModel::ParamValues &paramValues,
                                    const typename CurrentSourceModel::VarValues &varInitialisers)
    {
        return addCurrentSource<CurrentSourceModel>(currentSourceName, CurrentSourceModel::getInstance(),
                                targetNeuronGroupName, paramValues, varInitialisers);
    }

private:
    //--------------------------------------------------------------------------
    // Private members
    //--------------------------------------------------------------------------
    //!< Named local neuron groups
    map<string, NeuronGroup> m_LocalNeuronGroups;

    //!< Named remote neuron groups
    map<string, NeuronGroup> m_RemoteNeuronGroups;

    //!< Named local synapse groups
    map<string, SynapseGroup> m_LocalSynapseGroups;

    //!< Named remote synapse groups
    map<string, SynapseGroup> m_RemoteSynapseGroups;

    //!< Named local current sources
    map<string, CurrentSource> m_LocalCurrentSources;

    //!< Named remote current sources
    map<string, CurrentSource> m_RemoteCurrentSources;

    //!< Mapping  of synapse group names which have postsynaptic learning to their start and end padded indices
    //!< **THINK** is this the right container?
    map<string, std::pair<unsigned int, unsigned int>> m_SynapsePostLearnGroups;

    //!< Mapping of synapse group names which have synapse dynamics to their start and end padded indices
    //!< **THINK** is this the right container?
    map<string, std::pair<unsigned int, unsigned int>> m_SynapseDynamicsGroups;

    // Kernel members
    map<string, string> m_InitKernelParameters;
    map<string, string> neuronKernelParameters;
    map<string, string> synapseKernelParameters;
    map<string, string> simLearnPostKernelParameters;
    map<string, string> synapseDynamicsKernelParameters;
    map<string, string> currentSourceKernelParameters;

     // Model members
    string name;                //!< Name of the neuronal newtwork model
    string ftype;               //!< Type of floating point variables (float, double, ...; default: float)
    string RNtype;              //!< Underlying type for random number generation (default: uint64_t)
    double dt;                  //!< The integration time step of the model
    bool final;                 //!< Flag for whether the model has been finalized
    bool timing;
    unsigned int seed;
    unsigned int resetKernel;   //!< The identity of the kernel in which the spike counters will be reset.
};

#endif
