// Google test includes
#include "gtest/gtest.h"

// Auto-generated simulation code includess
#include DEFINITIONS_HEADER

// **NOTE** base-class for simulation tests must be
// included after auto-generated globals are includes
#include "../../utils/simulation_test_cont_decoder_matrix.h"

//----------------------------------------------------------------------------
// SimTest
//----------------------------------------------------------------------------
class SimTest : public SimulationTestContDecoderMatrix
{
public:
    //----------------------------------------------------------------------------
    // SimulationTest virtuals
    //----------------------------------------------------------------------------
    virtual void Init()
    {
        // Loop through presynaptic neurons
        for(unsigned int i = 0; i < 10; i++)
        {
            // Initially zero row length
            CSyn.rowLength[i] = 0;
            for(unsigned int j = 0; j < 4; j++)
            {
                // Get value this post synaptic neuron represents
                const unsigned int j_value = (1 << j);

                // If this postsynaptic neuron should be connected, add index
                if(((i + 1) & j_value) != 0)
                {
                    const unsigned int idx = (i * 4) + CSyn.rowLength[i]++;
                    CSyn.ind[idx] = j;
                    gSyn[idx] = 1.0f;
                }
            }
        }
    }
};

TEST_P(SimTest, CorrectDecoding)
{
    // Initialize sparse arrays
    initdecode_matrix_cont_individualg_ragged_new();

    // Check total error is less than some tolerance
    EXPECT_TRUE(Simulate());
}

#ifndef CPU_ONLY
auto simulatorBackends = ::testing::Values(true, false);
#else
auto simulatorBackends = ::testing::Values(false);
#endif

WRAPPED_INSTANTIATE_TEST_CASE_P(MODEL_NAME,
                                SimTest,
                                simulatorBackends);
