// Standard C++ includes
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>

// Standard C includes
#include <cassert>
#include <cmath>
#include <cstdlib>

// Filesystem includes
#include "filesystem/path.h"

// pugixml includes
#include "pugixml/pugixml.hpp"

// GeNN includes
#include "generateALL.h"
#include "global.h"
#include "modelSpec.h"
#include "utils.h"

// SpineMLCommon includes
#include "connectors.h"
#include "spineMLUtils.h"

// SpineMLGenerator includes
#include "modelParams.h"
#include "neuronModel.h"
#include "passthroughPostsynapticModel.h"
#include "passthroughWeightUpdateModel.h"
#include "postsynapticModel.h"
#include "weightUpdateModel.h"

using namespace SpineMLCommon;
using namespace SpineMLGenerator;

// SpineML generator requires the C++ regex library to be operational
// We assume it is for:
// 1) Non GCC compilers
// 2) GCC 5.X.X and future
// 3) Any future (4.10.X?) releases
// 4) 4.9.1 and subsequent patch releases (GCC fully implemented regex in 4.9.0
// BUT bug 61227 https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61227 prevented \w from working)
#if defined(__GNUC__) && \
    __GNUC__ <= 4 && \
    (__GNUC__ != 4 || (__GNUC_MINOR__ <= 9 && \
                      (__GNUC_MINOR__ != 9 || __GNUC_PATCHLEVEL__ < 1)))
    #error "GeNN SpineML generator requires at least GCC 4.9.1 for functional <regex> library"
#endif

