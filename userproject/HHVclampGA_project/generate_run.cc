/*--------------------------------------------------------------------------
  Author: Thomas Nowotny
  
  Institute: Center for Computational Neuroscience and Robotics
             University of Sussex
             Falmer, Brighton BN1 9QJ, UK 
  
  email to:  T.Nowotny@sussex.ac.uk
  
  initial version: 2010-02-07
  
  --------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file userproject/HHVclampGA_project/generate_run.cc

\brief This file is used to run the HHVclampGA model with a single command line.


*/ 
//--------------------------------------------------------------------------

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cfloat>
#include <locale>

#ifdef _WIN32
#include <direct.h>
#include <stdlib.h>
#else // UNIX
#include <sys/stat.h> // needed for mkdir
#endif

#include "stringUtils.h"
#include "command_line_processing.h"

using namespace std;


//--------------------------------------------------------------------------
/*! \brief Main entry point for generate_run.
 */
//--------------------------------------------------------------------------

int main(int argc, char *argv[])
{
  if (argc < 6)
  {
    cerr << "usage: generate_run <CPU=0, AUTO GPU=1, GPU n= \"n+2\"> <protocol> <nPop> <totalT> <outdir> <OPTIONS> \n\
Possible options: \n\
DEBUG=0 or DEBUG=1 (default 0): Whether to run in a debugger \n\
FTYPE=DOUBLE of FTYPE=FLOAT (default FLOAT): What floating point type to use \n\
REUSE=0 or REUSE=1 (default 0): Whether to reuse generated connectivity from an earlier run \n\
CPU_ONLY=0 or CPU_ONLY=1 (default 0): Whether to compile in (CUDA independent) \"CPU only\" mode." << endl;
    exit(1);
  }
  int retval;
  string cmd;
  string gennPath = getenv("GENN_PATH");
  int which = atoi(argv[1]);
  int protocol = atoi(argv[2]);
  int nPop = atoi(argv[3]);
  double totalT = atof(argv[4]);
  string outDir = toString(argv[5]) + "_output";  

   int argStart= 6;
#include "parse_options.h"  // parse options

  // write model parameters
  string fname = "model/HHVClampParameters.h";
  ofstream os(fname.c_str());
  os << "#define NPOP " << nPop << endl;
  os << "#define TOTALT " << totalT << endl;
  string tmps= tS(ftype);
  os << "#define _FTYPE " << "GENN_" << toUpper(tmps) << endl;
  if (which > 1) {
      os << "#define fixGPU " << which-2 << endl;
  }
  os.close();

  // build it
#ifdef _WIN32
  cmd = "cd model && genn-buildmodel.bat ";
#else // UNIX
  cmd = "cd model && genn-buildmodel.sh ";    
#endif
  cmd += "HHVClamp.cc";
  if (dbgMode) cmd += " -d";
  if (cpu_only) cmd += " -c";
#ifdef _WIN32
  cmd += " && msbuild HHVClamp.vcxproj /p:Configuration=";
  if (dbgMode) {
	  cmd += "Debug";
  }
  else {
	  cmd += "Release";
  }
  if (cpu_only) {
	  cmd += "_CPU_ONLY";
  }
#else // UNIX
  cmd += " && make clean all SIM_CODE=" + modelName + "_CODE";
  if (dbgMode) cmd += " DEBUG=1";
  if (cpu_only) cmd += " CPU_ONLY=1";
#endif
  cout << cmd << endl;
  retval=system(cmd.c_str());
  if (retval != 0){
    cerr << "ERROR: Following call failed with status " << retval << ":" << endl << cmd << endl;
    cerr << "Exiting..." << endl;
    exit(1);
  }

  // create output directory
#ifdef _WIN32
  _mkdir(outDir.c_str());
#else // UNIX
  if (mkdir(outDir.c_str(), S_IRWXU | S_IRWXG | S_IXOTH) == -1) {
    cerr << "Directory cannot be created. It may exist already." << endl;
  }
#endif

  // run it!
  cout << "running test..." << endl;
  cmd= toString(argv[5]) + " " + toString(which) + " " + toString(protocol);
#ifdef _WIN32
  if (dbgMode == 1) {
    cmd = "devenv /debugexe model\\HHVClamp.exe " + cmd;
  }
  else {
    cmd = "model\\HHVClamp.exe " + cmd;
  }
#else // UNIX
  if (dbgMode == 1) {
    cmd = "cuda-gdb -tui --args model/VClampGA " + cmd;
  }
  else {
    cmd = "model/VClampGA " + cmd;
  }
#endif
  retval=system(cmd.c_str());
  if (retval != 0){
    cerr << "ERROR: Following call failed with status " << retval << ":" << endl << cmd << endl;
    cerr << "Exiting..." << endl;
    exit(1);
  }

  return 0;
}
