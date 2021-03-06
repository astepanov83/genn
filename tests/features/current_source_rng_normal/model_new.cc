#include "modelSpec.h"

//----------------------------------------------------------------------------
// Neuron
//----------------------------------------------------------------------------
class Neuron : public NeuronModels::Base
{
public:
    DECLARE_MODEL(Neuron, 0, 1);

    SET_SIM_CODE("$(x)= $(Isyn);\n");

    SET_VARS({{"x", "scalar"}});
};
IMPLEMENT_MODEL(Neuron);


void modelDefinition(NNmodel &model)
{
    initGeNN();

    CurrentSourceModels::GaussianNoise::ParamValues paramVals(
        0.0,        // 2 - mean
        1.0);       // 3 - standard deviation

    model.setDT(0.1);
    model.setName("current_source_rng_normal_new");

    model.addNeuronPopulation<Neuron>("Pop", 1000, {}, Neuron::VarValues(0.0));

    model.addCurrentSource<CurrentSourceModels::GaussianNoise>("CurrentSource",
                                                               "Pop",
                                                               paramVals, {});

    model.setPrecision(GENN_FLOAT);
    model.finalize();
}