//----------------------------------------------------------------------------
// Anonymous namespace
//----------------------------------------------------------------------------
namespace
{
// Helper function to either find existing model that provides desired parameters or create new one
template<typename Param, typename Model, typename ...Args>
const Model &getCreateModel(const Param &params, std::map<Param, Model> &models, Args... args)
{
    // If no existing model is found that matches parameters
    const auto existingModel = models.find(params);
    if(existingModel == models.end())
    {
        // Load XML document
        pugi::xml_document doc;
        auto result = doc.load_file(params.getURL().c_str());
        if(!result) {
            throw std::runtime_error("Could not open file:" + params.getURL() + ", error:" + result.description());
        }

        // Get SpineML root
        auto spineML = doc.child("SpineML");
        if(!spineML) {
            throw std::runtime_error("XML file:" + params.getURL() + " is not a SpineML component - it has no root SpineML node");
        }

        // Get component class
        auto componentClass = spineML.child("ComponentClass");
        if(!componentClass || strcmp(componentClass.attribute("type").value(), Model::componentClassName) != 0) {
            throw std::runtime_error("XML file:" + params.getURL() + " is not a SpineML component - "
                                    "it's ComponentClass node is either missing or of the incorrect type");
        }

        // Create new model
        // **THINK** some sort of move-semantic magic could probably make this a move
        std::cout << "\tCreating new model" << std::endl;
        auto newModel = models.insert(
            std::make_pair(params, Model(params, componentClass, args...)));

        return newModel.first->second;
    }
    else
    {
        return existingModel->second;
    }
}
//----------------------------------------------------------------------------
// Helper function to either find existing model that provides desired parameters or create new one
template<typename Param, typename Model, typename ...Args>
const Model &getCreatePassthroughModel(const Param &params, std::map<Param, Model> &models, Args... args)
{
    // If no existing model is found that matches parameters
    const auto existingModel = models.find(params);
    if(existingModel == models.end())
    {
        // Create new model
        // **THINK** some sort of move-semantic magic could probably make this a move
        std::cout << "\tCreating new model" << std::endl;
        auto newModel = models.insert(
            std::make_pair(params, Model(params, args...)));

        return newModel.first->second;
    }
    else
    {
        return existingModel->second;
    }
}
//----------------------------------------------------------------------------
// Helper function to read the delay value from a SpineML 'Synapse' node
unsigned int readDelaySteps(const pugi::xml_node &node, double dt)
{
    // Get delay node
    auto delay = node.child("Delay");
    if(delay) {
        auto fixedValue = delay.child("FixedValue");
        if(fixedValue) {
            double delay = fixedValue.attribute("value").as_double();
            return (unsigned int)std::round(delay / dt);
        }
        else {
            throw std::runtime_error("GeNN currently only supports projections with a single delay value");
        }
    }
    else
    {
        throw std::runtime_error("Connector has no 'Delay' node");
    }
}
//----------------------------------------------------------------------------
// Helper function to determine the correct type of GeNN projection to use for a SpineML 'Synapse' node
std::tuple<SynapseMatrixType, unsigned int, unsigned int> getSynapticMatrixType(const filesystem::path &basePath, const pugi::xml_node &node,
                                                                                unsigned int numPre, unsigned int numPost, bool globalG, double dt)
{
    auto oneToOne = node.child("OneToOneConnection");
    if(oneToOne) {
        return std::make_tuple(Connectors::OneToOne::getMatrixType(oneToOne, numPre, numPost, globalG),
                               readDelaySteps(oneToOne, dt),
                               Connectors::OneToOne::estimateMaxRowLength(oneToOne, numPre, numPost));
    }

    auto allToAll = node.child("AllToAllConnection");
    if(allToAll) {
        return std::make_tuple(Connectors::AllToAll::getMatrixType(allToAll, numPre, numPost, globalG),
                               readDelaySteps(allToAll, dt),
                               Connectors::AllToAll::estimateMaxRowLength(allToAll, numPre, numPost));
    }

    auto fixedProbability = node.child("FixedProbabilityConnection");
    if(fixedProbability) {
        return std::make_tuple(Connectors::FixedProbability::getMatrixType(fixedProbability, numPre, numPost, globalG),
                               readDelaySteps(fixedProbability, dt),
                               Connectors::FixedProbability::estimateMaxRowLength(fixedProbability, numPre, numPost));
    }

    auto connectionList = node.child("ConnectionList");
    if(connectionList) {
        // Read maximum row length and any explicit delay from connector
        unsigned int maxRowLength;
        double explicitDelay;
        tie(maxRowLength, explicitDelay) = Connectors::List::readMaxRowLengthAndDelay(basePath, connectionList,
                                                                                      numPre, numPost);

        // If explicit delay wasn't specified, read it from delay child. Otherwise convert explicit delay to timesteps
        unsigned int delay = std::isnan(explicitDelay) ? readDelaySteps(connectionList, dt) : (unsigned int)std::round(explicitDelay / dt);
        return std::make_tuple(Connectors::List::getMatrixType(connectionList, numPre, numPost, globalG),
                               delay, maxRowLength);
    }

    throw std::runtime_error("No supported connection type found for projection");
}
//----------------------------------------------------------------------------
const std::set<std::string> *getNamedSet(const std::map<std::string, std::set<std::string>> &sets, const std::string &name)
{
    // If there is no set with this name return NULL
    auto s = sets.find(name);
    if(s == sets.end()) {
        return nullptr;
    }
    // Otherwise, return a pointer to the set
    else {
        return &s->second;
    }
}
}   // Anonymous namespace

