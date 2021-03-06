#pragma once

// Standard C++ includes
#include <fstream>
#include <set>
#include <string>
#include <vector>

// SpineML simulator includes
#include "modelProperty.h"
#include "networkClient.h"

// Forward declarations
namespace pugi
{
    class xml_node;
}

namespace filesystem
{
    class path;
}

//----------------------------------------------------------------------------
// SpineMLSimulator::LogOutput::Base
//----------------------------------------------------------------------------
namespace SpineMLSimulator
{
namespace LogOutput
{
class Base
{
public:
    Base(const pugi::xml_node &node, double dt, unsigned int numTimeSteps);
    virtual ~Base(){}

    //----------------------------------------------------------------------------
    // Declared virtuals
    //----------------------------------------------------------------------------
    // Record any data required during this timestep
    virtual void record(double dt, unsigned int timestep) = 0;

protected:
    //----------------------------------------------------------------------------
    // Protected API
    //----------------------------------------------------------------------------
    bool shouldRecord(unsigned int timestep) const
    {
        return (timestep >= m_StartTimeStep && timestep < m_EndTimeStep);
    }

    unsigned int getEndTimestep() const{ return m_EndTimeStep; }

private:
    //----------------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------------
    unsigned int m_StartTimeStep;
    unsigned int m_EndTimeStep;
};

//----------------------------------------------------------------------------
// SpineMLSimulator::LogOutput::AnalogueBase
//----------------------------------------------------------------------------
class AnalogueBase : public Base
{
public:
    AnalogueBase(const pugi::xml_node &node, double dt, unsigned int numTimeSteps,
                 const ModelProperty::Base *modelProperty);

protected:
    //----------------------------------------------------------------------------
    // Protected API
    //----------------------------------------------------------------------------
    const scalar *getModelPropertyHostStateVarBegin() const{ return m_ModelProperty->getHostStateVarBegin(); }
    const scalar *getModelPropertyHostStateVarEnd() const{ return m_ModelProperty->getHostStateVarEnd(); }
    void pullModelPropertyFromDevice() const{ m_ModelProperty->pullFromDevice(); }
    unsigned int getModelPropertySize() const{ return m_ModelProperty->getSize(); }

    const std::vector<unsigned int> &getIndices() const{ return m_Indices; }

private:
    //----------------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------------
    // The property that is being logged
    const ModelProperty::Base *m_ModelProperty;

    // Which members of population to log (all if empty)
    std::vector<unsigned int> m_Indices;
};

//----------------------------------------------------------------------------
// SpineMLSimulator::LogOutput::AnalogueFile
//----------------------------------------------------------------------------
class AnalogueFile : public AnalogueBase
{
public:
    AnalogueFile(const pugi::xml_node &node, double dt, unsigned int numTimeSteps,
                 const std::string &port, unsigned int popSize,
                 const filesystem::path &logPath,
                 const ModelProperty::Base *modelProperty);

    //----------------------------------------------------------------------------
    // Declared virtuals
    //----------------------------------------------------------------------------
    // Record any data required during this timestep
    virtual void record(double dt, unsigned int timestep) override;

private:
    //----------------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------------
    std::ofstream m_File;

    // Buffer used, if indices are in use, to store contiguous output data
    std::vector<scalar> m_OutputBuffer;
};

//----------------------------------------------------------------------------
// SpineMLSimulator::LogOutput::AnalogueNetwork
//----------------------------------------------------------------------------
class AnalogueNetwork : public AnalogueBase
{
public:
    AnalogueNetwork(const pugi::xml_node &node, double dt, unsigned int numTimeSteps,
                    const std::string &port, unsigned int popSize,
                    const filesystem::path &logPath,
                    const ModelProperty::Base *modelProperty);

    //----------------------------------------------------------------------------
    // Declared virtuals
    //----------------------------------------------------------------------------
    // Record any data required during this timestep
    virtual void record(double dt, unsigned int timestep) override;

private:
    //----------------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------------
    NetworkClient m_Client;

    // How many GeNN timesteps do we wait before logging
    unsigned int m_IntervalTimesteps;

    // Count down to next time we log
    unsigned int m_CurrentIntervalTimesteps;

    // Buffer used to generate contiguous output data
    // **NOTE** network protocol always uses double precision
    std::vector<double> m_OutputBuffer;
};

//----------------------------------------------------------------------------
// SpineMLSimulator::LogOutput::Event
//----------------------------------------------------------------------------
class Event : public Base
{
public:
    Event(const pugi::xml_node &node, double dt, unsigned int numTimeSteps,
          const std::string &port, unsigned int popSize,
          const filesystem::path &logPath, unsigned int *spikeQueuePtr,
          unsigned int *hostSpikeCount, unsigned int *deviceSpikeCount,
          unsigned int *hostSpikes, unsigned int *deviceSpikes);

    //----------------------------------------------------------------------------
    // Declared virtuals
    //----------------------------------------------------------------------------
    // Record any data required during this timestep
    virtual void record(double dt, unsigned int timestep) override;

private:
    //----------------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------------
    std::ofstream m_File;

    const unsigned int m_PopSize;

    unsigned int *m_SpikeQueuePtr;
    unsigned int *m_HostSpikeCount;
    unsigned int *m_DeviceSpikeCount;

    unsigned int *m_HostSpikes;
    unsigned int *m_DeviceSpikes;

    std::set<unsigned int> m_Indices;
};
}   // namespace LogOutput
}   // namespace SpineMLSimulator