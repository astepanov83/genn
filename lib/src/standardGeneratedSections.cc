#include "standardGeneratedSections.h"

// GeNN includes
#include "codeStream.h"
#include "modelSpec.h"

//----------------------------------------------------------------------------
// StandardGeneratedSections
//----------------------------------------------------------------------------
void StandardGeneratedSections::neuronOutputInit(
    CodeStream &os,
    const NeuronGroup &ng,
    const std::string &devPrefix)
{
    if (ng.isDelayRequired()) { // with delay
        // **NOTE** only device spike queue pointers should be reset here
        if(!devPrefix.empty()) {
            os << devPrefix << "spkQuePtr" << ng.getName() << " = (" << devPrefix << "spkQuePtr" << ng.getName() << " + 1) % " << ng.getNumDelaySlots() << ";" << std::endl;
        }

        if (ng.isSpikeEventRequired()) {
            os << devPrefix << "glbSpkCntEvnt" << ng.getName() << "[" << devPrefix << "spkQuePtr" << ng.getName() << "] = 0;" << std::endl;
        }
        if (ng.isTrueSpikeRequired()) {
            os << devPrefix << "glbSpkCnt" << ng.getName() << "[" << devPrefix << "spkQuePtr" << ng.getName() << "] = 0;" << std::endl;
        }
        else {
            os << devPrefix << "glbSpkCnt" << ng.getName() << "[0] = 0;" << std::endl;
        }
    }
    else { // no delay
        if (ng.isSpikeEventRequired()) {
            os << devPrefix << "glbSpkCntEvnt" << ng.getName() << "[0] = 0;" << std::endl;
        }
        os << devPrefix << "glbSpkCnt" << ng.getName() << "[0] = 0;" << std::endl;
    }
}
//----------------------------------------------------------------------------
void StandardGeneratedSections::neuronLocalVarInit(
    CodeStream &os,
    const NeuronGroup &ng,
    const VarNameIterCtx &nmVars,
    const std::string &devPrefix,
    const std::string &localID)
{
    for(const auto &v : nmVars.container) {
        os << v.second << " l" << v.first << " = ";
        os << devPrefix << v.first << ng.getName() << "[";
        if (ng.isVarQueueRequired(v.first) && ng.isDelayRequired()) {
            os << "(delaySlot * " << ng.getNumNeurons() << ") + ";
        }
        os << localID << "];" << std::endl;
    }
}
//----------------------------------------------------------------------------
void StandardGeneratedSections::neuronLocalVarWrite(
    CodeStream &os,
    const NeuronGroup &ng,
    const VarNameIterCtx &nmVars,
    const std::string &devPrefix,
    const std::string &localID)
{
    // store the defined parts of the neuron state into the global state variables dd_V etc
   for(const auto &v : nmVars.container) {
        if (ng.isVarQueueRequired(v.first)) {
            os << devPrefix << v.first << ng.getName() << "[" << ng.getQueueOffset(devPrefix) << localID << "] = l" << v.first << ";" << std::endl;
        }
        else {
            os << devPrefix << v.first << ng.getName() << "[" << localID << "] = l" << v.first << ";" << std::endl;
        }
    }
}
//----------------------------------------------------------------------------
void StandardGeneratedSections::neuronSpikeEventTest(
    CodeStream &os,
    const NeuronGroup &ng,
    const VarNameIterCtx &nmVars,
    const ExtraGlobalParamNameIterCtx &nmExtraGlobalParams,
    const std::string &,
    const std::vector<FunctionTemplate> &functions,
    const std::string &ftype,
    const std::string &rng)
{
    // Create local variable
    os << "bool spikeLikeEvent = false;" << std::endl;

    // Loop through outgoing synapse populations that will contribute to event condition code
    for(const auto &spkEventCond : ng.getSpikeEventCondition()) {
        // Replace of parameters, derived parameters and extraglobalsynapse parameters
        string eCode = spkEventCond.first;

        // code substitutions ----
        substitute(eCode, "$(id)", "n");
        StandardSubstitutions::neuronSpikeEventCondition(eCode, ng, nmVars, nmExtraGlobalParams,
                                                         functions, ftype, rng);

        // Open scope for spike-like event test
        {
            CodeStream::Scope b(os);

            // Use synapse population support code namespace if required
            if (!spkEventCond.second.empty()) {
                os << " using namespace " << spkEventCond.second << ";" << std::endl;
            }

            // Combine this event threshold test with
            os << "spikeLikeEvent |= (" << eCode << ");" << std::endl;
        }
    }
}
//----------------------------------------------------------------------------
void StandardGeneratedSections::neuronCurrentInjection(
    CodeStream &os,
    const NeuronGroup &ng,
    const std::string &devPrefix,
    const std::string &localID,
    const std::vector<FunctionTemplate> &functions,
    const std::string &ftype,
    const std::string &rng)
{
    // Loop through all of neuron group's current sources
    for (const auto *cs : ng.getCurrentSources())
    {
        os << "// current source " << cs->getName() << std::endl;
        CodeStream::Scope b(os);

        const auto* csm = cs->getCurrentSourceModel();
        VarNameIterCtx csVars(csm->getVars());
        DerivedParamNameIterCtx csDerivedParams(csm->getDerivedParams());
        ExtraGlobalParamNameIterCtx csExtraGlobalParams(csm->getExtraGlobalParams());

        // Read current source variables into registers
        for(const auto &v : csVars.container) {
            os <<  v.second << " l" << v.first << " = " << devPrefix << v.first << cs->getName() << "[" << localID << "];" << std::endl;
        }

        string iCode = csm->getInjectionCode();
        substitute(iCode, "$(id)", localID);
        StandardSubstitutions::currentSourceInjection(iCode, cs,
                            csVars, csDerivedParams, csExtraGlobalParams,
                            functions, ftype, rng);
        os << iCode << std::endl;

        // Write updated variables back to global memory
        for(const auto &v : csVars.container) {
             os << devPrefix << v.first << cs->getName() << "[" << localID << "] = l" << v.first << ";" << std::endl;
        }
    }
}