//----------------------------------------------------------------------------
// Entry point
//----------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    try
    {
        if(argc < 2) {
            throw std::runtime_error("Expected experiment XML file passed as argument");
        }

#ifndef CPU_ONLY
        CHECK_CUDA_ERRORS(cudaGetDeviceCount(&deviceCount));
        deviceProp = new cudaDeviceProp[deviceCount];
        for (int device = 0; device < deviceCount; device++) {
            CHECK_CUDA_ERRORS(cudaSetDevice(device));
            CHECK_CUDA_ERRORS(cudaGetDeviceProperties(&(deviceProp[device]), device));
        }
#endif // CPU_ONLY

        // Use filesystem library to get parent path of the network XML file
        const auto experimentPath = filesystem::path(argv[1]).make_absolute();
        const auto basePath = experimentPath.parent_path();

        // If 2nd argument is specified use as output path otherwise use SpineCreator-compliant location
        const auto outputPath = (argc > 2) ? filesystem::path(argv[2]).make_absolute() : basePath.parent_path();

        std::cout << "Output path:" << outputPath.str() << std::endl;
        std::cout << "Parsing experiment '" << experimentPath.str() << "'" << std::endl;

        // Load experiment document
        pugi::xml_document experimentDoc;
        auto experimentResult = experimentDoc.load_file(experimentPath.str().c_str());
        if(!experimentResult) {
            throw std::runtime_error("Unable to load experiment XML file:" + experimentPath.str() + ", error:" + experimentResult.description());
        }

        // Get SpineML root
        auto experimentSpineML = experimentDoc.child("SpineML");
        if(!experimentSpineML) {
            throw std::runtime_error("XML file:" + experimentPath.str() + " is not a SpineML experiment - it has no root SpineML node");
        }

        // Get experiment node
        auto experiment = experimentSpineML.child("Experiment");
        if(!experiment) {
            throw std::runtime_error("No 'Experiment' node found");
        }

        // Loop through inputs
        std::map<std::string, std::set<std::string>> externalInputs;
        for(auto input : experiment.select_nodes(SpineMLUtils::xPathNodeHasSuffix("Input").c_str())) {
            // Read target and port
            const std::string target = SpineMLUtils::getSafeName(input.node().attribute("target").value());
            const std::string port = input.node().attribute("port").value();

            // Add to map
            std::cout << "\tInput targetting: " << target << ":" << port << std::endl;
            if(!externalInputs[target].emplace(port).second) {
                throw std::runtime_error("Multiple inputs targetting " + target + ":" + port);
            }
        }

        // Get model
        auto experimentModel = experiment.child("Model");
        if(!experimentModel) {
            throw std::runtime_error("No 'Model' node found in experiment");
        }

        // Build path to network from URL in model
        auto networkPath = basePath / experimentModel.attribute("network_layer_url").value();
        std::cout << "\tExperiment using model:" << networkPath << std::endl;

        // Loop through configurations (overriden property values)
        std::map<std::string, std::set<std::string>> overridenProperties;
        for(auto config : experimentModel.children("Configuration")) {
            const std::string target = SpineMLUtils::getSafeName(config.attribute("target").value());

            // If this configuration has a property (it probably should)
            auto property = config.child("UL:Property");
            if(property) {
                const std::string propertyName = property.attribute("name").value();

                // Add to map
                std::cout << "\tOverriding property " << target << ":" << propertyName << std::endl;
                if(!overridenProperties[target].emplace(propertyName).second) {
                    throw std::runtime_error("Multiple overrides for property " + target + ":" + propertyName);
                }
            }
        }

        auto simulation = experiment.child("Simulation");
        if(!simulation) {
            throw std::runtime_error("No 'Simulation' node found in experiment");
        }

        auto eulerIntegration = simulation.child("EulerIntegration");
        if(!eulerIntegration) {
            throw std::runtime_error("GeNN only currently supports Euler integration scheme");
        }

        // Read integration timestep
        const double dt = eulerIntegration.attribute("dt").as_double(0.1);
        std::cout << "\tDT = " << dt << "ms" << std::endl;

        // Load XML document
        pugi::xml_document doc;
        auto result = doc.load_file(networkPath.str().c_str());
        if(!result) {
            throw std::runtime_error("Unable to load XML file:" + networkPath.str() + ", error:" + result.description());
        }

        // Get SpineML root
        auto spineML = doc.child("LL:SpineML");
        if(!spineML) {
            throw std::runtime_error("XML file:" + networkPath.str() + " is not a low-level SpineML network - it has no root SpineML node");
        }

        // Neuron, postsyaptic and weight update models required by network
        std::map<ModelParams::Neuron, NeuronModel> neuronModels;
        std::map<ModelParams::Postsynaptic, PostsynapticModel> postsynapticModels;
        std::map<ModelParams::WeightUpdate, WeightUpdateModel> weightUpdateModels;
        std::map<std::string, PassthroughWeightUpdateModel> passthroughWeightUpdateModels;
        std::map<std::string, PassthroughPostsynapticModel> passthroughPostsynapticModels;

        // Get the filename of the network and remove extension
        // to get something usable as a network name
        std::string networkName = networkPath.filename();
        networkName = networkName.substr(0, networkName.find_last_of("."));

        // Instruct GeNN to export all functions as extern "C"
        GENN_PREFERENCES::buildSharedLibrary = true;

        // Turn off autorefractory behaviour
        // **THINK** this allows inputs to be used in threshold conditions but is it actually a good idea more generally?
        GENN_PREFERENCES::autoRefractory = false;

        // Enable new intialization mode for sparse projections where their variables are automatically initialised
        GENN_PREFERENCES::autoInitSparseVars = true;

        // Initialize GeNN
        initGeNN();

        // The neuron model
        NNmodel model;
        model.setDT(dt);
        model.setName(networkName);

        // Loop through populations once to build neuron populations
        for(auto population : spineML.children("LL:Population")) {
            auto neuron = population.child("LL:Neuron");
            if(!neuron) {
                throw std::runtime_error("'Population' node has no 'Neuron' node");
            }

            // Read basic population properties
            auto popName = SpineMLUtils::getSafeName(neuron.attribute("name").value());
            const unsigned int popSize = neuron.attribute("size").as_int();
            std::cout << "Population " << popName << " consisting of ";
            std::cout << popSize << " neurons" << std::endl;

            // If population is a spike source add GeNN spike source
            // **TODO** is this the only special case?
            if(strcmp(neuron.attribute("url").value(), "SpikeSource") == 0) {
                model.addNeuronPopulation<NeuronModels::SpikeSource>(popName, popSize, {}, {});
            }
            else {
                // Get sets of external input and overriden properties for this population
                const auto *externalInputPorts = getNamedSet(externalInputs, popName);
                const auto *overridenPropertyNames = getNamedSet(overridenProperties, popName);

                // Read neuron properties
                std::map<std::string, NewModels::VarInit> varInitialisers;
                ModelParams::Neuron modelParams(basePath, neuron, externalInputPorts,
                                                overridenPropertyNames, varInitialisers);

                // Either get existing neuron model or create new one of no suitable models are available
                const auto &neuronModel = getCreateModel(modelParams, neuronModels);

                // Add population to model
                model.addNeuronPopulation(popName, popSize, &neuronModel,
                                          NeuronModel::ParamValues(varInitialisers, neuronModel),
                                          NeuronModel::VarValues(varInitialisers, neuronModel));
            }
        }

        // Loop through populations AGAIN to build projections and low-level inputs
        for(auto population : spineML.children("LL:Population")) {
            auto neuron = population.child("LL:Neuron");

            // Read source population name from neuron node
            auto popName = SpineMLUtils::getSafeName(neuron.attribute("name").value());
            const NeuronGroup *neuronGroup = model.findNeuronGroup(popName);
            const NeuronModel *neuronModel = dynamic_cast<const NeuronModel*>(neuronGroup->getNeuronModel());

            // Loop through low-level inputs
            for(auto input : neuron.children("LL:Input")) {
                auto srcPopName = SpineMLUtils::getSafeName(input.attribute("src").value());
                const NeuronGroup *srcNeuronGroup = model.findNeuronGroup(srcPopName);
                const NeuronModel *srcNeuronModel = dynamic_cast<const NeuronModel*>(srcNeuronGroup->getNeuronModel());

                std::string srcPort = input.attribute("src_port").value();
                std::string dstPort = input.attribute("dst_port").value();

                std::cout << "Low-level input from population:" << srcPopName << "(" << srcPort << ")->" << popName << "(" << dstPort << ")" << std::endl;

                // Either get existing passthrough weight update model or create new one of no suitable models are available
                const auto &passthroughWeightUpdateModel = getCreatePassthroughModel(srcPort, passthroughWeightUpdateModels,
                                                                                     srcNeuronModel);

                // Either get existing passthrough postsynaptic model or create new one of no suitable models are available
                const auto &passthroughPostsynapticModel = getCreatePassthroughModel(dstPort, passthroughPostsynapticModels,
                                                                                     neuronModel);

                // Determine the GeNN matrix type and number of delay steps
                SynapseMatrixType mtype;
                unsigned int delaySteps;
                unsigned int maxConnections;
                tie(mtype, delaySteps, maxConnections) = getSynapticMatrixType(basePath, input,
                                                                               srcNeuronGroup->getNumNeurons(),
                                                                               neuronGroup->getNumNeurons(),
                                                                               true, dt);

                // Add synapse population to model
                std::string passthroughSynapsePopName = std::string(srcPopName) + "_" + srcPort + "_" + popName + "_"  + dstPort;
                auto synapsePop = model.addSynapsePopulation(passthroughSynapsePopName, mtype, delaySteps, srcPopName, popName,
                                                             &passthroughWeightUpdateModel, {}, {},
                                                             &passthroughPostsynapticModel, {}, {});

                // If matrix uses sparse connectivity set max connections
                if(mtype & SynapseMatrixConnectivity::SPARSE) {
                    synapsePop->setMaxConnections(maxConnections);
                }
            }

            // Loop through outgoing projections
            for(auto projection : population.children("LL:Projection")) {
                // Read destination population name from projection
                auto trgPopName = SpineMLUtils::getSafeName(projection.attribute("dst_population").value());
                const NeuronGroup *trgNeuronGroup = model.findNeuronGroup(trgPopName);
                const NeuronModel *trgNeuronModel = dynamic_cast<const NeuronModel*>(trgNeuronGroup->getNeuronModel());

                // Loop through synapse children
                // **NOTE** multiple projections between the same two populations of neurons are implemented in this way
                for(auto synapse : projection.children("LL:Synapse")) {
                    std::cout << "Projection from population:" << popName << "->" << trgPopName << std::endl;

                    // Get weight update
                    auto weightUpdate = synapse.child("LL:WeightUpdate");
                    if(!weightUpdate) {
                        throw std::runtime_error("'Synapse' node has no 'WeightUpdate' node");
                    }

                    // Get name of weight update
                    const std::string weightUpdateName = SpineMLUtils::getSafeName(weightUpdate.attribute("name").value());

                    // Get sets of external input and overriden properties for this weight update
                    const auto *weightUpdateExternalInputPorts = getNamedSet(externalInputs, weightUpdateName);
                    const auto *weightUpdateOverridenPropertyNames = getNamedSet(overridenProperties, weightUpdateName);

                    // Read weight update properties
                    std::map<std::string, NewModels::VarInit> weightUpdateVarInitialisers;
                    ModelParams::WeightUpdate weightUpdateModelParams(basePath, weightUpdate,
                                                                      popName, trgPopName,
                                                                      weightUpdateExternalInputPorts,
                                                                      weightUpdateOverridenPropertyNames,
                                                                      weightUpdateVarInitialisers);

                    // Either get existing postsynaptic model or create new one of no suitable models are available
                    const auto &weightUpdateModel = getCreateModel(weightUpdateModelParams, weightUpdateModels,
                                                                   neuronModel, trgNeuronModel);

                    // Get post synapse
                    auto postSynapse = synapse.child("LL:PostSynapse");
                    if(!postSynapse) {
                        throw std::runtime_error("'Synapse' node has no 'PostSynapse' node");
                    }

                    // Get name of post synapse
                    const std::string postSynapseName = SpineMLUtils::getSafeName(postSynapse.attribute("name").value());

                    // Get sets of external input and overriden properties for this post synapse
                    const auto *postSynapseExternalInputPorts = getNamedSet(externalInputs, postSynapseName);
                    const auto *postSynapseOverridenPropertyNames = getNamedSet(overridenProperties, postSynapseName);

                    // Read postsynapse properties
                    std::map<std::string, NewModels::VarInit> postsynapticVarInitialisers;
                    ModelParams::Postsynaptic postsynapticModelParams(basePath, postSynapse,
                                                                      trgPopName,
                                                                      postSynapseExternalInputPorts,
                                                                      postSynapseOverridenPropertyNames,
                                                                      postsynapticVarInitialisers);

                    // Either get existing postsynaptic model or create new one of no suitable models are available
                    const auto &postsynapticModel = getCreateModel(postsynapticModelParams, postsynapticModels,
                                                                   trgNeuronModel, &weightUpdateModel);

                    // Global weight value can be used if there are no state variables
                    // **TODO** seperate individualness for PSM and WUM should be used here
                    const bool globalG = weightUpdateModel.getVars().empty() && postsynapticModel.getVars().empty();

                    // Determine the GeNN matrix type and number of delay steps
                    SynapseMatrixType mtype;
                    unsigned int delaySteps;
                    unsigned int maxConnections;
                    tie(mtype, delaySteps, maxConnections) = getSynapticMatrixType(basePath, synapse,
                                                                                   neuronGroup->getNumNeurons(),
                                                                                   trgNeuronGroup->getNumNeurons(),
                                                                                   globalG, dt);

                    // Add synapse population to model
                    // **NOTE** using weight update name is an arbitrary choice but these are guaranteed unique
                    auto synapsePop = model.addSynapsePopulation(weightUpdateName, mtype, delaySteps, popName, trgPopName,
                                                                 &weightUpdateModel, WeightUpdateModel::ParamValues(weightUpdateVarInitialisers, weightUpdateModel), WeightUpdateModel::VarValues(weightUpdateVarInitialisers, weightUpdateModel),
                                                                 &postsynapticModel, PostsynapticModel::ParamValues(postsynapticVarInitialisers, postsynapticModel), PostsynapticModel::VarValues(postsynapticVarInitialisers, postsynapticModel));

                    // If matrix uses sparse connectivity set max connections
                    if(mtype & SynapseMatrixConnectivity::SPARSE) {
                        synapsePop->setMaxConnections(maxConnections);
                    }
                }
            }
        }
    

        // Finalize model
        model.finalize();

        // Write generated code to run directory beneath output path (creating it if necessary)
        auto runPath = (outputPath / "run");
        filesystem::create_directory(runPath);
        runPath = runPath.make_absolute();

        // **NOTE** SpineML doesn't support MPI for now so set local host ID to zero
        const int localHostID = 0;
#ifndef CPU_ONLY
        chooseDevice(model, runPath.str(), localHostID);
#endif // CPU_ONLY
        generate_model_runner(model, runPath.str(), localHostID);

        // Build path to generated model code
        auto modelPath = runPath / (networkName + "_CODE");

        // Use this to build command line for building generated code
        std::string cmd = "cd \"" + modelPath.str() + "\" && ";
#ifdef _WIN32
        cmd += "nmake /nologo clean all";
#else // UNIX
        cmd += "make clean all";
#endif

#ifdef CPU_ONLY
        cmd += " CPU_ONLY=1";
#endif  // CPU_ONLY

        // Execute command
        int retval = system(cmd.c_str());
        if (retval != 0){
            throw std::runtime_error("Building generated code with call:'" + cmd + "' failed with return value:" + std::to_string(retval));
        }
    }
    catch(const std::exception &exception)
    {
        std::cerr << exception.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}